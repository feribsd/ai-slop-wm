/*
 * config.c — runtime config file parser
 *
 * Format (shell-style, no '='):
 *   # comment
 *   terminal    st
 *   launcher    dmenu_run
 *   border_px   2
 *   gap         6
 *   master_pct  55
 *   color_focus 5294e2
 *   color_normal 333333
 *
 *   bind  MOD Return  terminal
 *   bind  MOD d       launcher
 *   bind  MOD SHIFT c killclient
 *   bind  MOD j       focusnext
 *   bind  MOD k       focusprev
 *   bind  MOD h       incmaster -5
 *   bind  MOD l       incmaster +5
 *   bind  MOD z       zoom
 *   bind  MOD SHIFT q quit
 *   bind  MOD 1       viewtag 1
 *   bind  MOD SHIFT 1 movetotag 1
 *   bind  MOD F2      spawn firefox
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>   /* strcasecmp on BSD */
#include <ctype.h>
#include <X11/keysym.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>

#include "config.h"
#include "wm.h"

/* ── config state (single global instance) ───────────────── */
Config cfg;

/* ── helpers ─────────────────────────────────────────────── */

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) *--e = '\0';
    return s;
}

/* trim a token in-place (handles trailing spaces within tokenised words) */
static void trimtoken(char *s) {
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)*(e-1))) *--e = '\0';
}

/* parse "MOD", "MOD SHIFT", "MOD CTRL SHIFT" into xcb modifier mask */
static unsigned int parse_mods(char **tokens, int *idx, int count) {
    unsigned int mod = 0;
    while (*idx < count) {
        char *t = tokens[*idx];
        if      (strcasecmp(t, "MOD")   == 0) { mod |= cfg.mod;             (*idx)++; }
        else if (strcasecmp(t, "ALT")   == 0) { mod |= XCB_MOD_MASK_1;     (*idx)++; }
        else if (strcasecmp(t, "CTRL")  == 0) { mod |= XCB_MOD_MASK_CONTROL;(*idx)++;}
        else if (strcasecmp(t, "SHIFT") == 0) { mod |= XCB_MOD_MASK_SHIFT;  (*idx)++;}
        else break; /* next token is the key */
    }
    return mod;
}

/* map common key name strings → X11 KeySyms */
static xcb_keysym_t parse_keysym(const char *name) {
    /* single character */
    if (strlen(name) == 1) return (xcb_keysym_t)name[0];

    /* named specials */
    struct { const char *n; xcb_keysym_t k; } map[] = {
        {"Return",    XK_Return},    {"Enter",     XK_Return},
        {"space",     XK_space},     {"Tab",       XK_Tab},
        {"Escape",    XK_Escape},    {"BackSpace",  XK_BackSpace},
        {"Delete",    XK_Delete},    {"Insert",    XK_Insert},
        {"Home",      XK_Home},      {"End",       XK_End},
        {"Prior",     XK_Prior},     {"Next",      XK_Next},
        {"Up",        XK_Up},        {"Down",      XK_Down},
        {"Left",      XK_Left},      {"Right",     XK_Right},
        {"F1",  XK_F1},  {"F2",  XK_F2},  {"F3",  XK_F3},  {"F4",  XK_F4},
        {"F5",  XK_F5},  {"F6",  XK_F6},  {"F7",  XK_F7},  {"F8",  XK_F8},
        {"F9",  XK_F9},  {"F10", XK_F10}, {"F11", XK_F11}, {"F12", XK_F12},
        {NULL, 0}
    };
    for (int i = 0; map[i].n; i++)
        if (strcasecmp(name, map[i].n) == 0) return map[i].k;

    /* digit keys: "1".."9" already handled above via strlen==1;
       catch "0" explicitly */
    if (strcmp(name, "0") == 0) return XK_0;

    fprintf(stderr, "wm: unknown keysym '%s'\n", name);
    return 0;
}

/* split line into tokens (space-separated), max MAX_TOK */
#define MAX_TOK 16
static int tokenise(char *line, char *toks[], int max) {
    int n = 0;
    char *p = line;
    while (*p && n < max) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        toks[n++] = p;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (*p) *p++ = '\0';
    }
    return n;
}

/* ── action map ──────────────────────────────────────────── */

