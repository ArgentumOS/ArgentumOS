/* argentum/segmented.cpp — S2.3b SegmentedControl (docs/design/
 * argentum-s23-tier1-rest.md): N titled segments in one chrome bezel;
 * the selected segment draws armed/accent, hovered segments hover
 * chrome. Clicking selects; the action fires on release inside the
 * same segment; Left/Right adjust when focused. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cstdio>
#include <cstring>

namespace argentum {

static void
syncA11yValue(SegmentedControl *sc)
{
	char buf[16];

	std::snprintf(buf, sizeof(buf), "%d", sc->selectedIndex());
	sc->setAccessibilityValue(buf);
}

SegmentedControl::SegmentedControl()
	: seg_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::SegmentedControl);
	syncA11yValue(this);
}

SegmentedControl::~SegmentedControl()
{
	delete seg_;
}

void
SegmentedControl::setSegments(const char *const *titles, int count)
{
	if (count > Impl::kMax) {
		count = Impl::kMax;
	}
	if (count < 1) {
		count = 1;
	}
	for (int i = 0; i < count; i++) {
		std::snprintf(seg_->titles[i], sizeof(seg_->titles[i]),
			      "%s", titles ? titles[i] : "");
	}
	seg_->count = count;
	if (seg_->selected >= count) {
		seg_->selected = count - 1;
	}
	syncA11yValue(this);
	setNeedsDisplay();
}

int
SegmentedControl::segmentCount() const
{
	return seg_->count;
}

const char *
SegmentedControl::segmentTitle(int i) const
{
	if (i < 0 || i >= seg_->count) {
		return "";
	}
	return seg_->titles[i];
}

void
SegmentedControl::setSelectedIndex(int i)
{
	if (i < 0 || i >= seg_->count) {
		return;
	}
	if (seg_->selected != i) {
		seg_->selected = i;
		syncA11yValue(this);
		setNeedsDisplay();
	}
}

int
SegmentedControl::selectedIndex() const
{
	return seg_->selected;
}

int
SegmentedControl::segmentAt(double localPt) const
{
	Rect f = frame();
	double wpt = f.size.w;

	if (localPt < 0 || localPt >= wpt || seg_->count < 1) {
		return -1;
	}
	return (int) (localPt / (wpt / seg_->count));
}

void
SegmentedControl::draw(GraphicsContext &g)
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
	if (seg_->count < 1) {
		return;
	}
	/* overall bezel */
	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
			  (unsigned) r, theme.chromeOutline());
	double sw = w / (double) seg_->count;
	int o = 1;

	for (int i = 0; i < seg_->count; i++) {
		int x0 = (int) (i * sw) + o;
		int x1 = (int) ((i + 1) * sw) - o;

		if (x1 <= x0) {
			x1 = x0 + 1;
		}
		Theme::Params p;
		bool selected = (i == seg_->selected) && isEnabled();
		bool hovered = (i == seg_->armed) && isEnabled();

		if (!isEnabled()) {
			p = theme.state(ControlState::Disabled);
		} else if (selected) {
			p = theme.state(ControlState::Armed);
		} else if (hovered) {
			p = theme.state(ControlState::Hover);
		} else {
			p = theme.state(ControlState::Idle);
		}
		g.fillRoundedGradient(x0, o, (unsigned) (x1 - x0),
				      (unsigned) (h - 2 * o),
				      (unsigned) (r - 1 > 0 ? r - 1 : 0),
				      p.fillTop, p.fillBottom);
		/* label, centred in the segment */
		const char *title = seg_->titles[i];

		if (title[0]) {
			argentum::TextMetrics m =
				argentum::textMetrics(theme.fontFamily(),
						     theme.fontSizePt(), title);
			double wPt = m.widthPt;
			double hPt = m.ascentPt + m.descentPt;
			int tx = x0 + (int) ((x1 - x0 - wPt * ppt) / 2.0);
			int ty = o + (int) ((h - 2 * o - hPt * ppt) / 2.0);

			g.drawText(theme.fontFamily(), theme.fontSizePt(),
				   tx, ty, title, p.label);
		}
	}
	/* separators (a page-coloured 1px line between segments) */
	for (int i = 1; i < seg_->count; i++) {
		int sx = (int) (i * sw);

		g.drawLine(sx, o, sx, h - o, theme.page());
	}
}

void
SegmentedControl::mouseDown(const MouseEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	int seg = segmentAt(e.x);

	setArmed(seg >= 0);
	setHovered(seg >= 0);
	seg_->armed = seg;
	if (seg >= 0) {
		setSelectedIndex(seg);
	}
}

void
SegmentedControl::mouseUp(const MouseEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	if (armed() && seg_->armed >= 0 && segmentAt(e.x) == seg_->armed) {
		seg_->armed = -1;
		setArmed(false);
		sendAction();
		return;
	}
	seg_->armed = -1;
	setArmed(false);
}

void
SegmentedControl::keyDown(const KeyEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	if (e.keysym == 0xff51 || e.keysym == 0xff53) {
		int next = seg_->selected + (e.keysym == 0xff53 ? 1 : -1);

		if (next >= 0 && next < seg_->count) {
			setSelectedIndex(next);
			sendAction();
		}
		return;
	}
	Control::keyDown(e);
}

} /* namespace argentum */
