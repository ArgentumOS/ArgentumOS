/* argentum/textfield.cpp — S2.2d TextField + edit engine
 * (docs/design/argentum-s22-control-first-leaves.md): the first new
 * subsystem. Single-line text input with a value, an insertion caret
 * and selection:
 *   - click: focus (Control) + position the caret at the click x
 *   - printable keys type at the caret (replacing a selection)
 *   - BackSpace/Delete delete (the selection, else around the caret)
 *   - Left/Right/Home/End move the caret (Shift extends the selection)
 * Draws the field bezel (page interior + state outline) with the text
 * split pre/selection/post, the selection highlight (accent bg, white
 * text) and the caret (a 1px bar) while focused. A11y TextField with
 * the value mirrored. valueChanged() fires after every edit.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstring>

namespace argentum {

/* ---- utf8-safe byte helpers ------------------------------------- */

static bool
isCont(unsigned char c)
{
	return (c & 0xc0) == 0x80;
}

/* index of the character START before `at` (at must be a boundary) */
static unsigned int
prevCharStart(const char *s, unsigned int at)
{
	unsigned int p = at;

	if (p == 0) {
		return 0;
	}
	p--;
	while (p > 0 && isCont((unsigned char) s[p])) {
		p--;
	}
	return p;
}

/* index just past the character STARTING at `at` */
static unsigned int
nextCharEnd(const char *s, unsigned int at)
{
	unsigned int n = at;

	if (s[n] == 0) {
		return n;
	}
	n++;
	while (s[n] && isCont((unsigned char) s[n])) {
		n++;
	}
	return n;
}

static unsigned int
tlen(const char *s)
{
	return (unsigned int) std::strlen(s);
}

static int
pxi(double v, double ppt)
{
	return (int) (v * ppt + 0.5);
}

/* quiet advance (pt) of a string, at the theme font */
static double
advancePt(const char *family, double sizePt, const char *utf8)
{
	Application &app = Application::shared();
	unsigned int px = (unsigned int) ((sizePt * app.pxPerPt()) + 0.5);
	TextRun *t = textRunPrepare(family, utf8, px, true);

	if (!t) {
		return 0;
	}
	double a = ((double) textRunAdvance26(t) / 64.0) / app.pxPerPt();

	textRunFinish(t);
	return a;
}

/* ---- state ------------------------------------------------------ */

TextField::TextField()
	: fld_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::TextField);
}

TextField::~TextField()
{
	delete fld_;
}

void
TextField::setValue(const char *utf8)
{
	if (!utf8) {
		fld_->text[0] = 0;
	} else {
		std::strncpy(fld_->text, utf8, sizeof(fld_->text) - 1);
		fld_->text[sizeof(fld_->text) - 1] = 0;
	}
	fld_->caret = tlen(fld_->text);
	fld_->anchor = fld_->caret;
	setAccessibilityValue(fld_->text);
	setNeedsDisplay();
	valueChanged();
}

const char *
TextField::value() const
{
	return fld_->text;
}

unsigned int
TextField::caretIndex() const
{
	return fld_->caret;
}

unsigned int
TextField::selectionStart() const
{
	unsigned int a = fld_->anchor;
	unsigned int c = fld_->caret;

	return a < c ? a : c;
}

unsigned int
TextField::selectionEnd() const
{
	unsigned int a = fld_->anchor;
	unsigned int c = fld_->caret;

	return a > c ? a : c;
}

unsigned int
TextField::charStart(unsigned int at) const
{
	return prevCharStart(fld_->text, at);
}

unsigned int
TextField::charEnd(unsigned int at) const
{
	return nextCharEnd(fld_->text, at);
}

void
TextField::setSelection(unsigned int start, unsigned int end)
{
	fld_->anchor = start;
	fld_->caret = end;
	setNeedsDisplay();
}

/* ---- editing ---------------------------------------------------- */

void
TextField::deleteRange(unsigned int start, unsigned int end)
{
	if (end <= start) {
		return;
	}
	unsigned int len = tlen(fld_->text);

	if (end > len) {
		end = len;
	}
	std::memmove(fld_->text + start, fld_->text + end, len - end + 1);
	fld_->caret = start;
	fld_->anchor = start;
	setAccessibilityValue(fld_->text);
	setNeedsDisplay();
	valueChanged();
}

