/* kestrel.cpp — S4.1a (docs/design/argentum-s4-kestrel.md): the
 * window manager as an argentum app. v1 = a real reparenting WM:
 * SubstructureRedirect on the root; every MapRequest gets a Kestrel
 * frame (an argentum::Window) whose title band is drawn with argentum
 * chrome; the client is reparented into the frame below the band and
 * the frame is placed in the work area below the menubar strip. The
 * strip (Kestrel's own argentum window, full width at the top) will
 * carry the global menubar in S4.2. WM X work runs on Kestrel's own
 * display connection (the toolkit's Application stays untouched).
 *
 * S4.3: the frame is the Platinum one — a 1px outline carried by the
 * frame window's X border (not painted: the sides and bottom of the
 * frame's interior are occupied by the client), a 20px title bar in
 * OS X control order (close + zoom boxed at the left, the title
 * centred, the show/hide-toolbar box at the right), and a 4px lip on
 * the left/right/bottom that the WM owns: the client is inset by it,
 * the resize grips and the grow box live there. Every colour is the
 * theme's, never Platinum's. The drag session (S4.1d) gained a
 * RESIZE mode: a press on an edge or corner grip resizes the frame
 * and the client together. */
#include <argentum/argentum.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <cstdio>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

using namespace argentum;

static const int BAR_H = 30;	/* menubar strip height, px */
static const int MARGIN = 10;	/* work-area margin */

/* S4.3 frame geometry (px). BAND_H is the title bar; FRAME_PX is the
 * frame's lip on the left, right and bottom — the WM owns it, so the
 * client is inset by it and the resize grips/grow box live there. The
 * 1px outline around the whole frame is the frame WINDOW's X border. */
static const int BAND_H = 20;	/* frame title bar, px */
static const int FRAME_PX = 4;	/* the frame's lip (sides + bottom) */
static const int GRIP_PX = 4;	/* edge/corner grab thickness, px */
static const int MIN_FRAME_W = 120;
static const int MIN_FRAME_H = BAND_H + 40;

/* The title band's controls, band-local px. CTL is the box size; the
 * close and zoom boxes sit at the left (OS X order), the toolbar box
 * is right-aligned. */
static const int CTL = 13;
static const int CTL_Y = (BAND_H - CTL) / 2;
static const int CTL_X = 6;		/* close */
static const int CTL_GAP = 5;
static const int CTL2_X = CTL_X + CTL + CTL_GAP;	/* zoom */
static const int CTL_RIGHT_INSET = 6;	/* toolbar box */

/* which band control owns band-local px (x,y) — shared by the chrome
 * view's hit-test and the WM's press handler (a control press is left
 * to the toolkit; everything else in the band starts a move) */
enum { BAND_NONE = 0, BAND_CLOSE, BAND_ZOOM, BAND_TOOLBAR };

static int
bandControlAt(int w, int x, int y, bool hasToolbar)
{
	if (y < CTL_Y || y >= CTL_Y + CTL) {
		return BAND_NONE;
	}
	if (x >= CTL_X && x < CTL_X + CTL) {
		return BAND_CLOSE;
	}
	if (x >= CTL2_X && x < CTL2_X + CTL) {
		return BAND_ZOOM;
	}
	int tx = w - CTL_RIGHT_INSET - CTL;

	if (hasToolbar && x >= tx && x < tx + CTL) {
		return BAND_TOOLBAR;
	}
	return BAND_NONE;
}

/* resize directions: the grab owns whichever edges it hit */
enum { EDGE_LEFT = 1, EDGE_RIGHT = 2, EDGE_TOP = 4, EDGE_BOTTOM = 8 };

static Display *dpy = nullptr;	/* the WM's own connection */
static ::Window root = 0;
static int scr = 0;
static ::Window stripX = 0;	/* the menubar strip's X window */
static int screenW = 0;
static int screenH = 0;
static int xerrCount = 0;

/* ---- S5.2b: the menubar's zones ------------------------------------
 *
 * The mockup's bar has three zones: a system mark on the left, the active
 * app (its name, then its menus), and the date/time on the right. The
 * clock's zone is RESERVED: a menu that would reach it is dropped rather
 * than drawn over it, and the paint and the hit-test share one layout
 * (S4.2b's rule) so they cannot disagree about where a title is.
 */
#define SYS_ICON_W	16	/* the system mark's box, px */
#define SYS_ZONE_W	28	/* its hit zone: the box plus padding */
#define CLOCK_INSET	10	/* from the screen's right edge */
#define CLOCK_GAP	12	/* between the last menu and the clock */

static char gClockText[64] = { 0 };	/* what the clock currently shows */
static char gClockFmt[32] = { 0 };	/* desktop.clockFormat */
static int gClockW = 0;			/* its reserved width (px) */

/* The left edge of the clock's zone: menus stop here. Never so small that
 * the app name and its menus have no room at all. */
static int
clockZoneLeft()
{
	int left = screenW - gClockW - CLOCK_INSET - CLOCK_GAP;

	return (left < SYS_ZONE_W + 40) ? SYS_ZONE_W + 40 : left;
}

/* ---- S5.2a: the wallpaper surface ---------------------------------
 *
 * Kestrel owns the desktop, so it owns the wallpaper: an ordinary
 * Kestrel window at the BOTTOM of the stack, painted with the theme's
 * ramp through the toolkit's own draw path. Being a window (rather than
 * the root's background) is what makes an uncovered region a normal
 * Expose: the toolkit repaints exactly the damaged rect, so a window
 * move only redraws the strip of desktop it uncovered.
 *
 * The colour is configuration (Application::sessionBackground(),
 * i.e. window.background), so the desktop follows the theme, and the
 * ramp is parametric rather than an image — the S5.2 decision. A PNG
 * wallpaper is parked as S5.2h: BitmapImage has no loader.
 *
 * (A server-side PIXMAP as the root's background was tried first.
 * miPaintWindow does implement BackgroundPixmap, but Xfb's tiled fill of
 * the root background rendered as garbage stripes — see the S5.2a
 * record — so the desktop is a window.)
 */
static ::Window deskX = 0;
static argentum::Window *gDesk = nullptr;
static int gDeskW = 0, gDeskH = 0;

static std::uint32_t
mixColor(std::uint32_t a, std::uint32_t b, unsigned int num)
{
	/* num 0..256: 0 = a, 256 = b. Integer only — the double/optimizer
	 * path in this toolchain has produced garbage here before, and a
	 * wrong wallpaper colour is exactly the kind of thing that hides
	 * (the screen still looks like a ramp). */
	unsigned int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
	unsigned int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
	unsigned int r = (ar * (256 - num) + br * num) >> 8;
	unsigned int g = (ag * (256 - num) + bg * num) >> 8;
	unsigned int bl = (ab * (256 - num) + bb * num) >> 8;

	return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (bl & 0xff);
}

/* The desktop's two endpoint tones: 22% toward white, 18% toward black.
 * Kept as functions so the WM's log and the painter cannot drift. */
static std::uint32_t
deskTop(std::uint32_t base)
{
	return mixColor(base, 0xffffff, 56);	/* 56/256 = 22% */
}

static std::uint32_t
deskBot(std::uint32_t base)
{
	return mixColor(base, 0x000000, 46);	/* 46/256 = 18% */
}

/* The desktop's content: one vertical ramp off the session colour,
 * drawn as solid bands. A `fillLinearGradient` version rendered a smooth
 * ramp whose *start* colour matched the theme and whose *end* colour did
 * not (it reached R=255, which the theme's colours never contain) — so
 * the desktop does not depend on that path; see the S5.2a record. */
class DeskView : public argentum::View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		std::uint32_t base =
			argentum::Application::shared().sessionBackground();
		const int band = 4;	/* ~0.3 luma per step: invisible */

		if (w <= 0 || h <= 0)
			return;

		std::uint32_t top = deskTop(base);
		std::uint32_t bot = deskBot(base);

		for (int y = 0; y < h; y += band) {
			int bh = (h - y < band) ? (h - y) : band;
			unsigned int num = (unsigned int)
				((long) y * 256 / (h > 1 ? h - 1 : 1));

			g.fillRect(0, y, (unsigned) w, (unsigned) bh,
				   mixColor(top, bot, num));
		}
	}
};

static DeskView *gDeskView = nullptr;

static void
wallpaperInstall(int w, int h)
{
	if (w <= 0 || h <= 0)
		return;

	double ppt = argentum::Application::shared().pxPerPt();

	if (!gDesk) {
		/* created BEFORE the menubar strip, so the strip is above it */
		gDesk = new argentum::Window();
		if (!gDesk->init("Argentum Desktop", 0, 0, (unsigned) w,
				 (unsigned) h)) {
			fprintf(stderr, "KESTREL: desktop init failed\n");
			delete gDesk;
			gDesk = nullptr;
			return;
		}
		gDeskView = new DeskView();
		gDesk->setContentView(gDeskView);
		deskX = gDesk->xid();
		/* mapped directly, like the strip: Kestrel's own chrome is
		 * never managed (see manageClient's guard) */
		XMapWindow(dpy, deskX);
	} else if (w != gDeskW || h != gDeskH) {
		XResizeWindow(dpy, deskX, (unsigned) w, (unsigned) h);
	}

	gDeskView->setFrame(
		{ {0, 0}, { w / ppt, h / ppt } });
	gDeskView->setNeedsDisplay();
	gDesk->draw();
	XSync(dpy, False);
	/* the desktop is the bottom of the stack */
	XLowerWindow(dpy, deskX);
	XSync(dpy, False);

	gDeskW = w;
	gDeskH = h;
{
	std::uint32_t base =
		argentum::Application::shared().sessionBackground();

	printf("KESTREL: wallpaper %dx%d base=0x%06x top=0x%06x bot=0x%06x\n",
	       w, h, base, deskTop(base), deskBot(base));
}
	fflush(stdout);
}

/* ---- frame chrome -------------------------------------------------- */

class FrameChrome;	/* the frame's title-band content view */

/* A managed client + its Kestrel frame (an argentum::Window whose
 * content view draws the title band). */
struct Managed {
	argentum::Window *frame = nullptr;
	FrameChrome *chrome = nullptr;
	::Window client = 0;
	char title[128] = { 0 };
	int fx = 0, fy = 0;	/* frame origin (inside, px on the root) */
	int fw = 0, fh = 0;	/* frame inside size (px) */
	bool mapped = false;
	/* S4.3 client-published hints */
	unsigned int prefW = 0, prefH = 0;	/* _ARGENTUM_PREFERRED_SIZE */
	unsigned int tbH = 0;			/* _ARGENTUM_TOOLBAR_HEIGHT */
	bool toolbar = false;			/* strip currently shown */
};

static std::vector<Managed *> gFrames;
static void focusClient(Managed *m);	/* S4.1b (defined below) */
static void stripRefresh();		/* S4.2a (defined below) */
static void clockUpdate(bool force);	/* S5.2b (defined below) */
static void dockRefresh();		/* S5.2c (defined below) */
class DockView;

