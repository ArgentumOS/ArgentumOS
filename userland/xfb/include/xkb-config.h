/*
 * XKB compile-time paths.
 *
 * FNX fork note: meson generated the original from the build prefix
 * (.build/x11-prefix); M1 rebaked the three paths below for the FNX
 * rootfs layout (the server runs on FNX, where /bin and /usr/share
 * are the staged root image paths). xkbcomp is exec'd from
 * XKB_BIN_DIRECTORY; the compiled keymap cache goes in XKM_OUTPUT_DIR.
 */

#pragma once

#define XKB_BASE_DIRECTORY "/usr/share/X11/xkb"

#define XKB_BIN_DIRECTORY "/bin"

#define XKB_DFLT_LAYOUT "us"

#define XKB_DFLT_MODEL "pc105"

#define XKB_DFLT_OPTIONS ""

#define XKB_DFLT_RULES "evdev"

#define XKB_DFLT_VARIANT ""

#define XKM_OUTPUT_DIR "/usr/share/X11/xkb/compiled/"

