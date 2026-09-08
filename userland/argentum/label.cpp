/* argentum/label.cpp — S2.2b Label (docs/design/
 * argentum-s22-control-first-leaves.md): the catalog's hello world —
 * text + theme + draw + a11y in one input-free view. Draws one run of
 * the theme font inside its bounds, left-aligned and vertically
 * centred on the frame's centreline, through GraphicsContext::drawText
 * (S2.2a) so the text obeys the view-tree clip. */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstring>

namespace argentum {

Label::Label()
	: lbl_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::StaticText);
}

Label::Label(const char *utf8)
	: lbl_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::StaticText);
	setText(utf8);
}

Label::~Label()
{
	delete lbl_;
}

void
Label::setText(const char *utf8)
{
	if (!utf8) {
		lbl_->text[0] = 0;
	} else {
		std::strncpy(lbl_->text, utf8, sizeof(lbl_->text) - 1);
		lbl_->text[sizeof(lbl_->text) - 1] = 0;
	}
	setAccessibilityLabel(lbl_->text);
	setNeedsDisplay();
}

const char *
Label::text() const
{
	return lbl_->text;
}

void
Label::setTextColor(std::uint32_t rgb)
{
	if (lbl_->color != rgb) {
		lbl_->color = rgb;
		setNeedsDisplay();
	}
}

std::uint32_t
Label::textColor() const
{
	return lbl_->color;
}

void
Label::setFontSizePt(double sizePt)
{
	if (lbl_->sizePt != sizePt) {
		lbl_->sizePt = sizePt;
		setNeedsDisplay();
	}
}

double
Label::fontSizePt() const
{
	return lbl_->sizePt;
}

void
Label::draw(GraphicsContext &g)
{
	if (!lbl_->text[0]) {
		return;
	}
	Application &app = Application::shared();
	Theme &theme = app.theme();
	const char *family = theme.fontFamily();
	double sizePt = lbl_->sizePt > 0 ? lbl_->sizePt : theme.fontSizePt();
	std::uint32_t color = lbl_->color ? lbl_->color : theme.text();

	TextMetrics m = textMetrics(family, sizePt, lbl_->text);
	if (m.widthPt <= 0) {
		return;
	}
	double ppt = app.pxPerPt();
	Rect f = frame();
	int frameHpx = (int) (f.size.h * ppt + 0.5);
	int ascPx = (int) (m.ascentPt * ppt + 0.5);
	int descPx = (int) (m.descentPt * ppt + 0.5);

	/* the run box is ascent+descent+2px tall (the text core's 1px pad
	 * each side); centre that box on the frame's vertical centreline */
	int boxHpx = ascPx + descPx + 2;
	int yTop = boxHpx < frameHpx ? (frameHpx - boxHpx) / 2 : 0;

	g.drawText(family, sizePt, 0, yTop, lbl_->text, color);
}

} /* namespace argentum */