/* ---- S5.2c: the dock ------------------------------------------------
 *
 * A bar of app tiles on the edge named by system.workspace.conf
 * (dock.position = left | right, dock.icon-size), holding PINNED apps —
 * the running group and its separator arrive with the task list (S5.2f)
 * — each with a "running" dot when its app has a window. The dock OWNS a
 * column of the work area: windows are placed and zoomed in what is left,
 * so nothing hides under it.
 *
 * Tiles are vector chrome (a rounded tile, a monogram, a dot): the tree
 * ships no images and this theme's art is parameterised. Launching is a
 * direct fork/execve of the pinned PATH — S5.2d replaces that with real
 * bundles and the launch helper, and until an app has a bundle identity a
 * tile counts as running when a managed window's title matches its title.
 */
#define DOCK_PAD	8	/* padding inside the dock's column, px */
#define TILE_GAP	8	/* between tiles, px */

struct DockPin {
	const char *title;	/* matches the managed window's title */
	const char *monogram;	/* drawn in the tile */
	const char *path;	/* exec'd (S5.2d: a bundle payload) */
};

static const DockPin kPins[] = {
	{ "Argentum widget zoo", "Z", "/System/Shared/tests/widget_zoo" },
	{ "Argentum S1.3 theme chrome", "C",
	  "/System/Shared/tests/theme_chrome" },
};
static const int kPinCount = (int) (sizeof(kPins) / sizeof(kPins[0]));

static bool gDockLeft = false;		/* dock.position */
static int gDockIcon = 48;		/* dock.icon-size */
static ::Window dockX = 0;
static argentum::Window *gDock = nullptr;
static DockView *gDockView = nullptr;

static int
dockW()
{
	return gDockIcon + 2 * DOCK_PAD;
}

static int
dockLeft()
{
	return gDockLeft ? 0 : screenW - dockW();
}

static int
dockRight()
{
	return gDockLeft ? dockW() : screenW;
}

/* The work area: what a window may occupy. The strip owns the top, the
 * dock owns a side column, MARGIN separates them from the windows. */
static int workTop()	{ return BAR_H + MARGIN; }
static int workBottom()	{ return screenH - MARGIN; }
static int workLeft()
{
	return gDockLeft ? dockRight() + MARGIN : MARGIN;
}
static int workRight()
{
	return gDockLeft ? screenW - MARGIN : dockLeft() - MARGIN;
}
static int workWidth()	{ return workRight() - workLeft(); }
static int workHeight()	{ return workBottom() - workTop(); }

/* A tile's top edge inside the dock's own window (dock px). One column,
 * from the top. */
static int
dockTileY(int i)
{
	return DOCK_PAD + i * (gDockIcon + TILE_GAP);
}

/* Which tile is at a point in the dock's window, or -1. The paint and the
 * hit-test share this (S4.2b's rule). */
static int
dockTileAt(int x, int y)
{
	if (x < 0 || x >= dockW())
		return -1;
	for (int i = 0; i < kPinCount; i++) {
		int t = dockTileY(i);

		if (y >= t && y < t + gDockIcon)
			return i;
	}
	return -1;
}

static bool
dockPinRunning(int i)
{
	for (Managed *m : gFrames) {
		if (strcmp(m->title, kPins[i].title) == 0)
			return true;
	}
	return false;
}

static Managed *
findByTitle(const char *title)
{
	for (Managed *m : gFrames) {
		if (strcmp(m->title, title) == 0)
			return m;
	}
	return nullptr;
}

/* A tile click: raise and focus the app if it is running, launch it if it
 * is not (initial-release §3.1: "click to focus or re-launch"). */
static void
dockActivate(int i)
{
	Managed *m = findByTitle(kPins[i].title);

	if (m) {
		XRaiseWindow(dpy, m->frame->xid());
		if (stripX) {
			XRaiseWindow(dpy, stripX);
		}
		focusClient(m);
		printf("KESTREL: dock raise '%s'\n", kPins[i].title);
		fflush(stdout);
		return;
	}
	{
		pid_t pid = fork();

		if (pid == 0) {
			execl(kPins[i].path, kPins[i].path,
			      (char *) nullptr);
			_exit(127);	/* exec failed */
		}
		if (pid < 0) {
			fprintf(stderr, "KESTREL: dock exec failed for %s\n",
				kPins[i].path);
			return;
		}
		printf("KESTREL: dock launch '%s' pid=%d (%s)\n",
		       kPins[i].title, (int) pid, kPins[i].path);
		fflush(stdout);
	}
}
static Managed *gActive = nullptr;	/* S4.1b focused client */
static ::Window ewmhRoot = 0;

/* S4.1b EWMH: publish _NET_ACTIVE_WINDOW on the root. */
static void
setActiveProperty(::Window client)
{
	if (!ewmhRoot) {
		ewmhRoot = DefaultRootWindow(dpy);
	}
	Atom netActive = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

	XChangeProperty(dpy, ewmhRoot, netActive, XA_WINDOW, 32,
			PropModeReplace, (unsigned char *) &client, 1);
	XSync(dpy, False);
}

/* ---- S4.2a: the session socket --------------------------------------
 *
 * One connection per app. Apps connect at map time and publish their
 * menubar as a menu record; Kestrel keeps the parsed model per window
 * and renders the FOCUSED window's titles in the strip. Framing is the
 * toolkit's (sessionWriteFrame): a 4-byte big-endian length then a text
 * payload, "PUBLISH 0x<xid>\n" + menuSerialize's record.
 */
struct SessionConn {
	int fd = -1;
	std::string in;			/* read, frames not complete */
};

struct PublishedMenu {
	::Window client = 0;
	SessionConn *conn = nullptr;	/* borrowed: the socket it came on */
	Menu *menu = nullptr;		/* ours: menuParse's tree */
};

static int stripMenuLayout(const Menu *menu, const char *title, int *xs,
			   int *ws, int *idx, int max, int limit);	/* with the strip */
static std::vector<SessionConn *> gConns;
static std::vector<PublishedMenu *> gMenus;
static int gListenFd = -1;
static void stripRefresh();		/* defined with the strip, below */

static Menu *
menuForClient(::Window client)
{
	for (size_t i = 0; i < gMenus.size(); i++) {
		if (gMenus[i]->client == client) {
			return gMenus[i]->menu;
		}
	}
	return nullptr;
}

static const char *
clientNameFor(::Window client)
{
	for (size_t i = 0; i < gFrames.size(); i++) {
		if (gFrames[i]->client == client) {
			return gFrames[i]->title;
		}
	}
	return "?";
}

/* the titles we parsed, one line — the S4.2a gate reads this */
static void
logMenu(::Window client, const Menu *menu)
{
	char titles[512];
	size_t n = 0;

	titles[0] = 0;
	for (int i = 0; i < menu->itemCount(); i++) {
		MenuItem *item = menu->itemAt(i);
		const char *t = item->title();

		if (item->kind() == MenuItem::Kind::Separator || !t[0]) {
			continue;
		}
		if (n + strlen(t) + 1 >= sizeof(titles)) {
			break;
		}
		if (n) {
			titles[n++] = ',';
		}
		n += (size_t) snprintf(titles + n, sizeof(titles) - n, "%s", t);
	}
	printf("KESTREL: menu 0x%lx '%s' titles=%s\n",
	       (unsigned long) client, clientNameFor(client), titles);
	fflush(stdout);
}

static void
sessionDropConn(SessionConn *c)
{
	if (!c) {
		return;
	}
	/* the menus it published go with it */
	for (size_t i = 0; i < gMenus.size();) {
		if (gMenus[i]->conn == c) {
			delete gMenus[i]->menu;
			delete gMenus[i];
			gMenus.erase(gMenus.begin() + (long) i);
		} else {
			i++;
		}
	}
	if (c->fd >= 0) {
		Application::shared().removeFdHandler(c->fd);
		::close(c->fd);
		c->fd = -1;
	}
	for (size_t i = 0; i < gConns.size(); i++) {
		if (gConns[i] == c) {
			gConns.erase(gConns.begin() + (long) i);
			break;
		}
	}
	delete c;
	stripRefresh();
}

/* a window died: its menubar entry goes too */
static void
dropMenusForClient(::Window client)
{
	bool dropped = false;

	for (size_t i = 0; i < gMenus.size();) {
		if (gMenus[i]->client == client) {
			delete gMenus[i]->menu;
			delete gMenus[i];
			gMenus.erase(gMenus.begin() + (long) i);
			dropped = true;
		} else {
			i++;
		}
	}
	if (dropped) {
		stripRefresh();
	}
}

static void
sessionPublish(SessionConn *c, const std::string &payload)
{
	unsigned long xid = strtoul(payload.c_str() + 8, nullptr, 16);
	size_t nl = payload.find('\n');
	Menu *menu;
	PublishedMenu *entry = nullptr;

	if (nl == std::string::npos) {
		printf("KESTREL: menu 0x%lx bad record\n", xid);
		fflush(stdout);
		return;
	}
	menu = menuParse(payload.c_str() + nl + 1, payload.size() - nl - 1);
	if (!menu) {
		printf("KESTREL: menu 0x%lx bad record\n", xid);
		fflush(stdout);
		return;
	}
	for (size_t i = 0; i < gMenus.size(); i++) {
		if (gMenus[i]->client == (::Window) xid) {
			entry = gMenus[i];
			break;
		}
	}
	if (entry) {
		delete entry->menu;	/* a re-publish replaces the model */
		entry->menu = menu;
		entry->conn = c;
	} else {
		entry = new PublishedMenu();
		entry->client = (::Window) xid;
		entry->conn = c;
		entry->menu = menu;
		gMenus.push_back(entry);
	}
	logMenu((::Window) xid, menu);
	stripRefresh();
}

/* S4.2b: route a pick home. The app owns the item, so the WM only
 * names it by id (menuParse preserved the app's ids); the app runs the
 * action. */
static void
sessionSendPick(::Window client, int id)
{
	PublishedMenu *entry = nullptr;
	char msg[64];

	for (size_t i = 0; i < gMenus.size(); i++) {
		if (gMenus[i]->client == client) {
			entry = gMenus[i];
			break;
		}
	}
	snprintf(msg, sizeof(msg), "PICK 0x%lx %d\n", (unsigned long) client, id);
	if (entry && entry->conn && entry->conn->fd >= 0 &&
	    sessionWriteFrame(entry->conn->fd, msg, strlen(msg))) {
		printf("KESTREL: pick 0x%lx %d\n", (unsigned long) client, id);
	} else {
		printf("KESTREL: pick 0x%lx %d (no session)\n",
		       (unsigned long) client, id);
	}
	fflush(stdout);
}

/* S4.2b: drop the focused client's menu #index under its title and
 * route the pick back to it. */
