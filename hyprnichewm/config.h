/*
 * config.h — edit this file to configure HyprNicheWM, then recompile
 */
#pragma once
#include <xcb/xcb.h>
#include <X11/keysym.h>

/* ── modifier ────────────────────────────────────────────── */
#define MOD XCB_MOD_MASK_1   /* Alt — change to XCB_MOD_MASK_4 for Super */

/* ── appearance ──────────────────────────────────────────── */
#define BORDER_PX   4    /* total border width (inner + outer) */
#define OUTER_PX    2    /* outer ring width — must be < BORDER_PX */
#define GAP         10
#define MASTER_PCT  55
#define BAR_HEIGHT  36

#define COL_FOCUS   0xffffff   /* inner border — focused */
#define COL_NORMAL  0x444444   /* inner border — unfocused */
#define COL_OUTER   0x282828   /* outer ring — both states */

/* ── programs ────────────────────────────────────────────── */
#define TERMINAL "alacritty"
#define LAUNCHER "rofi -show drun"

/* ── Arg type ────────────────────────────────────────────── */
typedef union { int i; unsigned int ui; const char *s; } Arg;

/* forward declarations */
void spawn(const Arg *a);
void killclient(const Arg *a);
void focusnext(const Arg *a);
void focusprev(const Arg *a);
void focuslast(const Arg *a);
void incmaster(const Arg *a);
void zoom(const Arg *a);
void togglefloat(const Arg *a);
void snapback(const Arg *a);
void togglefullscreen(const Arg *a);
void viewtag(const Arg *a);
void movetotag(const Arg *a);
void moveandview(const Arg *a);  /* move window to tag then switch to it */
void reloadconfig(const Arg *a);
void quit(const Arg *a);

/* ── keybindings ─────────────────────────────────────────── */
typedef struct { unsigned int mod; xcb_keysym_t sym; void(*fn)(const Arg*); Arg arg; } Key;

static const Key keys[] = {
    { MOD,                    XK_Return, spawn,            { .s = TERMINAL } },
    { MOD,                    XK_p,      spawn,            { .s = LAUNCHER } },
    { MOD,                    XK_b,      spawn,            { .s = "firefox" } },

    { MOD,                    XK_Tab,    focuslast,        {0} },
    { MOD,                    XK_j,      focusnext,        {0} },
    { MOD,                    XK_k,      focusprev,        {0} },

    { MOD,                    XK_h,      incmaster,        { .i = -5 } },
    { MOD,                    XK_l,      incmaster,        { .i = +5 } },
    { MOD,                    XK_z,      zoom,             {0} },

    { MOD,                    XK_f,      togglefloat,      {0} },
    { MOD|XCB_MOD_MASK_SHIFT, XK_f,      snapback,         {0} },
    { MOD,                    XK_m,      togglefullscreen, {0} },

    { MOD,                    XK_q,      killclient,       {0} },
    { MOD|XCB_MOD_MASK_SHIFT, XK_q,      quit,             {0} },

    { MOD, XK_1, viewtag, {.ui=1<<0} }, { MOD|XCB_MOD_MASK_SHIFT, XK_1, movetotag, {.ui=1<<0} },
    { MOD, XK_2, viewtag, {.ui=1<<1} }, { MOD|XCB_MOD_MASK_SHIFT, XK_2, movetotag, {.ui=1<<1} },
    { MOD, XK_3, viewtag, {.ui=1<<2} }, { MOD|XCB_MOD_MASK_SHIFT, XK_3, movetotag, {.ui=1<<2} },
    { MOD, XK_4, viewtag, {.ui=1<<3} }, { MOD|XCB_MOD_MASK_SHIFT, XK_4, movetotag, {.ui=1<<3} },
    { MOD, XK_5, viewtag, {.ui=1<<4} }, { MOD|XCB_MOD_MASK_SHIFT, XK_5, movetotag, {.ui=1<<4} },
    { MOD, XK_6, viewtag, {.ui=1<<5} }, { MOD|XCB_MOD_MASK_SHIFT, XK_6, movetotag, {.ui=1<<5} },
    { MOD, XK_7, viewtag, {.ui=1<<6} }, { MOD|XCB_MOD_MASK_SHIFT, XK_7, movetotag, {.ui=1<<6} },
    { MOD, XK_8, viewtag, {.ui=1<<7} }, { MOD|XCB_MOD_MASK_SHIFT, XK_8, movetotag, {.ui=1<<7} },
    { MOD, XK_9, viewtag, {.ui=1<<8} }, { MOD|XCB_MOD_MASK_SHIFT, XK_9, movetotag, {.ui=1<<8} },

    /* Ctrl+N: move focused window to workspace N then follow it */
    { MOD|XCB_MOD_MASK_CONTROL, XK_1, moveandview, {.ui=1<<0} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_2, moveandview, {.ui=1<<1} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_3, moveandview, {.ui=1<<2} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_4, moveandview, {.ui=1<<3} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_5, moveandview, {.ui=1<<4} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_6, moveandview, {.ui=1<<5} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_7, moveandview, {.ui=1<<6} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_8, moveandview, {.ui=1<<7} },
    { MOD|XCB_MOD_MASK_CONTROL, XK_9, moveandview, {.ui=1<<8} },
};
