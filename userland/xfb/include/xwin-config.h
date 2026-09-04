/*
 * xwin-config.h.in
 *
 * This file has all defines used in the xwin ddx
 *
 */
#include <dix-config.h>

/* Winsock networking */
#undef HAS_WINSOCK

/* Cygwin has /dev/windows for signaling new win32 messages */
#undef HAS_DEVWINDOWS

/* Switch on debug messages */
#define CYGDEBUG 0
#define CYGWINDOWING_DEBUG 0
#define CYGMULTIWINDOW_DEBUG 0

/* Default log location */
#define DEFAULT_LOGDIR "/home/kyle/Development/Fiwix/.build/x11-prefix/var/log"

/* Whether we should re-locate the root to where the executable lives */
#undef RELOCATE_PROJECTROOT
