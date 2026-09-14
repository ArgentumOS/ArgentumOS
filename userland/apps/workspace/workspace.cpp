/*
 * Workspace — the desktop shell app (W0a of docs/design/workspace-plan.md).
 *
 * It owns the DESKTOP SURFACE: a window covering the screen, painted with
 * the theme's ramp. It carries _ARGENTUM_DESKTOP before it is mapped, which
 * is the whole protocol: an app's window is a CLIENT, and without the marker
 * the WM would frame it like any other. (Until W0b the WM also paints a
 * surface of its own, under this one and with the same ramp, so the desktop
 * is pixel-identical by construction while the hand-off is proven.)
 *
 * The ramp is S5.2a's, moved here with the surface: solid bands off
 * Application::sessionBackground(). Integer-only on purpose — the double
 * path in this toolchain has produced garbage before, and a wrong wallpaper
 * colour is exactly the kind of thing that hides (the screen still looks
 * like a ramp).
 *
 * It follows a mode-set itself: the root resizes, the surface is sized to it
 * (Xfb's mode changes are a root resize, not a window one). Kestrel used to
 * do this for its own wallpaper window; W0b deleted that, so this is the only
 * thing keeping the surface covering the screen.
 *
 * W1 adds the browser window: an ordinary MANAGED window (the WM frames it,
 * it takes focus) alongside the surface, which is a client the WM ignores —
 * the app has both kinds at once, which is the multi-window model D8 settled.
 * The window opens on the user's home and reads it.
 *
 * The start root comes from the IDENTITY plumbing, not from $HOME: the
 * session deliberately sets HOME=/ (there is no login), while this OS already
 * serves getpwuid() from the system.passwd domain — so the real home is one
 * call away, and no account name is hardcoded here.
 *
 * Not here yet: a PNG wallpaper (S5.2h: no decoder); the columns (W2).
 */
#include <argentum/argentum.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <pwd.h>
#include <sys/stat.h>
#include <vector>
#include <string>
#include <sys/types.h>
#include <unistd.h>

/* the browser window's size, in points (the WM is free to re-place it) */
#define BROWSER_W_PT 720
#define BROWSER_H_PT 420

static Display *dpy = nullptr;
static ::Window deskX = 0;
static argentum::Window *gDesk = nullptr;
static int gW = 0, gH = 0;

static std::uint32_t
mixColor(std::uint32_t a, std::uint32_t b, unsigned int num)
{
	/* num 0..256: 0 = a, 256 = b */
	unsigned int ar = (a >> 16) & 0xff, ag = (a >> 8) & 0xff, ab = a & 0xff;
	unsigned int br = (b >> 16) & 0xff, bg = (b >> 8) & 0xff, bb = b & 0xff;
	unsigned int r = (ar * (256 - num) + br * num) >> 8;
	unsigned int g = (ag * (256 - num) + bg * num) >> 8;
	unsigned int bl = (ab * (256 - num) + bb * num) >> 8;

	return ((r & 0xff) << 16) | ((g & 0xff) << 8) | (bl & 0xff);
}

/* The two endpoint tones, as functions so a log line and the painter cannot
 * drift apart: 22% toward white, 18% toward black. */
static std::uint32_t
deskTop(std::uint32_t base)
{
	return mixColor(base, 0xffffff, 56);
}

static std::uint32_t
deskBot(std::uint32_t base)
{
	return mixColor(base, 0x000000, 46);
}

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
static bool surfacePaint(int w, int h);

/* A root resize is a mode-set: the surface is sized to the screen. Not
 * consumed — returning false lets the toolkit keep processing the event. */
static bool
onEvent(void *xevent)
{
	XEvent *ev = (XEvent *) xevent;

	if (ev->type == ConfigureNotify &&
	    ev->xconfigure.window == DefaultRootWindow(dpy)) {
		surfacePaint(DisplayWidth(dpy, DefaultScreen(dpy)),
			     DisplayHeight(dpy, DefaultScreen(dpy)));
	}
	return false;
}

/* Declare this window the desktop BEFORE mapping it: the WM reads the marker
 * when it decides whether to frame the client, and by then it is too late. */
static bool
markDesktop(::Window w)
{
	Atom a = XInternAtom(dpy, "_ARGENTUM_DESKTOP", False);
	unsigned long one = 1;

	/* XChangeProperty returns 1 on success — Success is 0 and is NOT that
	 * answer, so comparing against it reports failure on every success
	 * (which is how this read the first time it ran). */
	return XChangeProperty(dpy, w, a, XA_CARDINAL, 32, PropModeReplace,
			       (unsigned char *) &one, 1) != 0;
}

static bool
surfacePaint(int w, int h)
{
	double ppt = argentum::Application::shared().pxPerPt();

	if (w <= 0 || h <= 0)
		return false;

	if (!gDesk) {
		gDesk = new argentum::Window();
		if (!gDesk->init("Argentum Surface", 0, 0, (unsigned) w,
				 (unsigned) h)) {
			std::fprintf(stderr, "WORKSPACE: surface init failed\n");
			delete gDesk;
			gDesk = nullptr;
			return false;
		}
		gDeskView = new DeskView();
		gDesk->setContentView(gDeskView);
		deskX = gDesk->xid();
		if (!markDesktop(deskX)) {
			std::fprintf(stderr, "WORKSPACE: desktop marker failed\n");
		}
		XMapWindow(dpy, deskX);
	} else if (w != gW || h != gH) {
		XResizeWindow(dpy, deskX, (unsigned) w, (unsigned) h);
	}

	gDeskView->setFrame({{0, 0}, {w / ppt, h / ppt}});
	gDeskView->setNeedsDisplay();
	gDesk->draw();
	XSync(dpy, False);

	gW = w;
	gH = h;
	{
		std::uint32_t base =
			argentum::Application::shared().sessionBackground();

		printf("WORKSPACE: desktop surface %dx%d base=0x%06x top=0x%06x "
		       "bot=0x%06x\n", w, h, base, deskTop(base), deskBot(base));
		fflush(stdout);
	}
	return true;
}

