# HyprNicheWM

A minimal vibecoded tiling wm its supposed to kinda jokey/ironic just wanted to use
github as a personal back up since it has all the featues i would ever want in a wm.

## Dependencies

**Arch / Artix**
```sh
pacman -S libxcb xcb-util-keysyms libx11
```

**Void Linux**
```sh
xbps-install -S libxcb-devel xcb-util-keysyms-devel libX11-devel
```

**Debian / Ubuntu**
```sh
apt install libxcb1-dev libxcb-keysyms1-dev libx11-dev
```

## Build & Install

```sh
git clone github.com/feribsd/ai-slop-wm.git
cd hyprnichewm
make
sudo make install   # installs to /usr/local/bin/wm
```


```

## Configuration(resembles dwm)

Edit `config.h` and recompile. There is no runtime config file.

```c
#define MOD         XCB_MOD_MASK_1   /* Alt — change to XCB_MOD_MASK_4 for Super */
#define BORDER_PX   4                /* total border width */
#define OUTER_PX    2                /* outer ring width */
#define GAP         10               /* gap between windows in px */
#define MASTER_PCT  55               /* master area % of screen width */
#define BAR_HEIGHT  36               /* reserved space for bar at top */

#define COL_FOCUS   0xffffff         /* focused inner border color */
#define COL_NORMAL  0x444444         /* unfocused inner border color */
#define COL_OUTER   0x282828         /* outer border ring color */

#define TERMINAL    "alacritty"
#define LAUNCHER    "rofi -show drun"
```

## Keybindings

| Key | Action |
|-----|--------|
| `Mod+Return` | Open terminal |
| `Mod+p` | Open launcher |
| `Mod+b` | Open browser |
| `Mod+j/k` | Focus next/prev window |
| `Mod+Tab` | Focus last window |
| `Mod+h/l` | Shrink/grow master area |
| `Mod+z` | Promote focused to master |
| `Mod+f` | Toggle float |
| `Mod+Shift+f` | Snap back to tiling |
| `Mod+m` | Toggle fullscreen |
| `Mod+q` | Kill focused window |
| `Mod+1..9` | Switch to tag |
| `Mod+Shift+1..9` | Move window to tag |
| `Mod+Ctrl+1..9` | Move window to tag and follow |
| `Mod+Shift+q` | Quit |

**Floating windows:**
- `Mod+Button1` drag — move
- `Mod+Button3` drag — resize


## Memory Usage

```sh
cat /proc/$(pidof wm)/status | grep VmRSS
```

Typical: ~2MB give or take
