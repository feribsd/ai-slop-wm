#pragma once
#include <xcb/xcb.h>

typedef struct Client Client;
struct Client {
    xcb_window_t win;
    int          x, y, w, h;
    unsigned int tags;
    int          is_floating;
    int          is_fullscreen;
    Client      *next;
};

extern xcb_connection_t  *conn;
extern xcb_screen_t      *screen;
extern xcb_window_t       root;
extern Client            *clients;
extern Client            *focused;
extern Client            *last_focused;
extern unsigned int       curtag;
extern int                master_w_pct;
extern int                running;