typedef struct { const char *name; ActionKind kind; } ActionEntry;
static const ActionEntry action_map[] = {
    {"terminal",    ACT_SPAWN_TERMINAL},
    {"launcher",    ACT_SPAWN_LAUNCHER},
    {"spawn",       ACT_SPAWN_CUSTOM},
    {"killclient",  ACT_KILLCLIENT},
    {"focusnext",   ACT_FOCUSNEXT},
    {"focusprev",   ACT_FOCUSPREV},
    {"incmaster",   ACT_INCMASTER},
    {"zoom",        ACT_ZOOM},
    {"viewtag",     ACT_VIEWTAG},
    {"movetotag",   ACT_MOVETOTAG},
    {"toggletag",   ACT_TOGGLETAG},
    {"togglefloat",      ACT_TOGGLEFLOAT},
    {"snapback",         ACT_SNAPBACK},
    {"togglefullscreen", ACT_TOGGLEFULLSCREEN},
    {"reloadconfig",     ACT_RELOADCONFIG},
    {"quit",        ACT_QUIT},
    {NULL,          ACT_QUIT}
};

static ActionKind parse_action(const char *name) {
    for (int i = 0; action_map[i].name; i++)
        if (strcasecmp(name, action_map[i].name) == 0)
            return action_map[i].kind;
    fprintf(stderr, "wm: unknown action '%s'\n", name);
    return ACT_QUIT; /* fallback — won't matter if keysym is 0 */
}

/* ── public API ──────────────────────────────────────────── */

void config_defaults(Config *c) {
    memset(c, 0, sizeof *c);

    strncpy(c->terminal, "st",        sizeof c->terminal - 1);
    strncpy(c->launcher, "dmenu_run", sizeof c->launcher - 1);
    c->mod = XCB_MOD_MASK_4;   /* default: Super/Win key */

    c->border_px       = 2;
    c->outer_border_px = 4;
    c->gap             = 6;
    c->master_pct   = 55;
    c->color_focus  = 0x5294e2;
    c->color_normal = 0x333333;
    c->color_outer  = 0x1a1a2e;   /* dark outer ring */
    c->nkeys        = 0;
}

static void add_bind(Config *c, unsigned int mod, xcb_keysym_t sym,
                     ActionKind kind, int iarg, const char *sarg)
{
    if (c->nkeys >= MAX_KEYS) {
        fprintf(stderr, "wm: too many keybindings (max %d)\n", MAX_KEYS);
        return;
    }
    Binding *b = &c->keys[c->nkeys++];
    b->mod  = mod;
    b->sym  = sym;
    b->kind = kind;
    b->iarg = iarg;
    if (sarg) strncpy(b->sarg, sarg, sizeof b->sarg - 1);
    else b->sarg[0] = '\0';
}

static void parse_bind(Config *c, char *toks[], int n) {
    /* bind  <mods...>  <key>  <action>  [arg] */
    /* toks[0] == "bind", so start at 1 */
    int i = 1;
    unsigned int mod = parse_mods(toks, &i, n);
    if (i >= n) { fprintf(stderr, "wm: bind: missing key\n"); return; }

    xcb_keysym_t sym = parse_keysym(toks[i++]);
    if (!sym)   { return; } /* error already printed */
    if (i >= n) { fprintf(stderr, "wm: bind: missing action\n"); return; }

    ActionKind kind = parse_action(toks[i++]);

    /* optional integer or string argument */
    int   iarg = 0;
    char  sarg[256] = {0};

    if (i < n) {
        char *rest = toks[i];
        /* is it a signed integer? */
        char *end;
        long v = strtol(rest, &end, 10);
        if (*end == '\0') {
            iarg = (int)v;
        } else {
            /* string arg — rejoin remaining tokens with spaces */
            sarg[0] = '\0';
            for (; i < n; i++) {
                if (sarg[0]) strncat(sarg, " ", sizeof sarg - strlen(sarg) - 1);
                strncat(sarg, toks[i], sizeof sarg - strlen(sarg) - 1);
            }
        }
    }

    add_bind(c, mod, sym, kind, iarg, sarg[0] ? sarg : NULL);
}

static void default_binds(Config *c) {
    /* sensible defaults if no bind lines in config */
    unsigned int M  = XCB_MOD_MASK_4;
    unsigned int MS = XCB_MOD_MASK_4 | XCB_MOD_MASK_SHIFT;

    add_bind(c, M,  XK_Return, ACT_SPAWN_TERMINAL, 0, NULL);
    add_bind(c, M,  XK_d,      ACT_SPAWN_LAUNCHER, 0, NULL);
    add_bind(c, M,  XK_j,      ACT_FOCUSNEXT,      0, NULL);
    add_bind(c, M,  XK_k,      ACT_FOCUSPREV,      0, NULL);
    add_bind(c, M,  XK_h,      ACT_INCMASTER,     -5, NULL);
    add_bind(c, M,  XK_l,      ACT_INCMASTER,     +5, NULL);
    add_bind(c, M,  XK_z,      ACT_ZOOM,           0, NULL);
    add_bind(c, MS, XK_c,      ACT_KILLCLIENT,     0, NULL);
    add_bind(c, MS, XK_r,      ACT_RELOADCONFIG,   0, NULL);
    add_bind(c, MS, XK_q,      ACT_QUIT,           0, NULL);

    /* tags 1–9 */
    xcb_keysym_t digits[] = {
        XK_1,XK_2,XK_3,XK_4,XK_5,XK_6,XK_7,XK_8,XK_9
    };
    for (int t = 0; t < 9; t++) {
        add_bind(c, M,  digits[t], ACT_VIEWTAG,   1<<t, NULL);
        add_bind(c, MS, digits[t], ACT_MOVETOTAG, 1<<t, NULL);
    }
}

