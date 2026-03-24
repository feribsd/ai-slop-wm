/*
 * wm.c — HyprNicheWM
 * master-stack tiling WM for X11 using XCB
 * config: edit config.h and recompile
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <X11/keysym.h>

#include "wm.h"
#include "config.h"

/* ── globals ─────────────────────────────────────────────── */

xcb_connection_t  *conn         = NULL;
xcb_screen_t      *screen       = NULL;
xcb_window_t       root         = 0;
xcb_key_symbols_t *keysyms      = NULL;
Client            *clients      = NULL;
Client            *focused      = NULL;
Client            *last_focused = NULL;
unsigned int       curtag       = 1;
int                master_w_pct = MASTER_PCT;
int                running      = 1;

static xcb_atom_t a_wm_type, a_wm_type_dock;
static xcb_atom_t a_net_curdesk, a_wm_delete, a_wm_protocols;
static xcb_atom_t a_net_state, a_net_fullscreen;
static int        ignore_unmaps = 0;

static struct {
    int active, resize;
    Client *c;
    int sx, sy, wx, wy, ww, wh;
} drag;

/* ── helpers ─────────────────────────────────────────────── */

static xcb_atom_t intern(const char *name) {
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(conn,
        xcb_intern_atom(conn, 0, strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r); return a;
}

static void attach(Client *c) { c->next = clients; clients = c; }

static void detach(Client *c) {
    Client **t = &clients;
    while (*t && *t != c) t = &(*t)->next;
    if (*t) *t = c->next;
}

static Client *wintoclient(xcb_window_t w) {
    for (Client *c = clients; c; c = c->next)
        if (c->win == w) return c;
    return NULL;
}

/* ── borders ─────────────────────────────────────────────── */
/* Double border implementation ported from dk window manager (swm/wmutils).
 * BORD_WIDTH = total border width (inner+outer combined)
 * OUTER_PX   = outer ring width
 * inner color touches the window, outer color is the outermost ring */

static void setborder(Client *c, uint32_t col_in, uint32_t col_out) {
    uint32_t b  = BORDER_PX;
    uint32_t o  = OUTER_PX;

    if (!b) return;

    /* set total border width on window */
    xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &b);

    if (b > o && o > 0) {
        /* pixmap sized to full window+border footprint */
        xcb_pixmap_t   pm = xcb_generate_id(conn);
        xcb_gcontext_t gc = xcb_generate_id(conn);
        uint16_t pw = c->w + b;   /* W(c) equivalent */
        uint16_t ph = c->h + b;   /* H(c) equivalent */
        xcb_create_pixmap(conn, screen->root_depth, pm, c->win, pw, ph);
        xcb_create_gc(conn, gc, pm, XCB_GC_FOREGROUND, &col_in);

        /* inner rectangles — the colored band touching the window */
        xcb_rectangle_t inner[] = {
            { c->w,         0,            b - o, c->h + b - o },
            { c->w + b + o, 0,            b - o, c->h + b - o },
            { 0,            c->h,         c->w + b - o, b - o },
            { 0,            c->h + b + o, c->w + b - o, b - o },
            { c->w + b + o, c->h + b + o, b, b               },
        };
        xcb_poly_fill_rectangle(conn, pm, gc, 5, inner);

        /* outer rectangles — the outermost ring */
        xcb_change_gc(conn, gc, XCB_GC_FOREGROUND, &col_out);
        xcb_rectangle_t outer[] = {
            { c->w + b - o, 0,            o, c->h + (b * 2) },
            { c->w + b,     0,            o, c->h + (b * 2) },
            { 0,            c->h + b - o, c->w + (b * 2), o },
            { 0,            c->h + b,     c->w + (b * 2), o },
            { 1,            1,            1, 1               },
        };
        xcb_poly_fill_rectangle(conn, pm, gc, 5, outer);

        xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXMAP, &pm);
        xcb_free_gc(conn, gc);
        xcb_free_pixmap(conn, pm);
    } else {
        /* no outer border — just use a flat color */
        xcb_change_window_attributes(conn, c->win, XCB_CW_BORDER_PIXEL, &col_in);
    }
}

