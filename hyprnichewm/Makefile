CC     = cc
CFLAGS = -std=c99 -Wall -Wextra -O2 -D_POSIX_C_SOURCE=200809L \
         $(shell pkg-config --cflags xcb xcb-keysyms x11)
LDFLAGS= $(shell pkg-config --libs   xcb xcb-keysyms x11)

all: wm

wm: wm.c wm.h config.h
	$(CC) $(CFLAGS) -o wm wm.c $(LDFLAGS)

clean:
	rm -f wm

install: wm
	install -Dm755 wm /usr/local/bin/wm

.PHONY: all clean install
