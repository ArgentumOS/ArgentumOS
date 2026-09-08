/* viewtree_d.cpp — Argentum S2.1d acceptance (docs/design/
 * argentum-s21-view-tree.md): springs/struts relayout.
 *
 * Window content tree (frames in pt at the fallback 4/3 px/pt):
 *   content        (0,0,360x270)  autoresizing none (the tree root)
 *     header       (0,0,360x40)   FlexibleWidth  — fixed height, top bar
 *     contentArea  (0,40,360x230) FlexibleWidth|FlexibleHeight
 *
 * The probe opens a second X connection and XResizeWindow's the window
 * from 480x360 px to 600x420 px (= 450x315 pt). Application::run()
 * gets ConfigureNotify -> Window::handleResize -> contentView->setFrame
 * -> resizeSubviewsWithOldBounds, then the Expose redraw walks the tree
 * and each view logs its NEW frame (view-local draw space = local pt):
 *   VTREE-D: header w=450 h=40        (stretched, height kept)
 *   VTREE-D: content y=40 w=450 h=275 (grew down with the window)
 * The gate asserts the LAST set of VTREE-D lines (post-resize).
 */
#include <argentum/argentum.h>

#include <X11/Xlib.h>

#include <cstdio>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;	/* px before resize */
static const unsigned WIN_H = 360;
static const unsigned RESIZE_W = 600;	/* px after */
static const unsigned RESIZE_H = 420;

class Board : public argentum::View {
public:
	const char *tag;

	Board(const char *tag) : tag(tag) {}

	/* logs the frame we now occupy, then paints a solid block over
	 * our local bounds so the screendump shows the layout too */
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();

		std::fprintf(stderr, "VTREE-D: %s x=%.0f y=%.0f w=%.0f h=%.0f\n",
			     tag, f.origin.x, f.origin.y,
			     f.size.w, f.size.h);
		std::fflush(stderr);
		g.fillRect(0, 0,
			   (unsigned int)(f.size.w * ppt + 0.5),
			   (unsigned int)(f.size.h * ppt + 0.5),
			   tag[0] == 'h' ? 0x336699u : 0x66aa55u);
	}
};

class ContentView : public argentum::View {
public:
	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();

		g.fillRect(0, 0,
			   (unsigned int)(f.size.w * ppt + 0.5),
			   (unsigned int)(f.size.h * ppt + 0.5),
			   0x223344u);
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
		std::fprintf(stderr, "VTREE-D: init failed\n");
		return 1;
	}

	ContentView content;
	Board header("header");
	Board contentArea("content");

	content.setFrame({ {0, 0}, {360, 270} });
	header.setFrame({ {0, 0}, {360, 40} });
	header.setAutoresizingMask(argentum::View::AutoresizingFlexibleWidth);
	contentArea.setFrame({ {0, 40}, {360, 230} });
	contentArea.setAutoresizingMask(
		argentum::View::AutoresizingFlexibleWidth |
		argentum::View::AutoresizingFlexibleHeight);
	content.addSubview(&header);
	content.addSubview(&contentArea);

	class DWindow : public argentum::Window {
	};
	DWindow w;

	if (!w.init("Argentum S2.1d springs/struts", WIN_X, WIN_Y,
		    WIN_W, WIN_H)) {
		std::fprintf(stderr, "VTREE-D: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.show();

	/* resize from a second connection: the server sends
	 * ConfigureNotify + Expose to the app's connection */
	Display *dpy = XOpenDisplay(nullptr);
	if (!dpy) {
		std::fprintf(stderr, "VTREE-D: no display for resize\n");
		return 1;
	}
	::Window xwin = (::Window) w.xid();

	usleep(300000);			/* let the initial draw flush */
	XResizeWindow(dpy, xwin, RESIZE_W, RESIZE_H);
	XSync(dpy, False);
	std::fprintf(stderr, "VTREE-D: resized to %ux%u px\n",
		     RESIZE_W, RESIZE_H);
	std::fflush(stderr);
	XCloseDisplay(dpy);

	app.run();			/* ConfigureNotify -> Expose */
	return 0;
}