static void unfocus(Client *c) {
    if (!c) return;
    setborder(c, COL_NORMAL, COL_OUTER);
    xcb_flush(conn);
}

static void focus(Client *c) {
    if (focused && focused != c) { last_focused = focused; unfocus(focused); }
    focused = c;
    if (!c) return;
    setborder(c, COL_FOCUS, COL_OUTER);
    xcb_set_input_focus(conn, XCB_INPUT_FOCUS_POINTER_ROOT, c->win, XCB_CURRENT_TIME);
    xcb_flush(conn);
}

/* ── layout ──────────────────────────────────────────────── */

static void arrange(void) {
    int ox = 0, oy = BAR_HEIGHT;
    int sw = screen->width_in_pixels;
    int sh = screen->height_in_pixels - oy;
    int bw = BORDER_PX, gp = GAP;
    int n = 0;

    for (Client *c = clients; c; c = c->next)
        if (!c->is_floating && (c->tags & curtag)) n++;

    int i = 0;
    for (Client *c = clients; c; c = c->next) {
        if (!(c->tags & curtag)) {
            ignore_unmaps++;
            xcb_unmap_window(conn, c->win);
            continue;
        }
        xcb_map_window(conn, c->win);

        if (c->is_fullscreen) {
            uint32_t g[4] = { 0, 0, screen->width_in_pixels, screen->height_in_pixels };
            xcb_configure_window(conn, c->win,
                XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|
                XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, g);
            uint32_t z = 0;
            xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_BORDER_WIDTH, &z);
            uint32_t top = XCB_STACK_MODE_ABOVE;
            xcb_configure_window(conn, c->win, XCB_CONFIG_WINDOW_STACK_MODE, &top);
            continue;
        }
        if (c->is_floating) continue;

        int x, y, w, h;
        if (n == 1) {
            x = ox+gp; y = oy+gp;
            w = sw-2*gp-2*bw; h = sh-2*gp-2*bw;
        } else if (i == 0) {
            x = ox+gp; y = oy+gp;
            w = sw*master_w_pct/100-gp-bw; h = sh-2*gp-2*bw;
        } else {
            int sc = n-1, sx = ox+sw*master_w_pct/100+gp;
            int th = (sh-(sc+1)*gp)/sc-2*bw;
            x = sx; y = oy+gp+(i-1)*(th+gp+2*bw);
            w = sw-(sx-ox)-gp-2*bw; h = th;
        }

        c->x = x; c->y = y; c->w = w; c->h = h;
        uint32_t g[4] = { x, y, w, h };
        xcb_configure_window(conn, c->win,
            XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|
            XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, g);
        /* redraw border with correct w/h for pixmap sizing */
        setborder(c, c == focused ? COL_FOCUS : COL_NORMAL, COL_OUTER);
        i++;
    }
    xcb_flush(conn);
}

/* ── actions ─────────────────────────────────────────────── */

void spawn(const Arg *a) {
    /* double-fork so we don't need waitpid */
    if (fork() == 0) {
        if (fork() == 0) { setsid(); execlp("/bin/sh","sh","-c",a->s,NULL); exit(1); }
        exit(0);
    }
}

void killclient(const Arg *a) {
    (void)a;
    if (!focused) return;
    if (a_wm_delete != XCB_ATOM_NONE) {
        xcb_client_message_event_t ev = {0};
        ev.response_type = XCB_CLIENT_MESSAGE;
        ev.window        = focused->win;
        ev.type          = a_wm_protocols;
        ev.format        = 32;
        ev.data.data32[0]= a_wm_delete;
        xcb_send_event(conn, 0, focused->win, XCB_EVENT_MASK_NO_EVENT, (char*)&ev);
    } else {
        xcb_destroy_window(conn, focused->win);
    }
    xcb_flush(conn);
}