static void
popUpClientMenu(Managed *m, int itemIndex, int xRootPx)
{
	Menu *root;
	MenuItem *item;
	Menu *sub;
	::Window client;

	if (!m) {
		return;
	}
	root = menuForClient(m->client);
	item = root ? root->itemAt(itemIndex) : nullptr;
	sub = item ? item->submenu() : nullptr;
	client = m->client;
	if (!sub) {
		return;		/* an item with no submenu: nothing to drop */
	}
	menuPopUp(sub, xRootPx, BAR_H, [client](int id) {
		sessionSendPick(client, id);
	});
}

/* S5.2b: Kestrel's own menu, behind the system mark. Its items are DESKTOP
 * actions — session items (log out, restart, sleep) belong to sessionmgr
 * and are deliberately not here. Built once, then reused: menuPopUp without
 * a pick handler runs the item's own action (the S2.3c path). */
static void
arrangeWindows()
{
	for (Managed *m : gFrames) {
		if (m->mapped) {
			XRaiseWindow(dpy, m->frame->xid());
		}
	}
	if (stripX) {
		XRaiseWindow(dpy, stripX);	/* the bar stays on top */
	}
	XSync(dpy, False);
	printf("KESTREL: action arrange (%d frame(s))\n", (int) gFrames.size());
	fflush(stdout);
}

static Menu *
systemMenu()
{
	static Menu *menu = nullptr;

	if (menu) {
		return menu;
	}
	menu = new Menu();
	{
		MenuItem *about = new MenuItem("About Argentum Desktop");

		about->setAction([]() {
			printf("KESTREL: action about\n");
			fflush(stdout);
		});
		menu->addItem(about);
	}
	menu->addSeparator();
	{
		MenuItem *arrange = new MenuItem("Arrange Windows in Front");

		arrange->setAction(arrangeWindows);
		menu->addItem(arrange);
	}
	return menu;
}

static void
popUpSystemMenu()
{
	Menu *m = systemMenu();

	printf("KESTREL: system menu open (%d item(s))\n", m->itemCount());
	fflush(stdout);
	menuPopUp(m, 0, BAR_H);
}

static void
sessionMessage(SessionConn *c, const std::string &payload)
{
	if (payload.compare(0, 8, "PUBLISH ") == 0) {
		sessionPublish(c, payload);
	}
	/* anything else is ignored (forward compatibility) */
}

static void
sessionRead(SessionConn *c)
{
	char buf[4096];

	if (!c || c->fd < 0) {
		return;
	}
	for (;;) {
		ssize_t n = read(c->fd, buf, sizeof(buf));

		if (n > 0) {
			c->in.append(buf, (size_t) n);
			continue;
		}
		if (n == 0) {
			sessionDropConn(c);	/* the app closed */
			return;
		}
		if (errno == EINTR) {
			continue;
		}
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			break;
		}
		sessionDropConn(c);		/* an error: drop it */
		return;
	}

	while (c->in.size() >= 4) {
		const unsigned char *p = (const unsigned char *) c->in.data();
		size_t len = ((size_t) p[0] << 24) | ((size_t) p[1] << 16) |
			     ((size_t) p[2] << 8) | (size_t) p[3];

		if (len > 64 * 1024) {
			sessionDropConn(c);	/* desynced: unusable */
			return;
		}
		if (c->in.size() < 4 + len) {
			break;			/* wait for the rest */
		}
		std::string payload = c->in.substr(4, len);

		c->in.erase(0, 4 + len);
		sessionMessage(c, payload);
	}
}

static void
sessionAccept()
{
	if (gListenFd < 0) {
		return;
	}
	for (;;) {
		int fd = accept(gListenFd, nullptr, nullptr);
		int fl;
		SessionConn *c;

		if (fd < 0) {
			return;			/* EAGAIN: all drained */
		}
		if ((int) gConns.size() >= Application::kMaxFdHandlers - 1) {
			::close(fd);		/* no slot to poll it in */
			continue;
		}
		fl = fcntl(fd, F_GETFL, 0);
		if (fl >= 0) {
			fcntl(fd, F_SETFL, fl | O_NONBLOCK);
		}
		c = new SessionConn();
		c->fd = fd;
		gConns.push_back(c);
		Application::shared().addFdHandler(fd, [c] { sessionRead(c); });
	}
}

static bool
sessionOpen()
{
	struct sockaddr_un sun;
	int fd = socket(AF_UNIX, SOCK_STREAM, 0);
	int fl;

	if (fd < 0) {
		return false;
	}
	/* a stale node from a session that died without cleaning up */
	unlink(kSessionSocketPath);
	memset(&sun, 0, sizeof(sun));
	sun.sun_family = AF_UNIX;
	strncpy(sun.sun_path, kSessionSocketPath, sizeof(sun.sun_path) - 1);
	if (bind(fd, (struct sockaddr *) &sun, sizeof(sun)) < 0 ||
	    listen(fd, 8) < 0) {
		fprintf(stderr, "KESTREL: session socket failed\n");
		::close(fd);
		return false;
	}
	fl = fcntl(fd, F_GETFL, 0);
	if (fl >= 0) {
		fcntl(fd, F_SETFL, fl | O_NONBLOCK);
	}
	gListenFd = fd;
	Application::shared().addFdHandler(fd, [] { sessionAccept(); });
	printf("KESTREL: session socket %s\n", kSessionSocketPath);
	fflush(stdout);
	return true;
}

/* The frame's content: the chrome title band (its controls + the
 * client's WM_NAME) over a plate that fills the rest of the frame —
 * the lip the client is inset by. Every colour comes from the theme
 * (S4.3 coloration table): the active column, else the same shapes
 * stepped down through the disabled state. */
class FrameChrome : public View {
public:
	explicit FrameChrome(const char *title)
	{
		strncpy(title_, title, sizeof(title_) - 1);
	}

	/* S4.1c: clicking the close box asks Kestrel to close the
	 * client (wired by Kestrel; the default does nothing). */
	void setOnClose(std::function<void()> cb)
	{
		closeCb_ = std::move(cb);
	}

	/* S4.3: the zoom box — Kestrel grows the frame to the size the
	 * client published (_ARGENTUM_PREFERRED_SIZE). */
	void setOnZoom(std::function<void()> cb)
	{
		zoomCb_ = std::move(cb);
	}

	/* S4.3: the show/hide-toolbar box (only drawn when the client
	 * declared a toolbar strip). */
	void setOnToolbar(std::function<void()> cb)
	{
		toolbarCb_ = std::move(cb);
	}

	void setToolbarVisible(bool visible)
	{
		if (toolbarVisible_ == visible) {
			return;
		}
		toolbarVisible_ = visible;
		setNeedsDisplay();
	}

	/* S4.1b: clicking the band background activates the frame */
	void setOnActivate(std::function<void()> cb)
	{
		activateCb_ = std::move(cb);
	}

	void mouseDown(const MouseEvent &e) override
	{
		Application &app = Application::shared();
		double ppt = app.pxPerPt();
		Rect f = frame();
		int w = (int) (f.size.w * ppt + 0.5);
		int x = (int) (e.x * ppt + 0.5);
		int y = (int) (e.y * ppt + 0.5);

		/* only the title band is the frame's; below it the client
		 * covers the frame's interior, so a press there can only
		 * arrive when the client is gone (doing nothing is right) */
		if (y < 0 || y >= BAND_H) {
			return;
		}
		switch (bandControlAt(w, x, y, toolbarVisible_)) {
		case BAND_CLOSE:
			if (closeCb_) {
				closeCb_();
			}
			break;
		case BAND_ZOOM:
			if (zoomCb_) {
				zoomCb_();
			}
			break;
		case BAND_TOOLBAR:
			if (toolbarCb_) {
				toolbarCb_();
			}
			break;
		default:
			if (activateCb_) {
				activateCb_();
			}
			break;
		}
	}

	/* S4.1b: the active frame's band is accent-tinted; the inactive
	 * one goes flat (S4.3: no pinstripes), the outline/label step
	 * down through the theme's disabled state. */
	void setActive(bool active)
	{
		if (active_ == active) {
			return;
		}
		active_ = active;
		setNeedsDisplay();
	}

	/* the theme's tones for the current focus state (Kestrel also
	 * needs them for the frame window's X border) */
	std::uint32_t outlineTone(Theme &t) const
	{
		return active_ ? t.chromeOutline() :
				 t.state(ControlState::Disabled).outline;
	}

	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		int band = BAND_H;
		Theme::Params p = t.state(active_ ? ControlState::Armed :
					  ControlState::Idle);
		std::uint32_t outline = outlineTone(t);
		std::uint32_t label = active_ ?
			t.text() : t.state(ControlState::Disabled).label;
		/* the glyphs read against the band's own surface tone */
		std::uint32_t glyph = active_ ? p.fillTop : t.chromeTop();

		if (band > h) {
			band = h;
		}
		/* the frame's body: the lip the client is inset by */
		g.fillRect(0, 0, (unsigned) w, (unsigned) h, t.page());
		/* the title bar: the S4.1b accent tint when active, flat
		 * chrome when not */
		if (active_) {
			g.fillRoundedGradient(0, 0, (unsigned) w,
					      (unsigned) band, 0, p.fillTop,
					      p.fillBottom);
		} else {
			g.fillRect(0, 0, (unsigned) w, (unsigned) band,
				   t.chromeTop());
		}
		/* 1px separator under the bar */
		g.fillRect(0, band - 1, (unsigned) w, 1, outline);
		/* close + zoom at the left, the toolbar toggle at the right */
		bandBox(g, CTL_X, outline, glyph, BAND_CLOSE);
		bandBox(g, CTL2_X, outline, glyph, BAND_ZOOM);
		if (toolbarVisible_) {
			bandBox(g, w - CTL_RIGHT_INSET - CTL, outline, glyph,
				BAND_TOOLBAR);
		}
		/* the title, centred (clamped clear of the controls) */
		if (title_[0]) {
			TextMetrics m = textMetrics(t.fontFamily(),
						    t.fontSizePt(), "Ag");
			double box = (m.ascentPt + m.descentPt + 2.0 / ppt);
			int ty = (int) ((band - box * ppt) / 2.0);
			double tw = textMetrics(t.fontFamily(), t.fontSizePt(),
						title_).widthPt * ppt;
			int lo = CTL2_X + CTL + 4;
			int hi = w - CTL_RIGHT_INSET - CTL - 4;
			int tx = (int) ((w - tw) / 2.0);

			if (tx < lo) {
				tx = lo;
			}
			if (hi > lo && tx + (int) tw > hi) {
				tx = hi - (int) tw;
			}
			if (tx < lo) {
				tx = lo;
			}
			g.drawText(t.fontFamily(), t.fontSizePt(), tx, ty,
				   title_, label);
		}
		/* the grow box: two short 45-degree lines in the
		 * lower-right of the lip (the resize indicator) */
		g.drawLine(w - 2, h - 2, w - FRAME_PX, h - FRAME_PX, outline);
		g.drawLine(w - 2, h - FRAME_PX, w - FRAME_PX, h - 2, outline);
	}

