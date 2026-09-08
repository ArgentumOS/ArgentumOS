/* argentum/progress.cpp — S2.3b ProgressIndicator (docs/design/
 * argentum-s23-tier1-rest.md): an input-free determinate progress bar.
 * A rounded track (chrome outline + page fill) with the theme accent
 * filling `progress` (0..1) of its width from the leading edge. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>

namespace argentum {

static void
syncA11yValue(ProgressIndicator *pi)
{
	char buf[16];

	std::snprintf(buf, sizeof(buf), "%g", pi->progress());
	pi->setAccessibilityValue(buf);
}

ProgressIndicator::ProgressIndicator()
	: pro_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::ProgressIndicator);
	syncA11yValue(this);
}

ProgressIndicator::~ProgressIndicator()
{
	delete pro_;
}

void
ProgressIndicator::setProgress(double value)
{
	if (value < 0) {
		value = 0;
	}
	if (value > 1) {
		value = 1;
	}
	if (pro_->progress != value) {
		pro_->progress = value;
		syncA11yValue(this);
		setNeedsDisplay();
	}
}

double
ProgressIndicator::progress() const
{
	return pro_->progress;
}

void
ProgressIndicator::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = (int) (f.size.w * ppt + 0.5);
	int h = (int) (f.size.h * ppt + 0.5);
	int r = (int) (theme.smallRadius() * ppt + 0.5);

	if (r < 1) {
		r = 1;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	if (w < 4 || h < 4) {
		return;
	}
	/* track */
	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
			  (unsigned) r, theme.chromeOutline());
	g.fillRoundedRect(1, 1, (unsigned) (w - 2), (unsigned) (h - 2),
			  (unsigned) (r - 1 > 0 ? r - 1 : 0),
			  theme.page());
	/* fill */
	int fw = (int) ((w - 2) * pro_->progress + 0.5);

	if (fw > 0) {
		g.fillRoundedRect(1, 1, (unsigned) fw, (unsigned) (h - 2),
				  (unsigned) (r - 1 > 0 ? r - 1 : 0),
				  theme.accent());
	}
}

} /* namespace argentum */