void focusnext(const Arg *a) {
    (void)a;
    if (!clients) return;
    Client *start = focused ? focused->next : clients;
    Client *c = start;
    while (c && !(c->tags & curtag)) c = c->next;
    if (!c) { c = clients; while (c && !(c->tags & curtag)) c = c->next; }
    if (c && c != focused) focus(c);
}

void focusprev(const Arg *a) {
    (void)a;
    Client *prev = NULL, *c = clients;
    while (c && c != focused) { if (c->tags & curtag) prev = c; c = c->next; }
    if (!prev) { c = clients; while (c) { if (c->tags & curtag) prev = c; c = c->next; } }
    if (prev && prev != focused) focus(prev);
}

void focuslast(const Arg *a) {
    (void)a;
    if (last_focused && wintoclient(last_focused->win) && (last_focused->tags & curtag))
        focus(last_focused);
}

void incmaster(const Arg *a) {
    master_w_pct += a->i;
    if (master_w_pct < 10) master_w_pct = 10;
    if (master_w_pct > 90) master_w_pct = 90;
    arrange();
}

void zoom(const Arg *a) {
    (void)a;
    if (!focused) return;
    detach(focused); attach(focused);
    arrange(); focus(focused);
}

void togglefloat(const Arg *a) {
    (void)a;
    if (!focused) return;
    focused->is_floating ^= 1;
    if (focused->is_floating) {
        /* centre at half screen size */
        int sw2 = screen->width_in_pixels, sh2 = screen->height_in_pixels;
        focused->w = sw2/2; focused->h = sh2/2;
        focused->x = (sw2-focused->w)/2; focused->y = (sh2-focused->h)/2;
        uint32_t g[4] = { focused->x, focused->y, focused->w, focused->h };
        xcb_configure_window(conn, focused->win,
            XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y|
            XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT, g);
        uint32_t top = XCB_STACK_MODE_ABOVE;
        xcb_configure_window(conn, focused->win, XCB_CONFIG_WINDOW_STACK_MODE, &top);
    }
    arrange();
}

void snapback(const Arg *a) {
    (void)a;
    if (!focused) return;
    focused->is_floating = 0;
    arrange();
}

void togglefullscreen(const Arg *a) {
    (void)a;
    if (!focused) return;
    focused->is_fullscreen ^= 1;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, focused->win,
                        a_net_state, XCB_ATOM_ATOM, 32,
                        focused->is_fullscreen ? 1 : 0, &a_net_fullscreen);
    arrange();
}

void viewtag(const Arg *a) {
    curtag = a->ui;
    arrange();
    /* broadcast to bar */
    int idx = __builtin_ctz(curtag);
    uint32_t v = idx;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        a_net_curdesk, XCB_ATOM_CARDINAL, 32, 1, &v);
    Client *c = clients;
    while (c && !(c->tags & curtag)) c = c->next;
    focus(c);
    xcb_flush(conn);
}

void movetotag(const Arg *a) {
    if (!focused) return;
    focused->tags = a->ui;
    arrange();
}

void moveandview(const Arg *a) {
    if (!focused) return;
    focused->tags = a->ui;
    curtag = a->ui;
    arrange();
    int idx = __builtin_ctz(curtag);
    uint32_t v = idx;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, root,
                        a_net_curdesk, XCB_ATOM_CARDINAL, 32, 1, &v);
    focus(focused);
    xcb_flush(conn);
}

void reloadconfig(const Arg *a) { (void)a; }

void quit(const Arg *a) { (void)a; running = 0; }

/* ── grabs ───────────────────────────────────────────────── */