private:
	/* one band control: a rounded plate with a glyph cut out of it */
	void bandBox(GraphicsContext &g, int x, std::uint32_t plate,
		     std::uint32_t glyph, int kind)
	{
		int y = CTL_Y;
		int a = 3;
		int b = CTL - 4;

		g.fillRoundedRect(x, y, (unsigned) CTL, (unsigned) CTL, 2,
				  plate);
		if (kind == BAND_CLOSE) {
			g.drawLine(x + a, y + a, x + b, y + b, glyph);
			g.drawLine(x + a, y + b, x + b, y + a, glyph);
		} else if (kind == BAND_ZOOM) {
			g.drawLine(x + a, y + CTL / 2, x + b, y + CTL / 2,
				   glyph);
			g.drawLine(x + CTL / 2, y + a, x + CTL / 2, y + b,
				   glyph);
		} else {
			/* the toolbar toggle: two short bars */
			g.drawLine(x + a, y + 4, x + b, y + 4, glyph);
			g.drawLine(x + a, y + CTL - 5, x + b, y + CTL - 5,
				   glyph);
		}
	}

	char title_[128] = { 0 };
	bool active_ = false;
	bool toolbarVisible_ = false;
	std::function<void()> closeCb_;
	std::function<void()> zoomCb_;
	std::function<void()> toolbarCb_;
	std::function<void()> activateCb_;
};

/* ---- S4.3 frame geometry ------------------------------------------- */

/* The nearest thing to the frame's own tone for `active`: the theme's
 * values, exactly as the chrome view draws them (the frame WINDOW's X
 * border must match the painted band) */
static std::uint32_t
outlineTone(bool active)
{
	Theme &t = Application::shared().theme();

	return active ? t.chromeOutline() : t.state(ControlState::Disabled).outline;
}

/* The client's rect inside the frame (px, frame-local): inset by the
 * frame's lip on the sides and bottom, the band on top. One helper so
 * the reparent, the resize and ConfigureRequest cannot drift apart. */
static void
clientRect(const Managed *m, int *x, int *y, int *w, int *h)
{
	*x = FRAME_PX;
	*y = BAND_H;
	*w = m->fw - 2 * FRAME_PX;
	*h = m->fh - BAND_H - FRAME_PX;
	if (*w < 1) {
		*w = 1;
	}
	if (*h < 1) {
		*h = 1;
	}
}

/* The one place frame geometry is applied: move+resize the frame and
 * (mapped) its client together. The toolkit's Window sees the
 * ConfigureNotify and relayouts/repaints through its normal path. */
static void
applyFrameGeometry(Managed *m, int fx, int fy, int fw, int fh)
{
	if (fw < MIN_FRAME_W) {
		fw = MIN_FRAME_W;
	}
	if (fh < MIN_FRAME_H) {
		fh = MIN_FRAME_H;
	}
	m->fx = fx;
	m->fy = fy;
	m->fw = fw;
	m->fh = fh;
	if (!m->frame || !m->mapped) {
		return;
	}
	int cx, cy, cw, ch;

	clientRect(m, &cx, &cy, &cw, &ch);
	XMoveResizeWindow(dpy, m->frame->xid(), fx, fy, (unsigned) fw,
			  (unsigned) fh);
	XMoveResizeWindow(dpy, m->client, cx, cy, (unsigned) cw, (unsigned) ch);
	XSync(dpy, False);
}

/* read a CARDINAL property (`n` words) off a client window */
static bool
getCardinals(::Window w, const char *name, unsigned long *out, int n)
{
	Atom a = XInternAtom(dpy, name, False);
	Atom type = 0;
	int fmt = 0;
	unsigned long cnt = 0, after = 0;
	unsigned char *data = nullptr;
	bool ok = false;

	if (XGetWindowProperty(dpy, w, a, 0, 4, False, XA_CARDINAL, &type,
			       &fmt, &cnt, &after, &data) == Success && data) {
		if (type == XA_CARDINAL && fmt == 32 && cnt >= (unsigned long) n) {
			unsigned long *v = (unsigned long *) data;

			for (int i = 0; i < n; i++) {
				out[i] = v[i];
			}
			ok = true;
		}
		XFree(data);
	}
	return ok;
}

static void zoomClient(Managed *m);
static void toggleToolbar(Managed *m);

/* ---- WM helpers ----------------------------------------------------- */

static Managed *
findFrame(::Window client)
{
	for (Managed *m : gFrames) {
		if (m->client == client) {
			return m;
		}
	}
	return nullptr;
}

static bool
findFrameByXid(::Window xid)
{
	for (Managed *m : gFrames) {
		if (m->frame->xid() == xid) {
			return true;
		}
	}
	return false;
}

static void
manageClient(const XMapRequestEvent &ev)
{
	if (findFrame(ev.window)) {
		/* a remap of an already-managed client */
		XMapWindow(dpy, ev.window);
		return;
	}
	if (ev.window == stripX || ev.window == deskX || ev.window == dockX) {
		/* Kestrel's own chrome (the menubar strip, the desktop
		 * surface): never managed. Mapped explicitly — under
		 * SubstructureRedirect the server did not map it, and a WM
		 * that drops its own map request leaves its chrome
		 * invisible. */
		XMapWindow(dpy, ev.window);
		return;
	}
	Managed *m = new Managed();

	m->client = ev.window;
	char *name = nullptr;

	XFetchName(dpy, ev.window, &name);
	if (name) {
		strncpy(m->title, name, sizeof(m->title) - 1);
		XFree(name);
	}
	/* the client's requested geometry (MapRequest only names it) */
	XWindowAttributes a;
	int cw0 = 320, ch0 = 200, fx = MARGIN, fy = BAR_H + MARGIN;

	if (XGetWindowAttributes(dpy, ev.window, &a)) {
		cw0 = a.width > 1 ? a.width : cw0;
		ch0 = a.height > 1 ? a.height : ch0;
		fx = a.x;
		fy = a.y;
	}
	/* S4.3: the client publishes what it wants through two CARDINAL
	 * properties (the toolkit's setPreferredContentSize /
	 * setToolbarHeight): the frame reserves the toolbar strip, and
	 * the zoom box grows the frame back to the preferred size. */
	{
		unsigned long v[2] = { 0, 0 };

		if (getCardinals(ev.window, "_ARGENTUM_PREFERRED_SIZE", v, 2)) {
			m->prefW = (unsigned int) v[0];
			m->prefH = (unsigned int) v[1];
		}
		if (getCardinals(ev.window, "_ARGENTUM_TOOLBAR_HEIGHT", v, 1)) {
			m->tbH = (unsigned int) v[0];
		}
		m->toolbar = m->tbH > 0;
	}
	/* the frame's inside = the client's size + the lip on the sides
	 * and bottom, the band on top */
	int fw = cw0 + 2 * FRAME_PX;
	int fh = BAND_H + ch0 + FRAME_PX;

	/* S5.2c: the work area — below the strip, clear of the dock's column,
	 * MARGIN to spare (the frame's 1px outline sits outside fw/fh). A
	 * window that does not fit is SHRUNK, the way a WM constrains a window
	 * to the visible frame: that is why the zoo's screen-sized request now
	 * comes back narrower than the screen. The client keeps the frame's
	 * lip, so its own size follows. */
	if (fw + 2 > workWidth()) {
		fw = workWidth() - 2;
		cw0 = fw - 2 * FRAME_PX;
		if (cw0 < MIN_FRAME_W) {
			cw0 = MIN_FRAME_W;
		}
	}
	if (fh + 2 > workHeight()) {
		fh = workHeight() - 2;
		ch0 = fh - BAND_H - FRAME_PX;
		if (ch0 < MIN_FRAME_H) {
			ch0 = MIN_FRAME_H;
		}
	}
	if (fy < workTop()) {
		fy = workTop();
	}
	if (fy + fh + 2 > workBottom()) {
		fy = workBottom() - fh - 2;
	}
	if (fx < workLeft()) {
		fx = workLeft();
	}
	if (fx + fw + 2 > workRight()) {
		fx = workRight() - fw - 2;
	}
	/* the frame window = an argentum window + a chrome content view */
	argentum::Window *frame = new argentum::Window();

	if (!frame->init(m->title[0] ? m->title : "Kestrel",
			 fx, fy, (unsigned) fw, (unsigned) fh)) {
		fprintf(stderr, "KESTREL: frame init failed for 0x%lx\n",
			(unsigned long) ev.window);
		delete frame;
		delete m;
		return;
	}
	/* S4.3: the frame's 1px outline is the frame WINDOW's X border —
	 * the sides and bottom of the frame's interior are occupied by
	 * the client, so the outline cannot be painted there. */
	XSetWindowBorderWidth(dpy, frame->xid(), 1);
	XSetWindowBorder(dpy, frame->xid(), outlineTone(false));
	FrameChrome *cv = new FrameChrome(m->title);

	/* S4.1c: the frame's close box asks the client to exit via
	 * the WM_DELETE_WINDOW protocol */
	cv->setOnActivate([m]() {
		focusClient(m);
	});
	cv->setOnZoom([m]() {
		zoomClient(m);
	});
	cv->setOnToolbar([m]() {
		toggleToolbar(m);
	});
	cv->setToolbarVisible(m->tbH > 0);
	cv->setOnClose([m]() {
		Atom wmProtocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
		Atom wmDelete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
		XEvent ev;

		memset(&ev, 0, sizeof(ev));
		ev.xclient.type = ClientMessage;
		ev.xclient.window = m->client;
		ev.xclient.message_type = wmProtocols;
		ev.xclient.format = 32;
		ev.xclient.data.l[0] = (long) wmDelete;
		ev.xclient.data.l[1] = CurrentTime;
		XSendEvent(dpy, m->client, False, NoEventMask, &ev);
		XSync(dpy, False);
		printf("KESTREL: close-request 0x%lx '%s'\n",
		       (unsigned long) m->client, m->title);
		fflush(stdout);
	});
	cv->setFrame({ {0, 0},
		       { fw / Application::shared().pxPerPt(),
			 fh / Application::shared().pxPerPt() } });
	frame->setContentView(cv);
	m->frame = frame;
	m->chrome = cv;
	m->fx = fx;
	m->fy = fy;
	m->fw = fw;
	m->fh = fh;
	gFrames.push_back(m);

	/* reparent below the band + map frame and client; watch the
	 * client's pointer events (S4.1b focus) via a passive button
	 * grab — ButtonPressMask is an AtMostOneClient event (the
	 * client itself selected it), so a plain XSelectInput would
	 * BadAccess; the grab + ReplayPointer is how a WM sees clicks
	 * without stealing them. SubstructureNotify on the frame sees
	 * the reparented client's unmap/destroy, and SubstructureRedirect
	 * sends the client's own Map/Configure requests to the WM (which
	 * is what makes them OBEYED on the frame's terms rather than
	 * executed behind the WM's back). */
	{
		/* ADD to the toolkit's mask — a plain XSelectInput would
		 * replace it (same connection) and the frame would stop
		 * selecting Exposure/input. */
		XWindowAttributes fa;

		XGetWindowAttributes(dpy, frame->xid(), &fa);
		XSelectInput(dpy, frame->xid(),
			     fa.your_event_mask | SubstructureNotifyMask |
			     SubstructureRedirectMask);
	}
	/* S4.3: the client sits in the client rect of the frame (inset by
	 * the lip on the sides and bottom) */
	int ccx, ccy, ccw, cch;

	clientRect(m, &ccx, &ccy, &ccw, &cch);
	XReparentWindow(dpy, m->client, frame->xid(), ccx, ccy);
	XGrabButton(dpy, Button1, AnyModifier, m->client, False,
		    ButtonPressMask, GrabModeSync, GrabModeAsync,
		    None, None);
	XMapWindow(dpy, frame->xid());	/* frame first: its band must get
					 * its own Expose before the client
					 * covers the client area */
	/* S4.3b: draw the chrome in the SAME server batch as the frame's
	 * map (requests are ordered), so the frame is never seen
	 * undecorated — the band lands before the client's pixels can. */
	frame->draw();
	XMapWindow(dpy, m->client);
	XSync(dpy, False);
	m->mapped = true;
	XRaiseWindow(dpy, stripX);
	XSync(dpy, False);
	if (!gActive) {
		focusClient(m);		/* first window gets focus */
	}
	printf("KESTREL: manage 0x%lx '%s' frame=0x%lx at %d,%d %dx%d"
	       " toolbar=%upx pref=%ux%u\n",
	       (unsigned long) m->client, m->title,
	       (unsigned long) frame->xid(), fx, fy, fw, fh,
	       m->tbH, m->prefW, m->prefH);
	fflush(stdout);
	dockRefresh();		/* S5.2c: this app's running dot */
	fflush(stdout);
}

