/* argentum/level.cpp — S2.3b LevelIndicator (docs/design/
 * argentum-s23-tier1-rest.md): an input-free capacity gauge. A row of
 * `cellCount` cells; ceil(level*cellCount) of them fill in the accent
 * colour above an empty page/chrome track. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdio>

namespace argentum {

static void
syncA11yValue(LevelIndicator *li)
{
	char buf[16];

	std::snprintf(buf, sizeof(buf), "%g", li->level());
	li->setAccessibilityValue(buf);
}

LevelIndicator::LevelIndicator()
	: lev_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::LevelIndicator);
	syncA11yValue(this);
}

LevelIndicator::~LevelIndicator()
{
	delete lev_;
}

void
LevelIndicator::setLevel(double value)
{
	if (value < 0) {
		value = 0;
	}
	if (value > 1) {
		value = 1;
	}
	if (lev_->level != value) {
		lev_->level = value;
		syncA11yValue(this);
		setNeedsDisplay();
	}
}

double
LevelIndicator::level() const
{
	return lev_->level;
}

void
LevelIndicator::setCellCount(int n)
{
	if (n < 1) {
		n = 1;
	}
	if (lev_->cells != n) {
		lev_->cells = n;
		setNeedsDisplay();
	}
}

int
LevelIndicator::cellCount() const
{
	return lev_->cells;
}

void
LevelIndicator::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);

	if (w < 4 || h < 4 || lev_->cells < 1) {
		return;
	}
	int gap = 2;
	int cells = lev_->cells;
	double cellW = (w - (cells + 1) * gap) / (double) cells;

	if (cellW < 1) {
		cellW = 1;
	}
	int cy = gap;
	int ch = h - 2 * gap;
	int filled = (int) std::ceil(lev_->level * cells);

	if (filled > cells) {
		filled = cells;
	}
	int cr = 1;

	for (int i = 0; i < cells; i++) {
		int cx = gap + (int) (i * (cellW + gap));

		g.fillRoundedRect(cx, cy, (unsigned) cellW, (unsigned) ch,
				  (unsigned) cr, theme.chromeOutline());
		if (i < filled) {
			g.fillRoundedRect(cx + 1, cy + 1,
					  (unsigned) (cellW - 2),
					  (unsigned) (ch - 2),
					  (unsigned) cr, theme.accent());
		} else {
			g.fillRoundedRect(cx + 1, cy + 1,
					  (unsigned) (cellW - 2),
					  (unsigned) (ch - 2),
					  (unsigned) cr, theme.page());
		}
	}
}

} /* namespace argentum */