void
TextField::insertAtCaret(const char *utf8)
{
	unsigned int len = tlen(fld_->text);
	unsigned int ins = tlen(utf8);

	if (ins == 0) {
		return;
	}
	/* inserting replaces the selection */
	unsigned int s = selectionStart();
	unsigned int e = selectionEnd();

	if (e > s) {
		std::memmove(fld_->text + s, fld_->text + e, len - e + 1);
		fld_->caret = s;
		fld_->anchor = s;
		len = tlen(fld_->text);
	}
	if (len + ins >= sizeof(fld_->text)) {
		return;
	}
	std::memmove(fld_->text + fld_->caret + ins,
		     fld_->text + fld_->caret, len - fld_->caret + 1);
	std::memcpy(fld_->text + fld_->caret, utf8, ins);
	fld_->caret += ins;
	fld_->anchor = fld_->caret;
	setAccessibilityValue(fld_->text);
	setNeedsDisplay();
	valueChanged();
}

void
TextField::keyDown(const KeyEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	/* printable text (no ctrl/alt modifiers; real glyphs only —
	 * backspace/delete deliver control chars in e.chars) types at
	 * the caret */
	if (e.chars[0] &&
	    !(e.modifiers & (ARGENTUM_MOD_CTRL | ARGENTUM_MOD_ALT)) &&
	    (unsigned char) e.chars[0] >= 0x20 &&
	    (unsigned char) e.chars[0] != 0x7f) {
		char buf[8] = { 0 };

		std::strncpy(buf, e.chars, sizeof(buf) - 1);
		insertAtCaret(buf);
		return;
	}
	bool shift = (e.modifiers & ARGENTUM_MOD_SHIFT) != 0;

	switch (e.keysym) {
	case 0xff08: {		/* BackSpace */
		unsigned int s = selectionStart();
		unsigned int en = selectionEnd();

		if (en > s) {
			deleteRange(s, en);
		} else if (fld_->caret > 0) {
			unsigned int p = prevCharStart(fld_->text, fld_->caret);

			deleteRange(p, fld_->caret);
		}
		break;
	}
	case 0xffff: {		/* Delete */
		unsigned int s = selectionStart();
		unsigned int en = selectionEnd();

		if (en > s) {
			deleteRange(s, en);
		} else {
			unsigned int n = nextCharEnd(fld_->text, fld_->caret);

			deleteRange(fld_->caret, n);
		}
		break;
	}
	case 0xff51:		/* Left */
		if (selectionEnd() > selectionStart() && !shift) {
			setSelection(selectionStart(), selectionStart());
		} else if (fld_->caret > 0) {
			unsigned int p = prevCharStart(fld_->text, fld_->caret);

			fld_->caret = p;
			if (!shift) {
				fld_->anchor = fld_->caret;
			}
			setNeedsDisplay();
		}
		break;
	case 0xff53:		/* Right */
		if (selectionEnd() > selectionStart() && !shift) {
			setSelection(selectionEnd(), selectionEnd());
		} else {
			unsigned int n = nextCharEnd(fld_->text, fld_->caret);

			if (n > fld_->caret) {
				fld_->caret = n;
				if (!shift) {
					fld_->anchor = fld_->caret;
				}
				setNeedsDisplay();
			}
		}
		break;
	case 0xff50:		/* Home */
		fld_->caret = 0;
		if (!shift) {
			fld_->anchor = 0;
		}
		setNeedsDisplay();
		break;
	case 0xff57:		/* End */
		fld_->caret = tlen(fld_->text);
		if (!shift) {
			fld_->anchor = fld_->caret;
		}
		setNeedsDisplay();
		break;
	default:
		Control::keyDown(e);	/* bubble (Return etc.) */
		break;
	}
}

void
TextField::mouseDown(const MouseEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	fld_->caret = indexAtX(e.x);
	fld_->anchor = fld_->caret;
	setNeedsDisplay();
}

/* caret index for a click at local pt x (whole-char boundaries only) */
unsigned int
TextField::indexAtX(double localPt) const
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	double x = localPt * ppt - 3.0;	/* px past the 3px pad */

	if (x <= 0) {
		return 0;
	}
	unsigned int len = tlen(fld_->text);
	unsigned int i = 0;
	unsigned int best = 0;
	char buf[256];

	while (i < len) {
		unsigned int nb = nextCharEnd(fld_->text, i);

		if (nb <= i || nb >= sizeof(buf)) {
			break;
		}
		std::memcpy(buf, fld_->text, nb);
		buf[nb] = 0;
		double pw = advancePt(theme.fontFamily(), theme.fontSizePt(),
				      buf) * ppt;

		if (x < pw) {
			break;
		}
		best = nb;
		i = nb;
	}
	return best;
}