/* ---- S4.1d window drag state ----------------------------------------
 * (declared here, above the manage/unmanage code that must end an
 * active drag on a dying client before freeing its frame) */
enum class DragMode { Move, Resize };

static bool gDragActive = false;
static Managed *gDragFrame = nullptr;
static DragMode gDragMode = DragMode::Move;
static int gDragOffX = 0;	/* grab point - frame origin (px) */
static int gDragOffY = 0;
static int gResizeEdges = 0;	/* EDGE_* the resize grab owns */
static int gGrabRootX = 0;	/* the resize grab point (root px) */
static int gGrabRootY = 0;
static int gGrabFx = 0, gGrabFy = 0;	/* the frame rect at grab time */
static int gGrabFw = 0, gGrabFh = 0;
static bool gDragMoved = false;	/* any actual motion happened */

static void endDrag(bool moved);

static void
unmanageClient(::Window client, bool destroyed)
{
	Managed *m = findFrame(client);

	if (!m) {
		return;
	}
	if (destroyed) {
		/* the client is gone: destroy its frame and drop it */
		printf("KESTREL: unmanage 0x%lx '%s'\n",
		       (unsigned long) m->client, m->title);
		fflush(stdout);
		if (gActive == m) {
			gActive = nullptr;
		}
		/* an active drag on the dying frame must end first or
		 * gDragFrame dangles into the delete below */
		if (gDragActive && gDragFrame == m) {
			endDrag(gDragMoved);
		}
		argentum::Window *frame = m->frame;
		FrameChrome *chrome = m->chrome;

		for (size_t i = 0; i < gFrames.size(); i++) {
			if (gFrames[i] == m) {
				gFrames.erase(gFrames.begin() + (long) i);
				break;
			}
		}
		delete frame;	/* XDestroyWindow; the client is gone */
		delete chrome;	/* the content view is not owned by the frame */
		delete m;
	} else if (m->mapped) {
		/* an unmap of the client: unmap the frame too (kept for
		 * the remap path) */
		XUnmapWindow(dpy, m->frame->xid());
		m->mapped = false;
	}
	XSync(dpy, False);
}

/* ---- S4.1b focus ----------------------------------------------------- */

/* Make `m` the active client: repaint both bands, raise, focus the
 * input, publish _NET_ACTIVE_WINDOW. The frame's X border carries the
 * 1px outline, so it flips with the focus too (S4.3). */
static void
focusClient(Managed *m)
{
	if (!m || m == gActive) {
		return;
	}
	Managed *prev = gActive;

	gActive = m;
	if (prev && prev->chrome) {
		prev->chrome->setActive(false);
		XSetWindowBorder(dpy, prev->frame->xid(), outlineTone(false));
	}
	if (m->chrome) {
		m->chrome->setActive(true);
		XSetWindowBorder(dpy, m->frame->xid(), outlineTone(true));
	}
	XRaiseWindow(dpy, m->frame->xid());
	XSetInputFocus(dpy, m->client, RevertToParent, CurrentTime);
	setActiveProperty(m->client);
	XSync(dpy, False);
	printf("KESTREL: focus 0x%lx '%s'\n",
	       (unsigned long) m->client, m->title);
	fflush(stdout);
	stripRefresh();		/* S4.2a: the bar follows focus */
}

/* ---- S4.1d window drag (title-band move) ----------------------------- */

/* Drag = the frame follows the pointer LIVE per motion; the server's
 * CopyWindow carries the reparented client's pixels on the move, so
 * the drag costs no client redraw (measured: one redraw across 200
 * moves). The drag END is only a grab release — the window is
 * already where the pointer is, so a premature or delayed end is
 * visually harmless. (v2 used an XOR outline + teleport-on-drop
 * because a per-motion move was believed to force a full client
 * re-render on every Expose; on the current Xfb that is not the
 * case, and the outline made phantom releases catastrophic.) */

static long long
nowMs()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* The resize grips: the frame's lip on the left/right/bottom, the
 * band's top edge, and the 1px X border around all of it (a press on
 * the border arrives with x<0 / y<0 / x>=fw / y>=fh). Returns the
 * EDGE_* bitmask the grab owns, 0 = not a grip. */
static int
edgeHit(Managed *m, int lx, int ly)
{
	int e = 0;

	if (lx < FRAME_PX) {
		e |= EDGE_LEFT;
	}
	if (lx >= m->fw - FRAME_PX) {
		e |= EDGE_RIGHT;
	}
	if (ly < GRIP_PX) {
		e |= EDGE_TOP;
	}
	if (ly >= m->fh - FRAME_PX) {
		e |= EDGE_BOTTOM;
	}
	return e;
}

/* Is (lx,ly) in the band, off the controls? (frame-local px) */
static bool
isBandGrabArea(Managed *m, int lx, int ly)
{
	if (ly < 0 || ly >= BAND_H || lx < 0 || lx >= m->fw) {
		return false;
	}
	return bandControlAt(m->fw, lx, ly, m->tbH > 0) == BAND_NONE;
}

static void
beginDrag(Managed *m, int rootX, int rootY)
{
	/* a phantom re-press mid-drag (flaky button state): keep the
	 * drag state, just re-anchor + re-grab so the continuing
	 * motion continues the same drag */
	if (gDragActive && gDragFrame == m && gDragMode == DragMode::Move) {
		gDragOffX = rootX - m->fx;
		gDragOffY = rootY - m->fy;
		XGrabPointer(dpy, m->frame->xid(), False,
			     PointerMotionMask | ButtonReleaseMask,
			     GrabModeAsync, GrabModeAsync, None, None,
			     CurrentTime);
		XSync(dpy, False);
		return;
	}
	gDragActive = true;
	gDragFrame = m;
	gDragMode = DragMode::Move;
	gDragOffX = rootX - m->fx;
	gDragOffY = rootY - m->fy;
	gDragMoved = false;
	XGrabPointer(dpy, m->frame->xid(), False,
		     PointerMotionMask | ButtonReleaseMask,
		     GrabModeAsync, GrabModeAsync, None, None,
		     CurrentTime);
	XRaiseWindow(dpy, m->frame->xid());
	XSync(dpy, False);
}

/* S4.3: start (or re-anchor) a resize drag on the grabbed edges — the
 * same session as a move, so the grab, the quiet-end rules and the
 * abuse guards are shared. */
static void
beginResize(Managed *m, int edges, int rootX, int rootY)
{
	gDragActive = true;
	gDragFrame = m;
	gDragMode = DragMode::Resize;
	gResizeEdges = edges;
	gGrabRootX = rootX;
	gGrabRootY = rootY;
	gGrabFx = m->fx;
	gGrabFy = m->fy;
	gGrabFw = m->fw;
	gGrabFh = m->fh;
	gDragMoved = false;
	XGrabPointer(dpy, m->frame->xid(), False,
		     PointerMotionMask | ButtonReleaseMask,
		     GrabModeAsync, GrabModeAsync, None, None,
		     CurrentTime);
	XRaiseWindow(dpy, m->frame->xid());
	XSync(dpy, False);
}

static void
dragTo(int rootX, int rootY)
{
	if (!gDragActive || !gDragFrame) {
		return;
	}
	Managed *m = gDragFrame;

	if (gDragMode == DragMode::Resize) {
		/* apply the delta to the edges the grab owns, against the
		 * frame rect at grab time; the frame and the client move
		 * and resize together (applyFrameGeometry) */
		int dx = rootX - gGrabRootX;
		int dy = rootY - gGrabRootY;
		int nx = gGrabFx, ny = gGrabFy;
		int nw = gGrabFw, nh = gGrabFh;

		if (gResizeEdges & EDGE_LEFT) {
			nx = gGrabFx + dx;
			nw = gGrabFw - dx;
		}
		if (gResizeEdges & EDGE_RIGHT) {
			nw = gGrabFw + dx;
		}
		if (gResizeEdges & EDGE_TOP) {
			ny = gGrabFy + dy;
			nh = gGrabFh - dy;
		}
		if (gResizeEdges & EDGE_BOTTOM) {
			nh = gGrabFh + dy;
		}
		if (nw < MIN_FRAME_W) {
			nw = MIN_FRAME_W;
			if (gResizeEdges & EDGE_LEFT) {
				nx = gGrabFx + gGrabFw - MIN_FRAME_W;
			}
		}
		if (nh < MIN_FRAME_H) {
			nh = MIN_FRAME_H;
			if (gResizeEdges & EDGE_TOP) {
				ny = gGrabFy + gGrabFh - MIN_FRAME_H;
			}
		}
		if (ny < BAR_H) {
			/* the band stays out of the menubar strip: clamp the
			 * top edge and re-derive the height from it */
			ny = BAR_H;
			if (gResizeEdges & EDGE_TOP) {
				nh = gGrabFy + gGrabFh - ny;
			}
		}
		if (nx == m->fx && ny == m->fy && nw == m->fw &&
		    nh == m->fh) {
			return;		/* no motion yet */
		}
		applyFrameGeometry(m, nx, ny, nw, nh);
		gDragMoved = true;
		return;
	}
	int nx = rootX - gDragOffX;
	int ny = rootY - gDragOffY;

	if (ny < BAR_H) {
		ny = BAR_H;		/* keep it below the strip */
	}
	if (nx == m->fx && ny == m->fy) {
		return;			/* no motion yet */
	}
	/* MOVE LIVE: the frame follows the pointer per motion. The
	 * server's CopyWindow carries the frame AND the reparented
	 * client's pixels on a move (measured: a 200-move drag causes
	 * exactly ONE client redraw), so this is cheap and the window
	 * always renders at the tracked position — no outline, no
	 * teleport-on-drop. (v2's XOR outline existed because a move
	 * was believed to discard the client's pixels; on the current
	 * Xfb that is not the case.) */
	XMoveWindow(dpy, m->frame->xid(), nx, ny);
	m->fx = nx;
	m->fy = ny;
	gDragMoved = true;
	XFlush(dpy);
}