/* ---- W1: the browser window ---------------------------------------- */

/* The start root: the real home, from getpwuid (musl here answers it from
 * the system.passwd domain), with the shipped account as the fallback so a
 * missing identity degrades to something browsable instead of to "/". */
static std::string
homeRoot()
{
	struct passwd *pw = getpwuid(getuid());

	if (pw && pw->pw_dir && pw->pw_dir[0] == '/') {
		return std::string(pw->pw_dir);
	}
	return std::string("/Users/Admin");
}

/* D3: listing is libc, not a shell-out. Parsing `ls` would break on names
 * with spaces or newlines and would make the browser's latency a
 * process-launch problem. Dot entries are excluded, which is what `ls -A`
 * counts — the number the gate compares against. */
static int
countEntries(const char *path, std::string *why)
{
	DIR *d = opendir(path);
	int n = 0;
	struct dirent *de;

	if (!d) {
		*why = std::string(strerror(errno));
		return -1;
	}
	while ((de = readdir(d))) {
		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		n++;
	}
	closedir(d);
	return n;
}

/* ---- W2: the first column ------------------------------------------- */

/* The listing, read ONCE and kept: the control asks its source for rows as it
 * draws, so a source that re-read the directory per row would both be slow and
 * lie (the directory can change mid-draw). This is also where a row learns
 * whether it is a folder — the mark W3 needs and the reason a file manager
 * stats at all. */
struct Entry {
	std::string name;
	bool dir = false;
};

static std::vector<Entry> gEntries;

static int
listEntries(const std::string &path)
{
	DIR *d = opendir(path.c_str());
	struct dirent *de;
	int n = 0;

	if (!d)
		return -1;
	while ((de = readdir(d))) {
		struct stat st;
		Entry e;

		if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, ".."))
			continue;
		e.name = de->d_name;
		if (stat((path + "/" + e.name).c_str(), &st) == 0)
			e.dir = S_ISDIR(st.st_mode);
		gEntries.push_back(e);
		n++;
	}
	closedir(d);
	return n;
}

class WorkspaceSource : public argentum::BrowserSource {
public:
	int browserRowCount(const argentum::Browser *,
			    int column) const override
	{
		return column == 0 ? (int) gEntries.size() : 0;
	}

	const char *browserRowText(const argentum::Browser *, int column,
				   int row) const override
	{
		if (column != 0 || row < 0 || row >= (int) gEntries.size())
			return "";
		return gEntries[row].name.c_str();
	}
};

class WorkspaceDelegate : public argentum::BrowserDelegate {
public:
	void browserSelectionDidChange(argentum::Browser *, int column,
				      int row) override
	{
		if (column != 0 || row < 0 || row >= (int) gEntries.size())
			return;
		/* W2 reports what was chosen; W3 is what descends into it */
		std::printf("WORKSPACE: selected %s%s\n",
			    gEntries[(size_t) row].name.c_str(),
			    gEntries[(size_t) row].dir ? "/" : "");
		std::fflush(stdout);
	}
};

int
main()
{
	argentum::Application &app = argentum::Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "WORKSPACE: init failed\n");
		return 1;
	}

	dpy = (Display *) app.display();
	if (!dpy) {
		std::fprintf(stderr, "WORKSPACE: no display\n");
		return 1;
	}

	/* the root's size changes are OURS to follow now (W0b) */
	XSelectInput(dpy, DefaultRootWindow(dpy), StructureNotifyMask);
	app.setEventHook(onEvent);

	if (!surfacePaint(DisplayWidth(dpy, DefaultScreen(dpy)),
			  DisplayHeight(dpy, DefaultScreen(dpy)))) {
		return 1;
	}

	/* the browser window, on the user's home */
	{
		double ppt = app.pxPerPt();
		std::string root = homeRoot();
		int n = listEntries(root);

		if (n < 0) {
			std::printf("WORKSPACE: %s: unreadable (%s)\n",
				    root.c_str(), strerror(errno));
		} else {
			std::printf("WORKSPACE: %s: %d entries\n",
				    root.c_str(), n);
			if (n > 0) {
				/* the rows the column is about to draw: named, so
				 * the gate reads what was listed rather than
				 * trusting a pixel */
				std::printf("WORKSPACE: column 0 rows=%d first=%s"
					    " last=%s\n", n,
					    gEntries.front().name.c_str(),
					    gEntries.back().name.c_str());
			}
		}
		std::fflush(stdout);

		static argentum::Window bw;
		static WorkspaceSource src;
		static WorkspaceDelegate del;
		static argentum::Browser browser(&del);

		browser.setFrame({{0, 0}, {BROWSER_W_PT, BROWSER_H_PT}});
		browser.setSource(&src);
		browser.setFocusedColumn(0);
		if (!bw.init("Workspace", 120, 120,
			     (unsigned) (BROWSER_W_PT * ppt + 0.5),
			     (unsigned) (BROWSER_H_PT * ppt + 0.5))) {
			std::fprintf(stderr, "WORKSPACE: browser init failed\n");
			return 1;
		}
		bw.setContentView(&browser);
		bw.show();
	}

	app.run();
	return 0;
}
