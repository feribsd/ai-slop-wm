/*
 * ewmhtest.c — standalone test: set _NET_SUPPORTING_WM_CHECK and _NET_WM_NAME
 * Build: cc -o ewmhtest ewmhtest.c $(pkg-config --cflags --libs xcb)
 * Run:   ./ewmhtest
 * Check: xprop -root _NET_SUPPORTING_WM_CHECK _NET_WM_NAME
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>

#define WM_NAME "HyprNicheWM"

static xcb_atom_t intern(xcb_connection_t *c, const char *name) {
    xcb_intern_atom_reply_t *r = xcb_intern_atom_reply(c,
        xcb_intern_atom(c, 0, strlen(name), name), NULL);
    xcb_atom_t a = r ? r->atom : XCB_ATOM_NONE;
    free(r);
    fprintf(stderr, "  intern '%s' → atom %u\n", name, a);
    return a;
}

int main(void) {
    xcb_connection_t *conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) {
        fputs("cannot connect\n", stderr); return 1;
    }

    xcb_screen_t *screen = xcb_setup_roots_iterator(xcb_get_setup(conn)).data;
    xcb_window_t  root   = screen->root;

    fprintf(stderr, "root window: 0x%x\n", root);

    xcb_atom_t a_check = intern(conn, "_NET_SUPPORTING_WM_CHECK");
    xcb_atom_t a_name  = intern(conn, "_NET_WM_NAME");
    xcb_atom_t a_utf8  = intern(conn, "UTF8_STRING");

    if (a_check == XCB_ATOM_NONE || a_name == XCB_ATOM_NONE || a_utf8 == XCB_ATOM_NONE) {
        fputs("failed to intern atoms\n", stderr); return 1;
    }

    /* create child window */
    xcb_window_t child = xcb_generate_id(conn);
    uint32_t mask = XCB_CW_OVERRIDE_REDIRECT;
    uint32_t val  = 1;
    xcb_void_cookie_t wck = xcb_create_window_checked(conn,
        XCB_COPY_FROM_PARENT, child, root,
        -1, -1, 1, 1, 0,
        XCB_WINDOW_CLASS_INPUT_OUTPUT,
        screen->root_visual, mask, &val);

    xcb_generic_error_t *err = xcb_request_check(conn, wck);
    if (err) {
        fprintf(stderr, "create_window failed: error %d\n", err->error_code);
        free(err); return 1;
    }
    fprintf(stderr, "child window: 0x%x\n", child);

    /* set props on child */
    err = xcb_request_check(conn, xcb_change_property_checked(conn,
        XCB_PROP_MODE_REPLACE, child, a_check, XCB_ATOM_WINDOW, 32, 1, &child));
    fprintf(stderr, "child _NET_SUPPORTING_WM_CHECK: %s\n", err ? "FAILED" : "ok"); free(err);

    err = xcb_request_check(conn, xcb_change_property_checked(conn,
        XCB_PROP_MODE_REPLACE, child, a_name, a_utf8, 8, strlen(WM_NAME), WM_NAME));
    fprintf(stderr, "child _NET_WM_NAME: %s\n", err ? "FAILED" : "ok"); free(err);

    /* set props on root */
    err = xcb_request_check(conn, xcb_change_property_checked(conn,
        XCB_PROP_MODE_REPLACE, root, a_check, XCB_ATOM_WINDOW, 32, 1, &child));
    fprintf(stderr, "root _NET_SUPPORTING_WM_CHECK: %s\n", err ? "FAILED" : "ok"); free(err);

    err = xcb_request_check(conn, xcb_change_property_checked(conn,
        XCB_PROP_MODE_REPLACE, root, a_name, a_utf8, 8, strlen(WM_NAME), WM_NAME));
    fprintf(stderr, "root _NET_WM_NAME: %s\n", err ? "FAILED" : "ok"); free(err);

    xcb_flush(conn);

    fprintf(stderr, "\nnow run: xprop -root _NET_SUPPORTING_WM_CHECK _NET_WM_NAME\n");
    fprintf(stderr, "press enter to exit (properties will be lost on exit)...\n");
    getchar();

    xcb_destroy_window(conn, child);
    xcb_disconnect(conn);
    return 0;
}