static void
endDrag(bool moved)
{
	if (!gDragActive) {
		return;
	}
	Managed *m = gDragFrame;

	if (m) {
		if (gDragMode == DragMode::Resize) {
			printf("KESTREL: resize 0x%lx '%s' to %dx%d%s\n",
			       (unsigned long) m->client, m->title,
			       m->fw, m->fh, moved ? "" : " (no motion)");
		} else {
			printf("KESTREL: move 0x%lx '%s' to %d,%d%s\n",
			       (unsigned long) m->client, m->title,
			       m->fx, m->fy, moved ? "" : " (no motion)");
		}
		fflush(stdout);
	}
	XUngrabPointer(dpy, CurrentTime);
	XSync(dpy, False);
	gDragActive = false;
	gDragFrame = nullptr;
	gDragMode = DragMode::Move;
	gResizeEdges = 0;
}

/* ---- S4.3 zoom + toolbar -------------------------------------------- */

/* The zoom box: grow the frame to the size the client published
 * (_ARGENTUM_PREFERRED_SIZE = the client's own window size, so a
 * window carrying a toolbar strip declares it with the strip). A
 * second press is a no-op — the frame is already at it. */
static void
zoomClient(Managed *m)
{
	if (!m->prefW || !m->prefH) {
		return;		/* nothing declared: the box does nothing */
	}
	int fw = (int) m->prefW + 2 * FRAME_PX;
	int fh = BAND_H + (int) m->prefH + FRAME_PX;

	if (!m->toolbar && m->tbH > 0) {
		fh -= (int) m->tbH;	/* the strip is hidden */
	}
	/* S5.2c: the zoom box respects the work area (the dock's column) */
	{
		int fx = m->fx, fy = m->fy;

		if (fw + 2 > workWidth()) {
			fw = workWidth() - 2;
		}
		if (fh + 2 > workHeight()) {
			fh = workHeight() - 2;
		}
		if (fx + fw + 2 > workRight()) {
			fx = workRight() - fw - 2;
		}
		if (fx < workLeft()) {
			fx = workLeft();
		}
		if (fy + fh + 2 > workBottom()) {
			fy = workBottom() - fh - 2;
		}
		if (fy < workTop()) {
			fy = workTop();
		}
		applyFrameGeometry(m, fx, fy, fw, fh);
	}
	printf("KESTREL: zoom 0x%lx '%s' to %dx%d\n",
	       (unsigned long) m->client, m->title, m->fw, m->fh);
	fflush(stdout);
}

/* Tell the client its new toolbar state (S4.3: the strip's pixels are
 * the client's, so the box can only say so — `_ARGENTUM_TOOLBAR`, the
 * same shape as WM_DELETE_WINDOW, data.l[1] = the state). */
static void
sendToolbarState(Managed *m)
{
	Atom wmProtocols = XInternAtom(dpy, "WM_PROTOCOLS", False);
	Atom wmToolbar = XInternAtom(dpy, "_ARGENTUM_TOOLBAR", False);
	XEvent ev;

	memset(&ev, 0, sizeof(ev));
	ev.xclient.type = ClientMessage;
	ev.xclient.window = m->client;
	ev.xclient.message_type = wmProtocols;
	ev.xclient.format = 32;
	ev.xclient.data.l[0] = (long) wmToolbar;
	ev.xclient.data.l[1] = m->toolbar ? 1L : 0L;
	XSendEvent(dpy, m->client, False, NoEventMask, &ev);
	XSync(dpy, False);
}

/* The show/hide-toolbar box. The strip is the CLIENT's content, so the
 * box does both halves of the toggle: it tells the client the new state
 * (its strip appears or goes), and it reserves or drops the strip's
 * height in the frame — the client's window is resized around the
 * strip. The message goes FIRST: the resize that follows carries the
 * Expose that repaints the client, so it repaints once, already knowing
 * the state (a client with no hook keeps its strip and the geometry
 * still matches the old behaviour). */
static void
toggleToolbar(Managed *m)
{
	if (!m->tbH) {
		return;
	}
	m->toolbar = !m->toolbar;
	sendToolbarState(m);
	int fh = m->fh + (m->toolbar ? (int) m->tbH : -(int) m->tbH);

	applyFrameGeometry(m, m->fx, m->fy, m->fw, fh);
	printf("KESTREL: toolbar 0x%lx '%s' %s (%upx)\n",
	       (unsigned long) m->client, m->title,
	       m->toolbar ? "on" : "off", m->tbH);
	fflush(stdout);
}

/* ---- drag end -------------------------------------------------------
 *
 * The drag follows the pointer LIVE (above); the end is only a grab
 * release - the window is already at its final position, so the end
 * costs nothing.
 *
 * The end is the *release*, immediately.  It used to be deferred by up
 * to a second (button-up plus a quiet window, longer if the release hit
 * mid fast motion), distrusting a release that arrived during motion:
 * that was written for QEMU's ps2 sync slips and for the xHCI
 * event-ring stall, and both are gone (the shipped session is USB HID
 * with i8042 off, and the event-ring stall was fixed in f42fab1).  The
 * deferral was itself the bug the user sees - "it keeps dragging after
 * I let go": motion while the button was up both moved the window and
 * re-armed the clock, so a flick followed by any pointer movement kept
 * the window on the pointer for as long as the pointer kept moving.
 *
 * A release lost in the input path is still caught, but from the
 * stream itself rather than from a timer: a MotionNotify carries the
 * button state at its own time, so motion with the drag button up ends
 * the drag instead of following the pointer (see MotionNotify).  The
 * mouse device also keeps queue room for edge records now
 * (MOUSE_EDGE_RESERVE, mousedev.h), so a release cannot be dropped
 * there either. */
#define DRAG_BUTTON_MASK						\
	(Button1Mask | Button2Mask | Button3Mask | Button4Mask | Button5Mask)


/* ---- the idle + event hooks: WM housekeeping ------------------------ */

/* Some events (DestroyNotify for a client killed by its connection
 * closing) never arrive on this server; probe the managed clients
 * cheaply and reap the dead. Runs on the idle beat (a ~250ms poll
 * when no events pend) — deliberately NOT on the per-event path: the
 * XGetWindowAttributes probe is a synchronous round-trip per managed
 * client and would throttle every motion during a drag. */
static bool
reapDeadClients()
{
	for (size_t i = 0; i < gFrames.size();) {
		Managed *m = gFrames[i];
		XWindowAttributes a;

		if (XGetWindowAttributes(dpy, m->client, &a)) {
			i++;
			continue;
		}
		printf("KESTREL: unmanage 0x%lx '%s'\n",
		       (unsigned long) m->client, m->title);
		fflush(stdout);
		dockRefresh();	/* S5.2c: the running dot goes away */
		if (gActive == m) {
			gActive = nullptr;
		}
		/* an active drag on the dying frame must end first or
		 * gDragFrame dangles into the delete below */
		if (gDragActive && gDragFrame == m) {
			endDrag(gDragMoved);
		}
		argentum::Window *frame = m->frame;
		FrameChrome *chrome = m->chrome;

		dropMenusForClient(m->client);	/* S4.2a */
		gFrames.erase(gFrames.begin() + (long) i);
		delete frame;
		delete chrome;	/* the content view is not owned by the frame */
		delete m;
	}
	return false;		/* the caller keeps idling */
}

/* The idle beat (the app loop polls ~250ms with no events, then calls
 * this): WM housekeeping that must not run on the per-event path. */
static bool
idleBeat()
{
	reapDeadClients();
	clockUpdate(false);	/* S5.2b: the bar's clock */
	/* S5.2c: reap the dock's launches (WNOHANG: never block here) */
	while (waitpid(-1, nullptr, WNOHANG) > 0) {
		;
	}
	return false;
}

