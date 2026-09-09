/* structure_e.cpp — S2.4e acceptance (docs/design/
 * argentum-s24-tier2-structure.md): TableView-basic inside a
 * ScrollView. A 3-column / 14-row table (Name/Kind/Size) renders its
 * header + data rows from a data source; the gate clicks row 3 and
 * screendumps before/after — the row selection must tint the row and
 * fire the delegate (TABLE-SELECT). The board also reads back the
 * table's a11y role/label. */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

using namespace argentum;

static const int WIN_X = 40;
static const int WIN_Y = 40;
static const unsigned WIN_W = 560;	/* 420 pt @ 4/3 */
static const unsigned WIN_H = 480;	/* 360 pt @ 4/3 */

static const char *NAMES[] = {
	"init", "sh", "Xfb", "xdraw", "xkey", "theme_chrome", "widgets",
	"viewtree", "zoo", "config", "acls", "mount", "df", "ps",
};
static const char *KINDS[] = {
	"tool", "shell", "server", "demo", "demo", "probe", "probe",
	"probe", "board", "tool", "probe", "tool", "tool", "tool",
};
static const char *SIZES[] = {
	"48K", "120K", "8.1M", "24K", "18K", "96K", "150K",
	"72K", "340K", "60K", "40K", "52K", "38K", "44K",
};

struct Source : public TableViewDataSource {
	int rowCount() const override
	{
		return 14;
	}
	const char *cellText(int row, int col) const override
	{
		if (row < 0 || row >= 14) {
			return "";
		}
		switch (col) {
		case 0:
			return NAMES[row];
		case 1:
			return KINDS[row];
		default:
			return SIZES[row];
		}
	}
};

struct Delegate : public TableViewDelegate {
	void tableSelectionDidChange(TableView *, int row) override
	{
		printf("TABLE-SELECT: %d\n", row);
		fflush(stdout);
	}
};

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
		printf("S24E: app init failed\n");
		return 1;
	}
	Content v;

	v.setFrame({ {0, 0}, {420, 360} });

	Source src;
	Delegate del;
	const char *cols[] = { "Name", "Kind", "Size" };
	TableView table(&src, &del);
	ScrollView scroll;

	table.setColumns(cols, 3);
	/* header = rowH + 2 pt; rows 14 * rowH */
	double rh = table.rowHeight();
	double docH = (rh + 2.0) + 14 * rh;

	table.setFrame({ {0, 0}, {300, docH} });
	scroll.setFrame({ {40, 40}, {300, 180} });
	scroll.setDocumentView(&table);
	table.setAccessibilityLabel("Files");
	v.addSubview(&scroll);

	printf("S24E-INFO: rowH=%.1f docH=%.1f cols=%d\n", rh, docH, 3);
	printf("S24E-A11Y: role=%s label=%s\n",
	       accessibilityRoleName(table.accessibilityRole()),
	       table.accessibilityLabel() ? table.accessibilityLabel() : "");
	fflush(stdout);

	argentum::Window w;

	if (!w.init("S2.4e Table", WIN_X, WIN_Y, WIN_W, WIN_H)) {
		printf("S24E: window init failed\n");
		return 1;
	}
	w.setContentView(&v);
	w.show();
	printf("S24E-READY xid=0x%lx\n", (unsigned long) w.xid());
	fflush(stdout);

	app.run();
	return 0;
}