void
TextField::valueChanged()
{
}

/* ---- drawing ---------------------------------------------------- */

void
TextField::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	Rect f = frame();
	int w = pxi(f.size.w, ppt);
	int h = pxi(f.size.h, ppt);
	Theme::Params p = theme.state(state());
	int r = pxi(theme.smallRadius(), ppt);
	int outline = pxi(theme.outline(), ppt);

	if (outline < 1) {
		outline = 1;
	}
	if (r < 1) {
		r = 1;
	}
	if (r > h / 2) {
		r = h / 2;
	}
	/* bezel: state outline ring + page interior */
	g.fillRoundedRect(0, 0, (unsigned) w, (unsigned) h,
			  (unsigned) r, p.outline);
	int o = outline > h / 2 ? h / 2 : outline;
	int ri = r > o ? r - o : 0;

	g.fillRoundedRect(o, o, (unsigned) (w - 2 * o),
			  (unsigned) (h - 2 * o), (unsigned) ri,
			  theme.page());

	const char *family = theme.fontFamily();
	double sizePt = theme.fontSizePt();
	const char *text = fld_->text;
	unsigned int len = tlen(text);
	/* text box height (pt) from a sample glyph */
	TextMetrics m = textMetrics(family, sizePt, "Ag");
	double boxHpt = m.ascentPt + m.descentPt + 2.0 / ppt;
	int yTop = (int) ((f.size.h - boxHpt) / 2.0 * ppt);

	if (yTop < 0) {
		yTop = 0;
	}
	int padx = 3;

	/* split the text into pre / selection / post */
	unsigned int s = selectionStart();
	unsigned int e = selectionEnd();
	bool hasSel = isEnabled() && e > s && e <= len;
	unsigned int preEnd = hasSel ? s : len;
	unsigned int postStart = hasSel ? e : len;
	char pre[256], selc[256], post[256];

	pre[0] = 0;
	selc[0] = 0;
	post[0] = 0;
	if (preEnd < sizeof(pre)) {
		std::memcpy(pre, text, preEnd);
		pre[preEnd] = 0;
	}
	if (hasSel && e - s < sizeof(selc)) {
		std::memcpy(selc, text + s, e - s);
		selc[e - s] = 0;
	}
	if (len - postStart < sizeof(post)) {
		std::memcpy(post, text + postStart, len - postStart);
		post[len - postStart] = 0;
	}

	double xPre = padx;
	double xSel = xPre + advancePt(family, sizePt, pre) * ppt;
	double xPost = xSel + (hasSel ? advancePt(family, sizePt, selc)
				     : 0.0) * ppt;

	if (pre[0]) {
		g.drawText(family, sizePt, (int) xPre, yTop, pre,
			   theme.text());
	}
	if (hasSel && selc[0]) {
		int x0 = (int) (xSel - 0.5);
		int x1 = (int) (xPost + 0.5);
		int bh = (int) (boxHpt * ppt) + 2;

		if (x1 > x0 && bh > 0) {
			g.fillRect(x0, yTop - 1, (unsigned) (x1 - x0),
				   (unsigned) bh, theme.accent());
		}
		g.drawText(family, sizePt, (int) xSel, yTop, selc, 0xffffff);
	}
	if (post[0]) {
		g.drawText(family, sizePt, (int) xPost, yTop, post,
			   theme.text());
	}
	/* caret (visible while focused and no selection) */
	if (isEnabled() && focused() && !hasSel) {
		char before[256];
		unsigned int k = 0;

		if (fld_->caret < sizeof(before)) {
			for (k = 0; k < fld_->caret; k++) {
				before[k] = text[k];
			}
			before[k] = 0;
		} else {
			before[0] = 0;
		}
		double cx = xPre + advancePt(family, sizePt, before) * ppt;
		int bh = (int) (boxHpt * ppt) + 2;

		g.drawLine((int) cx, yTop - 1, (int) cx, yTop - 1 + bh,
			   theme.text());
	}
}

} /* namespace argentum */
