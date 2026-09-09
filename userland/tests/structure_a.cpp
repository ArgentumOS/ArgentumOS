/* structure_a.cpp — S2.4a acceptance (docs/design/
 * argentum-s24-tier2-structure.md): Box title chrome + row/column
 * arrangement. A titled Column Box "Controls" packs three buttons and
 * a nested untitled Row Box (two buttons) — no manual child frames —
 * and the Box logs the arranged frames (BOX-A lines) for the gate.
 * The gate screendump-probes the box border, the title cap, the
 * buttons at their ARRANGED origins, and the page gaps between them. */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 60;
static const int WIN_Y = 60;
static const unsigned WIN_W = 480;	/* 360 pt @ 4/3 */
static const unsigned WIN_H = 400;	/* 300 pt @ 4/3 */

struct Content : public View {
	void draw(GraphicsContext &g) override
	{
		Application &app = Application::shared();
		Theme &t = app.theme();
		Rect f = frame();
		double ppt = app.pxPerPt();

		g.fillRect(0, 0, (unsigned) (f.size.w * ppt + 0.5),
			   (unsigned) (f.size.h * ppt + 0.5), t.page());
	}
};

int
main()
{
	Application &app = Application::shared();
	int tries;

	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		printf("S24A: app init failed\n");
		return 1;
	}
	Content v;
	Box box;
	Box rowBox;
	Button bOne, bTwo, bThree, bA, bB;

	v.setFrame({ {0, 0}, {360, 300} });

	box.setTitle("Controls");
	box.setLayout(BoxLayout::Column);
	box.setFrame({ {40, 40}, {220, 240} });

	bOne.setTitle("One");
	bTwo.setTitle("Two");
	bThree.setTitle("Three");
	bOne.setFrame({ {0, 0}, {150, 30} });
	bTwo.setFrame({ {0, 0}, {150, 30} });
	bThree.setFrame({ {0, 0}, {150, 30} });

	rowBox.setLayout(BoxLayout::Row);
	rowBox.setFrame({ {0, 0}, {170, 30} });
	bA.setTitle("A");
	bB.setTitle("B");
	bA.setFrame({ {0, 0}, {70, 26} });
	bB.setFrame({ {0, 0}, {70, 26} });

	box.addSubview(&bOne);
	box.addSubview(&bTwo);
	box.addSubview(&bThree);
	box.addSubview(&rowBox);
	rowBox.addSubview(&bA);
	rowBox.addSubview(&bB);
	v.addSubview(&box);

	Window w;

	if (!w.init("S2.4a Box", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S24A: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S24A-READY\n");
	fflush(stdout);

	/* the app loop drives the first composite (BOX-A logs + draw);
	 * the run script screendumps once BOX-A appears, then halts */
	app.run();
	return 0;
}