static void grabkeys(void) {
    xcb_ungrab_key(conn, XCB_GRAB_ANY, root, XCB_MOD_MASK_ANY);
    int n = sizeof keys / sizeof keys[0];
    for (int i = 0; i < n; i++) {
        xcb_keycode_t *kc = xcb_key_symbols_get_keycode(keysyms, keys[i].sym);
        if (!kc) continue;
        for (xcb_keycode_t *k = kc; *k; k++) {
            xcb_grab_key(conn,1,root,keys[i].mod,*k,XCB_GRAB_MODE_ASYNC,XCB_GRAB_MODE_ASYNC);
            xcb_grab_key(conn,1,root,keys[i].mod|XCB_MOD_MASK_LOCK,*k,XCB_GRAB_MODE_ASYNC,XCB_GRAB_MODE_ASYNC);
            xcb_grab_key(conn,1,root,keys[i].mod|XCB_MOD_MASK_2,*k,XCB_GRAB_MODE_ASYNC,XCB_GRAB_MODE_ASYNC);
        }
        free(kc);
    }
    xcb_grab_button(conn,0,root,XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE,
        XCB_GRAB_MODE_ASYNC,XCB_GRAB_MODE_ASYNC,XCB_NONE,XCB_NONE,XCB_BUTTON_INDEX_1,MOD);
    xcb_grab_button(conn,0,root,XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE,
        XCB_GRAB_MODE_ASYNC,XCB_GRAB_MODE_ASYNC,XCB_NONE,XCB_NONE,XCB_BUTTON_INDEX_3,MOD);
    xcb_flush(conn);
}

/* ── event handlers ──────────────────────────────────────── */

static void remove_client(Client *c) {
    if (focused == c)      focused = NULL;
    if (last_focused == c) last_focused = NULL;
    detach(c); free(c); arrange();
    Client *n = clients;
    while (n && !(n->tags & curtag)) n = n->next;
    focus(n);
}

static int is_dock(xcb_window_t w) {
    xcb_get_property_reply_t *p = xcb_get_property_reply(conn,
        xcb_get_property(conn,0,w,a_wm_type,XCB_ATOM_ATOM,0,32),NULL);
    int dock = 0;
    if (p && xcb_get_property_value_length(p) > 0) {
        xcb_atom_t *t = xcb_get_property_value(p);
        int n = xcb_get_property_value_length(p)/sizeof(xcb_atom_t);
        for (int i = 0; i < n; i++) if (t[i] == a_wm_type_dock) { dock=1; break; }
    }
    free(p); return dock;
}

static void handle_map_request(xcb_map_request_event_t *ev) {
    if (wintoclient(ev->window)) return;
    xcb_get_window_attributes_reply_t *wa = xcb_get_window_attributes_reply(
        conn, xcb_get_window_attributes(conn, ev->window), NULL);
    int ovr = wa ? wa->override_redirect : 0; free(wa);
    if (ovr) return;
    if (is_dock(ev->window)) { xcb_map_window(conn, ev->window); xcb_flush(conn); return; }
    Client *c = calloc(1, sizeof *c);
    c->win = ev->window; c->tags = curtag;
    uint32_t mask = XCB_CW_BORDER_PIXEL | XCB_CW_EVENT_MASK;
    uint32_t v[2] = { COL_NORMAL, XCB_EVENT_MASK_ENTER_WINDOW|XCB_EVENT_MASK_FOCUS_CHANGE };
    xcb_change_window_attributes(conn, c->win, mask, v);
    attach(c); arrange(); focus(c);
}

static void handle_unmap(xcb_unmap_notify_event_t *ev) {
    if (ignore_unmaps > 0) { ignore_unmaps--; return; }
    Client *c = wintoclient(ev->window); if (c) remove_client(c);
}