static bool
kestrelHook(void *xevent)
{
	XEvent *ev = (XEvent *) xevent;

	/* reapDeadClients does a synchronous XGetWindowAttributes
	 * round-trip per managed client — on the per-event path it would
	 * throttle every motion during a drag to server round-trip
	 * latency. Reaping runs on the idle beat instead (~250ms, when
	 * no events pend), which is plenty for frame cleanup. */

	/* S5.2a: the desktop itself changed size (an fb0 mode-set). The
	 * wallpaper is sized from the screen, so rebuild it; the menubar strip
	 * spans the screen, so re-span and repaint it. Handled here, before
	 * the dispatch, deliberately NOT as a `case` with a `break`: GCC's
	 * switch lowering gave ConfigureNotify a jump-table entry that landed
	 * on a ud2 trampoline (an early return avoids the table entirely) —
	 * see the S5.2a record. Not consumed: nothing else acts on a root
	 * configure. */
	if (ev->type == ConfigureNotify && ev->xconfigure.window == root) {
		int nw = ev->xconfigure.width;
		int nh = ev->xconfigure.height;

		if (nw != screenW || nh != screenH) {
			screenW = nw;
			screenH = nh;
			wallpaperInstall(nw, nh);
			if (stripX) {
				XResizeWindow(dpy, stripX, (unsigned) nw,
					      (unsigned) BAR_H);
				stripRefresh();
			}
		}
		return false;
	}

	switch (ev->type) {
	case ButtonPress:
		/* a press in a CLIENT arrives through the passive grab
		 * (GrabModeSync): focus it, then replay the press so the
		 * client's own UI sees the click. */
		for (Managed *m : gFrames) {
			if (ev->xbutton.window == m->client) {
				menuPopUpDismiss();	/* S4.2b */
				focusClient(m);
				XAllowEvents(dpy, ReplayPointer,
					     CurrentTime);
				XSync(dpy, False);
				return true;
			}
		}
		/* S5.2c: a press on the dock — a tile activates (raise the app if
		 * it is running, launch it if it is not); anywhere else on the
		 * dock closes an open menu, like the strip. Consumed either way:
		 * the dock is the WM's own. */
		if (dockX && ev->xbutton.window == dockX) {
			int i = dockTileAt(ev->xbutton.x, ev->xbutton.y);

			if (i >= 0) {
				dockActivate(i);
			} else {
				menuPopUpDismiss();
			}
			return true;
		}
		/* S4.2b: a press on the menubar strip — on a title it drops
		 * that title's menu, anywhere else it closes an open one.
		 * Consumed either way: the strip is the WM's own. */
		if (ev->xbutton.window == stripX) {
			int x = ev->xbutton.x;

			/* S5.2b: the system mark opens Kestrel's own menu */
			if (x < SYS_ZONE_W) {
				popUpSystemMenu();
				return true;
			}
			Menu *root = gActive ? menuForClient(gActive->client)
					     : nullptr;
			int xs[32], ws[32], idx[32];
			int n = stripMenuLayout(root,
						gActive ? gActive->title
							: "Kestrel",
						xs, ws, idx, 32,
						clockZoneLeft());

			for (int i = 0; i < n; i++) {
				if (x >= xs[i] - 8 && x < xs[i] + ws[i] + 8) {
					popUpClientMenu(gActive, idx[i], xs[i]);
					return true;
				}
			}
			menuPopUpDismiss();
			return true;
		}
		/* S4.3: a press on a FRAME's edge/corner grip resizes it
		 * (the lip, the band's top edge, or the 1px X border);
		 * a press on a band control is left to the toolkit (the
		 * FrameChrome runs the close/zoom/toolbar callbacks); any
		 * other band press starts a MOVE. */
		for (Managed *m : gFrames) {
			if (ev->xbutton.window != m->frame->xid()) {
				continue;
			}
			int lx = ev->xbutton.x;
			int ly = ev->xbutton.y;
			int edges;

			/* a band control belongs to the toolkit (the
			 * FrameChrome runs the close/zoom/toolbar
			 * callbacks): check it BEFORE the grips, whose
			 * top band overlaps the controls' first row */
			menuPopUpDismiss();	/* S4.2b: any frame press */
			if (ly >= 0 && ly < BAND_H && lx >= 0 && lx < m->fw &&
			    bandControlAt(m->fw, lx, ly, m->tbH > 0) !=
				    BAND_NONE) {
				return false;
			}
			edges = edgeHit(m, lx, ly);
			if (edges) {
				focusClient(m);
				beginResize(m, edges, ev->xbutton.x_root,
					    ev->xbutton.y_root);
				return true;
			}
			if (!isBandGrabArea(m, lx, ly)) {
				return false;	/* a control: the toolkit's */
			}
			focusClient(m);
			beginDrag(m, ev->xbutton.x_root,
				  ev->xbutton.y_root);
			return true;
		}
		return false;
	case MotionNotify:
		if (gDragActive) {
			/* The event carries the button state at its own
			 * time, so it is authoritative: a motion with the
			 * drag button up means the release was already
			 * delivered - or was lost in the input path - and
			 * the drag is over.  Ending here (instead of
			 * following the pointer) is what keeps a lost
			 * release from leaving the window stuck on the
			 * pointer. */
			if (!(ev->xmotion.state & DRAG_BUTTON_MASK)) {
				endDrag(gDragMoved);
				return true;
			}
			dragTo(ev->xmotion.x_root, ev->xmotion.y_root);
			return true;
		}
		return false;
	case ButtonRelease:
		if (gDragActive) {
			/* the release is the end of the drag: immediate,
			 * never deferred (a fast release used to keep the
			 * window on the pointer for up to a second) */
			endDrag(gDragMoved);
			return true;
		}
		return false;
	case MapRequest:
		manageClient(ev->xmaprequest);
		return true;
	case ConfigureRequest: {
		/* a client asks for a geometry change: the frame redirects
		 * its children, so the request lands here instead of being
		 * executed. The requested size is the CLIENT window's, so
		 * it becomes a frame resize (client size + the frame's
		 * lip) and the client is placed by the client rect —
		 * applying it to the client directly would drift it out of
		 * its frame. Placement (width/height only) is the WM's job:
		 * v1 clients never move themselves. */
		Managed *m = findFrame(ev->xconfigurerequest.window);

		if (!m) {
			return true;
		}
		unsigned int mask =
			(unsigned int) ev->xconfigurerequest.value_mask;

		if (mask & (CWWidth | CWHeight)) {
			/* sizes come from another client: clamp before the
			 * frame arithmetic */
			int rw = (int) ev->xconfigurerequest.width;
			int rh = (int) ev->xconfigurerequest.height;
			XWindowAttributes ca;

			if (rw > 16384) {
				rw = 16384;
			}
			if (rh > 16384) {
				rh = 16384;
			}
			if (rw < 1) {
				rw = 1;
			}
			if (rh < 1) {
				rh = 1;
			}
			if (!XGetWindowAttributes(dpy, m->client, &ca)) {
				return true;
			}
			int cw = (mask & CWWidth) ? rw : ca.width;
			int ch = (mask & CWHeight) ? rh : ca.height;

			/* a request that already matches the client is the
			 * echo of our own applyFrameGeometry: applying it
			 * again would re-enter forever */
			if (cw == ca.width && ch == ca.height) {
				return true;
			}
			applyFrameGeometry(m, m->fx, m->fy,
					   cw + 2 * FRAME_PX,
					   BAND_H + ch + FRAME_PX);
		}
		return true;
	}
	case UnmapNotify:
		if (ev->xunmap.event == root ||
		    findFrameByXid(ev->xunmap.event)) {
			unmanageClient(ev->xunmap.window, false);
		}
		return true;
	case DestroyNotify:
		if (ev->xdestroywindow.event == root ||
		    findFrameByXid(ev->xdestroywindow.event)) {
			unmanageClient(ev->xdestroywindow.window, true);
		}
		return true;
	default:
		return false;	/* the toolkit dispatches normally */
	}
}

static int
xerr(Display *, XErrorEvent *e)
{
	if (e) {
		fprintf(stderr, "KESTREL: X error op=%d code=%d res=%lu\n",
			e->request_code, e->error_code,
			(unsigned long) e->resourceid);
		fflush(stderr);
	}
	xerrCount++;
	return 0;
}

/* ---- the menubar strip's content (chrome only until S4.2) ----------- */

/* S5.2c: the dock's content — a column of vector tiles (a rounded tile, a
 * monogram, a running dot), drawn with the theme's parameters. The hover
 * comes from the toolkit's pointer tracking, which only fires for views the
 * pointer is over. */
class DockView : public View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		int side = (int) (gDockIcon * ppt + 0.5);
		TextMetrics m = textMetrics(t.fontFamily(), t.fontSizePt(), "Zg");
		double box = m.ascentPt + m.descentPt + 2.0 / ppt;

		if (w <= 0 || h <= 0 || side <= 0)
			return;
		/* the dock's slab: a darker chrome tone, so the tiles read. It is
		 * edge chrome flush with the menubar and the screen edge, so it
		 * has no corners of its own to round (the tiles keep theirs). */
		g.fillRect(0, 0, (unsigned) w, (unsigned) h,
			   mixColor(t.chromeBottom(), 0x000000, 46));
		for (int i = 0; i < kPinCount; i++) {
			int ty = (int) (dockTileY(i) * ppt + 0.5);
			bool run = dockPinRunning(i);

			if (ty + side > h)
				break;
			/* the tile: page tone, or an accent tint when hovered */
			if (i == hover_) {
				g.fillRoundedRect(2, ty, (unsigned) (w - 4),
						  (unsigned) side, 8,
						  mixColor(t.page(), t.accent(), 90));
			} else {
				g.fillRoundedRect(2, ty, (unsigned) (w - 4),
						  (unsigned) side, 8, t.page());
			}
			/* the monogram, centred in the tile */
			{
				const char *mono = kPins[i].monogram;
				TextMetrics mm = textMetrics(t.fontFamily(),
							     t.fontSizePt(), mono);
				int mw = (int) (mm.widthPt * ppt + 0.5);
				int mx = (w - mw) / 2;
				int my = ty + (int) ((side - box * ppt) / 2.0);

				g.drawText(t.fontFamily(), t.fontSizePt(), mx,
					   my, mono, t.text());
			}
			/* the running dot under the tile */
			if (run) {
				int r = 4;
				int cx = w / 2;
				int cy = ty + side - r;

				g.fillRoundedRect(cx - r, cy - r, 2 * r, 2 * r,
						  r, t.accent());
			}
		}
	}

	void mouseMoved(const MouseEvent &e) override
	{
		int i = dockTileAt((int) e.x, (int) e.y);

		if (i != hover_) {
			hover_ = i;
			setNeedsDisplay();
		}
	}

	void mouseExited(const MouseEvent &e) override
	{
		(void) e;
		if (hover_ != -1) {
			hover_ = -1;
			setNeedsDisplay();
		}
	}

private:
	int hover_ = -1;
};

static void
dockRefresh()
{
	if (gDockView && gDock) {
		/* drawing: Window::draw() composites and flushes only the
		 * pending damage rect, so a content change must report itself
		 * (the strip learned this in S4.2b) */
		gDockView->setNeedsDisplay();
		gDock->draw();
	}
}

class StripView : public View {
public:
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();
		int w = (int) (f.size.w * ppt + 0.5);
		int h = (int) (f.size.h * ppt + 0.5);
		Theme::Params p = t.state(ControlState::Idle);

		g.fillRoundedGradient(0, 0, (unsigned) w, (unsigned) h, 0,
				      p.fillTop, p.fillBottom);
		g.fillRect(0, h - 1, (unsigned) w, 1, t.chromeOutline());
		{
			TextMetrics m = textMetrics(t.fontFamily(),
						    t.fontSizePt(), "Ag");
			double box = m.ascentPt + m.descentPt + 2.0 / ppt;
			int ty = (int) ((h - box * ppt) / 2.0);
			int xs[32], ws[32], idx[32];
			int n;

			/* S4.2a: the focused app's menus — the menubar's
			 * items are the bar titles, laid out by the same
			 * function the hit-test uses (S4.2b). */
			/* S5.2b: the system mark — the mockup's "system icon" as
			 * a vector tile (no asset): the theme's accent, centred in
			 * the bar. A press in its zone opens Kestrel's own menu. */
			g.fillRoundedRect(6, (h - SYS_ICON_W) / 2, SYS_ICON_W,
					  SYS_ICON_W, 4, t.accent());
			if (title_[0]) {
				g.drawText(t.fontFamily(), t.fontSizePt(),
					   SYS_ZONE_W, ty, title_, t.text());
			}
			n = stripMenuLayout(menu_, title_, xs, ws, idx, 32,
					    clockZoneLeft());
			for (int i = 0; i < n; i++) {
				g.drawText(t.fontFamily(), t.fontSizePt(),
					   xs[i], ty,
					   menu_->itemAt(idx[i])->title(),
					   t.text());
			}
			/* S5.2b: the clock, in its reserved zone at the right.
			 * Left-aligned inside the reserved box so it does not
			 * jitter when the text's own width changes. */
			if (clock_[0]) {
				g.drawText(t.fontFamily(), t.fontSizePt(),
					   screenW - gClockW - CLOCK_INSET, ty,
					   clock_, t.text());
			}
		}
	}

	void setTitle(const char *utf8)
	{
		strncpy(title_, utf8, sizeof(title_) - 1);
	}

	/* S4.2a: the focused client's parsed menubar (borrowed) */
	void setMenu(const Menu *menu)
	{
		menu_ = menu;
	}

	/* S5.2b: the date/time, drawn at the right in its reserved zone */
	void setClock(const char *utf8)
	{
		strncpy(clock_, utf8, sizeof(clock_) - 1);
		clock_[sizeof(clock_) - 1] = 0;
	}

