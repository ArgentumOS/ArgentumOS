/* textview_a.cpp — TXT-a acceptance (docs/design/argentum-textview.md):
 * TextView layout + read-only draw. A fixed-width TextView shows a
 * document with a forced hard break and a wrapping paragraph; the
 * board logs the visual lines (line count + per-line text + the line
 * box metrics) and the a11y role reads back "text area". The gate's
 * screendump asserts glyphs on the wrapped lines and an empty band at
 * the blank line. */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 520;	/* 390 pt @ 4/3 */
static const unsigned WIN_H = 340;	/* 255 pt @ 4/3 */

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
		printf("TXT-A: app init failed\n");
		return 1;
	}
	static const char *PARA1 =
		"Argentum text views lay out multi-line documents. "
		"The greedy wrapper breaks whole lines at spaces and "
		"stacks them in fixed-height boxes.";
	static const char *PARA2 =
		"A hard break ends the paragraph; blank lines survive.";

	Content c;
	TextView tv;

	c.setFrame({ {0, 0}, {390, 255} });
	tv.setFrame({ {18, 18}, {350, 200} });
	tv.setAccessibilityLabel("Story");

	char doc[1024];

	std::snprintf(doc, sizeof(doc), "%s\n\n%s", PARA1, PARA2);
	tv.setValue(doc);

	c.addSubview(&tv);

	argentum::Window w;

	if (!w.init("TXT-a wrap", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("TXT-A: window init failed\n");
		return 1;
	}
	w.setContentView(&c);

	/* log the layout before the first draw (forces it) */
	{
		Theme &t = app.theme();
		TextMetrics m = textMetrics(t.fontFamily(), t.fontSizePt(),
					    "Ag");

		printf("TXT-A: box asc=%.2f desc=%.2f\n", m.ascentPt,
		       m.descentPt);
		unsigned int n = tv.lineCount();
		char line[1024];

		printf("TXT-A-LINES: %u\n", n);
		for (unsigned int i = 0; i < n; i++) {
			line[0] = 0;
			tv.lineText(i, line, sizeof(line));
			printf("TXT-A: line %u len=%zu '%s'\n", i,
			       strlen(line), line);
		}
		printf("TXT-A-A11Y: role=%s label=%s value_len=%zu\n",
		       accessibilityRoleName(tv.accessibilityRole()),
		       "Story", strlen(tv.value()));
		fflush(stdout);
	}
	w.show();
	printf("TXT-A-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	app.run();
	return 0;
}