static unsigned int parse_mod_name(const char *name) {
    if (strcasecmp(name, "super") == 0 || strcasecmp(name, "mod4") == 0 || strcasecmp(name, "win") == 0)
        return XCB_MOD_MASK_4;
    if (strcasecmp(name, "alt")   == 0 || strcasecmp(name, "mod1") == 0)
        return XCB_MOD_MASK_1;
    if (strcasecmp(name, "ctrl")  == 0 || strcasecmp(name, "control") == 0)
        return XCB_MOD_MASK_CONTROL;
    if (strcasecmp(name, "shift") == 0)
        return XCB_MOD_MASK_SHIFT;
    fprintf(stderr, "wm: unknown mod '%s', defaulting to Super\n", name);
    return XCB_MOD_MASK_4;
}

int config_load(Config *c, const char *path) {
    config_defaults(c);

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "wm: config not found at %s, using defaults\n", path);
        default_binds(c);
        return 0;
    }

    int has_binds = 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *l = trim(line);
        if (!*l || *l == '#') continue;

        /* support both "key : value" and "key value" separators.
         * Remove the ':' entirely so it doesn't become a spurious empty token. */
        char *colon = strchr(l, ':');
        if (colon) {
            /* shift everything after ':' left, overwriting ' : ' */
            memmove(colon, colon + 1, strlen(colon + 1) + 1);
        }

        char *toks[MAX_TOK];
        int n = tokenise(l, toks, MAX_TOK);
        if (n < 2) continue;
        for (int ti = 0; ti < n; ti++) trimtoken(toks[ti]);

        char *key = toks[0];
        char *val = toks[1];

        if (strcasecmp(key, "terminal") == 0) {
            /* rejoin all tokens after "terminal" to support multi-word commands */
            c->terminal[0] = '\0';
            for (int ti = 1; ti < n; ti++) {
                if (ti > 1) strncat(c->terminal, " ", sizeof c->terminal - strlen(c->terminal) - 1);
                strncat(c->terminal, toks[ti], sizeof c->terminal - strlen(c->terminal) - 1);
            }
        } else if (strcasecmp(key, "launcher") == 0) {
            /* rejoin all tokens after "launcher" to support e.g. "rofi -show drun" */
            c->launcher[0] = '\0';
            for (int ti = 1; ti < n; ti++) {
                if (ti > 1) strncat(c->launcher, " ", sizeof c->launcher - strlen(c->launcher) - 1);
                strncat(c->launcher, toks[ti], sizeof c->launcher - strlen(c->launcher) - 1);
            }
        }
        else if (strcasecmp(key, "mod") == 0 || strcasecmp(key, "mod_key") == 0) c->mod = parse_mod_name(val);
        else if (strcasecmp(key, "border_px")        == 0) c->border_px        = atoi(val);
        else if (strcasecmp(key, "outer_border_px")  == 0) c->outer_border_px  = atoi(val);
        else if (strcasecmp(key, "gap")              == 0) c->gap              = atoi(val);
        else if (strcasecmp(key, "master_pct")   == 0) c->master_pct   = atoi(val);
        else if (strcasecmp(key, "color_focus")  == 0) c->color_focus  = (uint32_t)strtoul(val, NULL, 16);
        else if (strcasecmp(key, "color_normal") == 0) c->color_normal = (uint32_t)strtoul(val, NULL, 16);
        else if (strcasecmp(key, "color_outer")  == 0) c->color_outer  = (uint32_t)strtoul(val, NULL, 16);
        else if (strcasecmp(key, "bind")         == 0) {
            parse_bind(c, toks, n);
            has_binds = 1;
        } else {
            fprintf(stderr, "wm: unknown directive '%s'\n", key);
        }
    }
    fclose(f);

    if (!has_binds) default_binds(c);
    return 1;
}

char *config_path(void) {
    static char buf[512];
    const char *xdg = getenv("XDG_CONFIG_HOME");
    if (xdg) snprintf(buf, sizeof buf, "%s/wm/wmrc", xdg);
    else {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof buf, "%s/.config/wm/wmrc", home ? home : "~");
    }
    return buf;
}
