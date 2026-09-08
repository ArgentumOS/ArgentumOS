/* widgets_g.cpp — Argentum S2.3c acceptance APP (docs/design/
 * argentum-s23-tier1-rest.md): ImageView content modes + PopUpButton
 * with its Menu. A companion helper (widgets_g_inj) drives the
 * interactions from a second X connection while this app runs, so
 * popup open/hover/close can be screendumped mid-state.
 *
 * Board window 480x360 at (100,80); content (360x270 pt):
 *   ivStretch (10,10,120x60) Stretch   of a 24x24 half-green/half-blue
 *   ivFit     (140,10,100x60) ScaleToFit   image (see below)
 *   ivCenter  (10,80,100x80)  Center
 *   popup button "File" (10,150,140x26) with Menu New/Open.../Quit
 * a11y read-back + S23C-DRAW logs per redraw. */
#include <argentum/argentum.h>

#include <X11/Xlib.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;
static const unsigned WIN_H = 360;

class ContentView : public argentum::View {
public:
	argentum::ImageView ivStretch;
	argentum::ImageView ivFit;
	argentum::ImageView ivCenter;
	argentum::PopUpButton pop;

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();

		g.fillRect(0, 0, (unsigned int) (f.size.w * ppt + 0.5),
			   (unsigned int) (f.size.h * ppt + 0.5), 0x223344);
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
		std::fprintf(stderr, "S23C: init failed\n");
		return 1;
	}

	/* the painted source image: 24x24, left half green 0x1fa84d,
	 * right half blue 0x2288ee */
	argentum::BitmapImage img(24, 24);
	{
		argentum::GraphicsContext g0(img);

		g0.fillRect(0, 0, 12, 24, 0x1fa84d);
		g0.fillRect(12, 0, 12, 24, 0x2288ee);
	}

	ContentView content;
	content.setFrame({ {0, 0}, {360, 270} });
	content.ivStretch.setFrame({ {10, 10}, {120, 60} });
	content.ivStretch.setImage(&img);
	content.ivStretch.setContentMode(argentum::ImageContentMode::Stretch);
	content.ivFit.setFrame({ {140, 10}, {100, 60} });
	content.ivFit.setImage(&img);
	content.ivFit.setContentMode(argentum::ImageContentMode::ScaleToFit);
	content.ivCenter.setFrame({ {10, 80}, {100, 80} });
	content.ivCenter.setImage(&img);
	content.ivCenter.setContentMode(argentum::ImageContentMode::Center);
	content.addSubview(&content.ivStretch);
	content.addSubview(&content.ivFit);
	content.addSubview(&content.ivCenter);

	/* the menu */
	argentum::MenuItem miNew("New");
	argentum::MenuItem miOpen("Open...");
	argentum::MenuItem miQuit("Quit");
	argentum::Menu fileMenu;

	fileMenu.setTitle("File");
	fileMenu.addItem(&miNew);
	fileMenu.addItem(&miOpen);
	fileMenu.addItem(&miQuit);
	miNew.setAction([]() {
		std::fprintf(stderr, "S23C-ACTION: New\n");
		std::fflush(stderr);
	});
	miOpen.setAction([]() {
		std::fprintf(stderr, "S23C-ACTION: Open...\n");
		std::fflush(stderr);
	});
	miQuit.setAction([]() {
		std::fprintf(stderr, "S23C-ACTION: Quit\n");
		std::fflush(stderr);
	});
	content.pop.setFrame({ {10, 150}, {140, 26} });
	content.pop.setTitle("File");
	content.pop.setMenu(&fileMenu);
	content.addSubview(&content.pop);

	/* a11y read-back */
	std::fprintf(stderr, "S23C-A11Y: %s label=%s\n",
		     argentum::accessibilityRoleName(
			     content.ivStretch.accessibilityRole()),
		     content.ivStretch.accessibilityLabel());
	std::fprintf(stderr, "S23C-A11Y: %s label=%s\n",
		     argentum::accessibilityRoleName(
			     content.pop.accessibilityRole()),
		     content.pop.accessibilityLabel());
	std::fflush(stderr);

	class BoardWin : public argentum::Window {
	public:
		void draw() override
		{
			Window::draw();
			std::fprintf(stderr, "S23C-DRAW\n");
			std::fflush(stderr);
		}
	} w;

	if (!w.init("Argentum S2.3c image+menu", WIN_X, WIN_Y, WIN_W,
		    WIN_H)) {
		std::fprintf(stderr, "S23C: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.show();
	std::fprintf(stderr, "S23C-APP-READY xid=0x%lx\n", w.xid());
	std::fflush(stderr);

	app.run();
	return 0;
}