static void handle_button_press(xcb_button_press_event_t *ev) {
    Client *c = wintoclient(ev->child ? ev->child : ev->event);
    if (!c || !c->is_floating) return;
    focus(c);
    drag.active=1; drag.resize=(ev->detail==XCB_BUTTON_INDEX_3);
    drag.c=c; drag.sx=ev->root_x; drag.sy=ev->root_y;
    drag.wx=c->x; drag.wy=c->y; drag.ww=c->w; drag.wh=c->h;
    uint32_t top = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(conn,c->win,XCB_CONFIG_WINDOW_STACK_MODE,&top);
    xcb_grab_pointer(conn,0,root,XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_POINTER_MOTION,
        XCB_GRAB_MODE_ASYNC,XCB_GRAB_MODE_ASYNC,XCB_NONE,XCB_NONE,XCB_CURRENT_TIME);
    xcb_flush(conn);
}

static void handle_motion(xcb_motion_notify_event_t *ev) {
    if (!drag.active || !drag.c) return;
    int dx = ev->root_x-drag.sx, dy = ev->root_y-drag.sy;
    if (!drag.resize) {
        drag.c->x = drag.wx+dx; drag.c->y = drag.wy+dy;
        uint32_t v[2] = { drag.c->x, drag.c->y };
        xcb_configure_window(conn,drag.c->win,XCB_CONFIG_WINDOW_X|XCB_CONFIG_WINDOW_Y,v);
    } else {
        drag.c->w = (drag.ww+dx) < 50 ? 50 : drag.ww+dx;
        drag.c->h = (drag.wh+dy) < 50 ? 50 : drag.wh+dy;
        uint32_t v[2] = { drag.c->w, drag.c->h };
        xcb_configure_window(conn,drag.c->win,XCB_CONFIG_WINDOW_WIDTH|XCB_CONFIG_WINDOW_HEIGHT,v);
    }
    xcb_flush(conn);
}

static void handle_configure_request(xcb_configure_request_event_t *ev) {
    Client *c = wintoclient(ev->window);
    if (c && !c->is_floating) return;
    uint32_t v[7]; int i=0; uint16_t mask=ev->value_mask;
    if (mask&XCB_CONFIG_WINDOW_X)            v[i++]=ev->x;
    if (mask&XCB_CONFIG_WINDOW_Y)            v[i++]=ev->y;
    if (mask&XCB_CONFIG_WINDOW_WIDTH)        v[i++]=ev->width;
    if (mask&XCB_CONFIG_WINDOW_HEIGHT)       v[i++]=ev->height;
    if (mask&XCB_CONFIG_WINDOW_BORDER_WIDTH) v[i++]=ev->border_width;
    if (mask&XCB_CONFIG_WINDOW_SIBLING)      v[i++]=ev->sibling;
    if (mask&XCB_CONFIG_WINDOW_STACK_MODE)   v[i++]=ev->stack_mode;
    xcb_configure_window(conn, ev->window, mask, v);
    xcb_flush(conn);
}

static void handle_key_press(xcb_key_press_event_t *ev) {
    xcb_keysym_t sym = xcb_key_symbols_get_keysym(keysyms, ev->detail, 0);
    int n = sizeof keys / sizeof keys[0];
    for (int i = 0; i < n; i++)
        if (keys[i].sym == sym &&
            (keys[i].mod & ~XCB_MOD_MASK_LOCK) == (ev->state & ~XCB_MOD_MASK_LOCK))
        { keys[i].fn(&keys[i].arg); return; }
}

/* ── setup ───────────────────────────────────────────────── */