private:
	char title_[64] = { 0 };
	char clock_[64] = { 0 };
	const Menu *menu_ = nullptr;
};

/* S4.2b: where the strip's menu titles sit, in window px — ONE layout
 * used by both the paint and the hit-test, so they cannot drift
 * (bandControlAt's rule). xs/ws are the title's extent, idx its index in
 * the menu (separators are skipped and take no room). Returns how many
 * titles were laid out (at most max). */
static int
stripMenuLayout(const Menu *menu, const char *title, int *xs, int *ws,
		int *idx, int max, int limit)
{
	Application &app = Application::shared();
	Theme &t = app.theme();
	double ppt = app.pxPerPt();
	int x = SYS_ZONE_W;	/* S5.2b: past the system mark */
	int n = 0;

	if (title && title[0]) {
		x += (int) (textMetrics(t.fontFamily(), t.fontSizePt(), title)
			    .widthPt * ppt + 0.5) + 16;
	}
	for (int i = 0; menu && i < menu->itemCount(); i++) {
		MenuItem *item = menu->itemAt(i);
		const char *s = item->title();
		int w;

		if (item->kind() == MenuItem::Kind::Separator || !s[0]) {
			continue;
		}
		w = (int) (textMetrics(t.fontFamily(), t.fontSizePt(), s)
			   .widthPt * ppt + 0.5);
		/* S5.2b: the clock's zone is reserved — a title that would
		 * reach into it is dropped (and, since the hit-test runs this
		 * same layout, it is not clickable either). */
		if (x + w > limit)
			break;
		if (n < max) {
			xs[n] = x;
			ws[n] = w;
			idx[n] = i;
			n++;
		}
		x += w + 18;
	}
	return n;
}

static StripView *gStrip = nullptr;	/* the strip's content view */
static argentum::Window *gBar = nullptr;	/* its window (in main) */

/* The clock's reserved width, measured from a FIXED reference time so the
 * menus beside it never shift when the text changes ("9:05" -> "10:05").
 * The reference has two digits everywhere, so the reserved box is the
 * widest the format can be for a normal date. */
static int
clockTextWidth(const char *fmt)
{
	Application &app = Application::shared();
	Theme &t = app.theme();
	struct tm ref = {};
	char buf[96];

	ref.tm_year = 106;	/* 2006-11-22 22:22 */
	ref.tm_mon = 10;
	ref.tm_mday = 22;
	ref.tm_hour = 22;
	ref.tm_min = 22;
	ref.tm_wday = 3;
	if (!strftime(buf, sizeof buf, fmt, &ref))
		return 0;
	return (int) (textMetrics(t.fontFamily(), t.fontSizePt(), buf)
			      .widthPt * app.pxPerPt() + 0.5);
}

/* S5.2b: tick the clock. Runs on the idle beat, redraws only when the text
 * actually changes (once a minute), and logs the text so a run can see it
 * tick — the same trick the wallpaper tones use. */
static void
clockUpdate(bool force)
{
	struct tm tm;
	time_t now = time(nullptr);
	char buf[96];

	if (!gClockFmt[0])
		return;
	if (!localtime_r(&now, &tm))
		return;
	if (!strftime(buf, sizeof buf, gClockFmt, &tm))
		return;
	if (!force && strcmp(buf, gClockText) == 0)
		return;
	strncpy(gClockText, buf, sizeof(gClockText) - 1);
	gClockText[sizeof(gClockText) - 1] = 0;
	if (gStrip) {
		gStrip->setClock(gClockText);
		stripRefresh();
	}
	printf("KESTREL: clock \"%s\"\n", gClockText);
	fflush(stdout);
}

/* S4.2a: what the strip shows — the focused client's name and its
 * published menus, else Kestrel's own. Called when focus or the menus
 * change. */
static void
stripRefresh()
{
	if (!gStrip) {
		return;
	}
	if (gActive) {
		gStrip->setTitle(gActive->title);
		gStrip->setMenu(menuForClient(gActive->client));
	} else {
		gStrip->setTitle("Kestrel");
		gStrip->setMenu(nullptr);
	}
	if (gBar) {
		/* Mark the strip damaged before drawing: Window::draw()
		 * composites and flushes only the pending damage rect,
		 * so a content change that does not report itself would
		 * be painted into the backing and never reach the
		 * screen. The frames' chrome reports its own rect the
		 * same way (View::setNeedsDisplay). */
		gStrip->setNeedsDisplay();
		gBar->draw();
	}
}

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		fprintf(stderr, "KESTREL: no display\n");
		return 1;
	}
	/* the WM works on the toolkit's connection so run()'s event hook
	 * sees the redirect events (MapRequest arrives on the loop's
	 * display, not on a private one) */
	dpy = (Display *) app.display();

	if (!dpy) {
		fprintf(stderr, "KESTREL: no display\n");
		return 1;
	}
	scr = DefaultScreen(dpy);
	root = DefaultRootWindow(dpy);
	screenW = DisplayWidth(dpy, scr);
	screenH = DisplayHeight(dpy, scr);

	/* become the WM: redirect root substructure; BadAccess = another
	 * WM already runs. StructureNotify on the root is for S5.2a: an fb0
	 * mode-set resizes the desktop, and the wallpaper is sized to it. */
	XSetErrorHandler(xerr);
	XSelectInput(dpy, root,
		     SubstructureRedirectMask | SubstructureNotifyMask |
		     StructureNotifyMask);
	XSync(dpy, False);
	if (xerrCount) {
		fprintf(stderr, "KESTREL: another WM owns the display\n");
		return 1;
	}
	/* keep the handler installed: a stray error must log, not exit */
	xerrCount = 0;

	/* S5.2a: paint the desktop before any client can map */
	wallpaperInstall(screenW, screenH);

	/* S5.2c: the dock's settings (system.workspace.conf), read BEFORE
	 * anything is placed — the dock owns a column of the work area, so
	 * its geometry has to be known before the first frame is placed. */
	{
		char pos[16] = "right";
		char isz[16] = "48";

		app.configString("system.workspace", "dock.position", "right",
				 pos, sizeof(pos));
		app.configString("system.workspace", "dock.icon-size", "48",
				 isz, sizeof(isz));
		gDockLeft = (strcmp(pos, "left") == 0);
		gDockIcon = atoi(isz);
		if (gDockIcon < 24 || gDockIcon > 128) {
			gDockIcon = 48;	/* a usable tile whatever the file says */
		}
	}
	{
		int dw = dockW();
		int dx0 = dockLeft();
		/* The dock spans the screen from under the menubar to the bottom
		 * edge: it is edge chrome, so the work area's MARGIN insets
		 * *windows* from it, not the dock from the screen. */
		int dy0 = BAR_H;
		int dh = screenH - BAR_H;

		gDock = new argentum::Window();
		if (!gDock->init("Argentum Dock", dx0, dy0, (unsigned) dw,
				 (unsigned) dh)) {
			fprintf(stderr, "KESTREL: dock init failed\n");
			delete gDock;
			gDock = nullptr;
		} else {
			DockView *dv = new DockView();

			dv->setFrame({ {0, 0},
				       { dw / app.pxPerPt(),
					 dh / app.pxPerPt() } });
			gDock->setContentView(dv);
			gDockView = dv;
			dockX = gDock->xid();
			/* mapped directly, like the strip and the desktop */
			XMapWindow(dpy, dockX);
			dockRefresh();
			printf("KESTREL: dock %s %dx%d at %d,%d tiles=%d "
			       "icon=%d\n", gDockLeft ? "left" : "right", dw, dh,
			       dx0, dy0, kPinCount, gDockIcon);
			fflush(stdout);
		}
	}

	/* the menubar strip: a full-width argentum window at the top */
	argentum::Window bar;

	if (!bar.init("Kestrel", 0, 0, (unsigned) screenW, (unsigned) BAR_H)) {
		fprintf(stderr, "KESTREL: strip init failed\n");
		return 1;
	}
	StripView *stripView = new StripView();

	stripView->setFrame(
		{ {0, 0},
		  { screenW / app.pxPerPt(), BAR_H / app.pxPerPt() } });
	stripView->setTitle("Kestrel");
	bar.setContentView(stripView);
	gStrip = stripView;
	/* S5.2b: the bar's clock — its format comes from the desktop config,
	 * its zone is reserved so the app's menus cannot reach it */
	app.configString("system.argentum", "desktop.clockFormat",
			 "%a %e %b  %H:%M", gClockFmt, sizeof(gClockFmt));
	gClockW = clockTextWidth(gClockFmt);
	clockUpdate(true);
	printf("KESTREL: strip zones: system=0..%d menus<-%d clock=%d..%d \"%s\"\n",
	       SYS_ZONE_W, clockZoneLeft(), clockZoneLeft() + CLOCK_GAP,
	       screenW - CLOCK_INSET, gClockFmt);
	fflush(stdout);
	gBar = &bar;
	stripX = bar.xid();
	/* map the strip directly: the toolkit's show() also grabs input
	 * focus, which races the map under SubstructureRedirect and
	 * trips a BadMatch (the focus window must be viewable). */
	XMapWindow(dpy, stripX);
	/* S4.3b: chrome with the map (see the frames) */
	bar.draw();
	XSync(dpy, False);
	XRaiseWindow(dpy, stripX);
	XSync(dpy, False);

	/* manage clients that mapped before we selected redirect */
	{
		::Window r, *kids = nullptr;
		unsigned int n = 0;

		if (XQueryTree(dpy, root, &r, &r, &kids, &n)) {
			for (unsigned int i = 0; i < n; i++) {
				XWindowAttributes a;

				if (kids[i] == stripX || kids[i] == deskX ||
				    kids[i] == dockX ||
				    !XGetWindowAttributes(dpy, kids[i], &a) ||
				    a.map_state != IsViewable ||
				    a.override_redirect) {
					continue;
				}
				XMapRequestEvent me;

				memset(&me, 0, sizeof(me));
				me.type = MapRequest;
				me.window = kids[i];
				manageClient(me);
			}
			if (kids) {
				XFree(kids);
			}
		}
	}
	/* S4.2a: the session socket — apps publish their menubars here */
	sessionOpen();

	printf("KESTREL-READY strip=0x%lx\n", (unsigned long) stripX);
	fflush(stdout);

	app.setEventHook(kestrelHook);
	app.setIdleHook(idleBeat);
	app.run();
	return 0;
}