static void setup(void) {
    signal(SIGCHLD, SIG_IGN); /* reap children automatically */

    conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) { write(2,"wm: no display\n",15); exit(1); }
    screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    root   = screen->root;

    uint32_t em = XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT|XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY
                | XCB_EVENT_MASK_BUTTON_PRESS|XCB_EVENT_MASK_BUTTON_RELEASE
                | XCB_EVENT_MASK_POINTER_MOTION|XCB_EVENT_MASK_STRUCTURE_NOTIFY;
    xcb_change_window_attributes(conn, root, XCB_CW_EVENT_MASK, &em);

    keysyms          = xcb_key_symbols_alloc(conn);
    a_wm_type        = intern("_NET_WM_WINDOW_TYPE");
    a_wm_type_dock   = intern("_NET_WM_WINDOW_TYPE_DOCK");
    a_net_curdesk    = intern("_NET_CURRENT_DESKTOP");
    a_wm_delete      = intern("WM_DELETE_WINDOW");
    a_wm_protocols   = intern("WM_PROTOCOLS");
    a_net_state      = intern("_NET_WM_STATE");
    a_net_fullscreen = intern("_NET_WM_STATE_FULLSCREEN");

    /* announce WM via EWMH */
    xcb_atom_t a_check = intern("_NET_SUPPORTING_WM_CHECK");
    xcb_atom_t a_name  = intern("_NET_WM_NAME");
    xcb_atom_t a_utf8  = intern("UTF8_STRING");
    xcb_window_t child = xcb_generate_id(conn);
    uint32_t ov = 1;
    xcb_create_window(conn,XCB_COPY_FROM_PARENT,child,root,-1,-1,1,1,0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT,screen->root_visual,
                      XCB_CW_OVERRIDE_REDIRECT,&ov);
    xcb_change_property(conn,XCB_PROP_MODE_REPLACE,child,a_check,XCB_ATOM_WINDOW,32,1,&child);
    xcb_change_property(conn,XCB_PROP_MODE_REPLACE,child,a_name,a_utf8,8,11,"HyprNicheWM");
    xcb_change_property(conn,XCB_PROP_MODE_REPLACE,root, a_check,XCB_ATOM_WINDOW,32,1,&child);
    xcb_change_property(conn,XCB_PROP_MODE_REPLACE,root, a_name,a_utf8,8,11,"HyprNicheWM");

    uint32_t nd=9, cd=0;
    xcb_change_property(conn,XCB_PROP_MODE_REPLACE,root,
                        intern("_NET_NUMBER_OF_DESKTOPS"),XCB_ATOM_CARDINAL,32,1,&nd);
    xcb_change_property(conn,XCB_PROP_MODE_REPLACE,root,
                        a_net_curdesk,XCB_ATOM_CARDINAL,32,1,&cd);

    grabkeys();
    xcb_flush(conn);
}

int main(void) {
    setup();
    xcb_generic_event_t *ev;
    while (running && (ev = xcb_wait_for_event(conn))) {
        switch (ev->response_type & ~0x80) {
        case XCB_MAP_REQUEST:
            handle_map_request((xcb_map_request_event_t*)ev); break;
        case XCB_UNMAP_NOTIFY:
            handle_unmap((xcb_unmap_notify_event_t*)ev); break;
        case XCB_DESTROY_NOTIFY: {
            Client *c = wintoclient(((xcb_destroy_notify_event_t*)ev)->window);
            if (c) remove_client(c); break;
        }
        case XCB_ENTER_NOTIFY: {
            xcb_enter_notify_event_t *e = (xcb_enter_notify_event_t*)ev;
            if (e->mode == XCB_NOTIFY_MODE_NORMAL) {
                Client *c = wintoclient(e->event); if (c) focus(c);
            }
            break;
        }
        case XCB_CONFIGURE_REQUEST:
            handle_configure_request((xcb_configure_request_event_t*)ev); break;
        case XCB_KEY_PRESS:
            handle_key_press((xcb_key_press_event_t*)ev); break;
        case XCB_BUTTON_PRESS:
            handle_button_press((xcb_button_press_event_t*)ev); break;
        case XCB_MOTION_NOTIFY:
            handle_motion((xcb_motion_notify_event_t*)ev); break;
        case XCB_BUTTON_RELEASE:
            drag.active=0; drag.c=NULL;
            xcb_ungrab_pointer(conn,XCB_CURRENT_TIME); xcb_flush(conn); break;
        }
        free(ev);
    }
    xcb_key_symbols_free(keysyms);
    xcb_disconnect(conn);
    return 0;
}
