/* argentum/textview.cpp — the L7 rich view (docs/design/
 * argentum-textview.md). TXT-a: multi-line layout + read-only draw;
 * TXT-b: the document editing surface (click-to-position, typing with
 * Return = \n, cross-line arrows with a goal column, Shift selection,
 * selection rendering). v1 text is plain (no attributes/runs).
 *
 * The document is a std::string (\r\n normalized on setValue); the
 * caret/anchor are byte offsets with TextField's utf8-safe semantics
 * (shared helpers in text_utf8.h). The layout memoizes visual lines
 * as byte ranges; byte <-> (line, x) mappings come from it.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <argentum/text_utf8.h>

#include <cmath>
#include <cstring>
#include <string>

namespace argentum {

static bool
isSpace(char c)
{
	return c == ' ';
}

/* theme-font advance (pt) of text[start..end) of `text` */
static double
rangeWidthPt(const std::string &text, unsigned int start,
	     unsigned int end)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();
	unsigned int px = (unsigned int)
		((theme.fontSizePt() * ppt) + 0.5);

	if (px == 0) {
		return 0;
	}
	std::string seg = text.substr(start, end - start);
	TextRun *t = textRunPrepare(theme.fontFamily(), seg.c_str(), px,
				    true);

	if (!t) {
		return 0;
	}
	double pt = ((double) textRunAdvance26(t) / 64.0) / ppt;

	textRunFinish(t);
	return pt;
}

/* theme-font advance (px) of a null-terminated string */
static double
strAdvancePx(const char *utf8)
{
	if (!utf8 || !utf8[0]) {
		return 0;
	}
	Application &app = Application::shared();
	Theme &theme = app.theme();
	unsigned int px = (unsigned int)
		((theme.fontSizePt() * app.pxPerPt()) + 0.5);

	TextRun *t = textRunPrepare(theme.fontFamily(), utf8, px, true);
	if (!t) {
		return 0;
	}
	double a = (double) textRunAdvance26(t) / 64.0;

	textRunFinish(t);
	return a;
}

/* wrap one hard segment [b,e) (no '\n' inside) into visual lines.
 * A line = the longest prefix that fits, broken at a space; the
 * breaking space is consumed (the next line starts after it) and
 * trailing spaces are trimmed off each pushed line. */
static void
wrapSegment(const std::string &text, unsigned int b, unsigned int e,
	    double wrapPt,
	    std::vector<TextViewLine> &lines)
{
	if (e <= b) {			/* an empty hard line */
		TextViewLine v = { b, b };
		lines.push_back(v);
		return;
	}
	unsigned int lineStart = b;

	while (lineStart < e) {
		unsigned int lineEnd = lineStart;
		unsigned int i = lineStart;

		/* grow the line across space break opportunities */
		while (i < e) {
			unsigned int sp = i;

			while (sp < e && !isSpace(text[sp])) {
				sp++;
			}
			if (sp == e) {
				/* no more spaces: the whole tail fits? */
				if (rangeWidthPt(text, lineStart, e)
				    <= wrapPt) {
					lineEnd = e;
				}
				break;
			}
			/* candidate = through this space (inclusive) */
			if (rangeWidthPt(text, lineStart, sp + 1)
			    <= wrapPt) {
				lineEnd = sp + 1;
				i = sp + 1;
			} else {
				break;
			}
		}
		if (lineEnd == lineStart) {
			/* even the first word overflows: character-wrap
			 * it (prefix walk; only ever one pathological
			 * word) */
			unsigned int k = lineStart;

			while (k < e && !isSpace(text[k]) &&
			       rangeWidthPt(text, lineStart, k + 1)
			       <= wrapPt) {
				k++;
			}
			if (k == lineStart) {
				k = lineStart + 1;	/* always progress */
			}
			TextViewLine v = { lineStart, k };

			lines.push_back(v);
			lineStart = k;
			continue;
		}
		/* trim the trailing (breaking) space off the line */
		unsigned int end = lineEnd;

		while (end > lineStart && isSpace(text[end - 1])) {
			end--;
		}
		TextViewLine v = { lineStart, end };

		lines.push_back(v);
		if (lineEnd >= e) {
			break;		/* the whole hard line fit */
		}
		lineStart = lineEnd;	/* consumed the breaking space */
	}
}

/* ---- state ------------------------------------------------------ */

TextView::TextView()
	: tv_(new Impl())
{
	setAccessibilityRole(AccessibilityRole::TextArea);
}

TextView::~TextView()
{
	delete tv_;
}

void
TextView::setValue(const char *utf8)
{
	tv_->text.clear();
	if (utf8) {
		/* normalize \r\n and \r to \n */
		for (const char *p = utf8; *p; p++) {
			if (*p == '\r') {
				if (p[1] == '\n') {
					p++;
				}
				tv_->text += '\n';
			} else {
				tv_->text += *p;
			}
		}
	}
	tv_->dirty = true;
	tv_->caret = (unsigned int) tv_->text.size();
	tv_->anchor = tv_->caret;
	tv_->goalXPx = -1.0;
	setAccessibilityValue(tv_->text.c_str());
	setNeedsDisplay();
	valueChanged();
}

const char *
TextView::value() const
{
	return tv_->text.c_str();
}

void
TextView::valueChanged()
{
}

unsigned int
TextView::caretIndex() const
{
	return tv_->caret;
}

unsigned int
TextView::selectionStart() const
{
	unsigned int a = tv_->anchor;
	unsigned int c = tv_->caret;

	return a < c ? a : c;
}

unsigned int
TextView::selectionEnd() const
{
	unsigned int a = tv_->anchor;
	unsigned int c = tv_->caret;

	return a > c ? a : c;
}

void
TextView::setSelection(unsigned int start, unsigned int end)
{
	tv_->anchor = start;
	tv_->caret = end;
	tv_->goalXPx = -1.0;
	setNeedsDisplay();
}

/* ---- layout ----------------------------------------------------- */

void
TextView::relayout()
{
	Application &app = Application::shared();
	double wrapPt = frame().size.w - 8.0 / app.pxPerPt();

	if (!tv_->dirty && std::fabs(wrapPt - tv_->wrapPt) < 0.25) {
		return;
	}
	tv_->lines.clear();
	if (wrapPt < 4.0) {
		wrapPt = 1e9;		/* no wrapping at all */
	}
	/* split into hard lines at '\n' */
	size_t n = tv_->text.size();
	size_t h0 = 0;

	while (h0 <= n) {
		size_t h1 = tv_->text.find('\n', h0);

		if (h1 == std::string::npos) {
			h1 = n;
		}
		wrapSegment(tv_->text, (unsigned int) h0,
			    (unsigned int) h1, wrapPt, tv_->lines);
		if (h1 == n) {
			break;
		}
		h0 = h1 + 1;
	}
	if (n == 0) {
		TextViewLine v = { 0, 0 };
		tv_->lines.push_back(v);
	}
	tv_->dirty = false;
	tv_->wrapPt = wrapPt;
}

unsigned int
TextView::lineCount()
{
	relayout();
	return (unsigned int) tv_->lines.size();
}

bool
TextView::lineText(unsigned int i, char *dst, unsigned int cap)
{
	if (i >= lineCount() || !dst || cap == 0) {
		return false;
	}
	TextViewLine v = tv_->lines[i];
	unsigned int len = v.end - v.start;

	if (len >= cap) {
		len = cap - 1;
	}
	std::memcpy(dst, tv_->text.c_str() + v.start, len);
	dst[len] = 0;
	return true;
}

/* ---- caret/geometry helpers ------------------------------------- */

/* the line box height in px (ascent + descent + 4 pt leading) */
static double
lineBoxPx()
{
	Application &app = Application::shared();
	Theme &theme = app.theme();

	return (textMetrics(theme.fontFamily(), theme.fontSizePt(), "Ag")
		.ascentPt + textMetrics(theme.fontFamily(),
					theme.fontSizePt(), "Ag")
		.descentPt + 4.0) * app.pxPerPt() + 0.5;
}

/* index of the visual line holding `byte` (clamped); *xPx = the text
 * x of the caret at that byte within the line (4px inset included) */
static unsigned int
lineForByte(const std::string &text,
	    const std::vector<TextViewLine> &lines, unsigned int byte,
	    double *xPx)
{
	unsigned int n = (unsigned int) lines.size();
	unsigned int li = 0;

	if (n == 0) {
		if (xPx) {
			*xPx = 4.0;
		}
		return 0;
	}
	for (unsigned int i = 0; i < n; i++) {
		if (lines[i].start <= byte) {
			li = i;
		} else {
			break;
		}
	}
	TextViewLine v = lines[li];
	unsigned int b = byte;

	if (b > v.end) {
		b = v.end;
	}
	if (xPx) {
		*xPx = 4.0 +
			strAdvancePx(text.substr(v.start,
						 b - v.start).c_str());
	}
	return li;
}

/* byte index for a text x (px, from the view's left edge) on line li */
static unsigned int
byteAtLineX(const std::string &text,
	    const std::vector<TextViewLine> &lines, unsigned int li,
	    double xPx)
{
	TextViewLine v = lines[li];
	double x = xPx - 4.0;		/* past the text inset */

	if (x <= 0) {
		return v.start;
	}
	unsigned int i = v.start;
	unsigned int best = v.start;

	while (i < v.end) {
		unsigned int nb = nextCharEnd(text.c_str(), i);

		if (nb > v.end) {
			nb = v.end;
		}
		if (nb <= i) {
			break;
		}
		std::string seg = text.substr(v.start, nb - v.start);

		if (x < strAdvancePx(seg.c_str())) {
			break;
		}
		best = nb;
		i = nb;
	}
	return best;
}

/* byte index for a click at local pt (x, y) inside the view */
unsigned int
TextView::indexAt(const double xPt, const double yPt)
{
	relayout();
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	double yPx = yPt * ppt;
	double box = lineBoxPx();
	unsigned int n = (unsigned int) tv_->lines.size();
	int li = (int) ((yPx - 2.0) / box);

	if (li < 0) {
		li = 0;
	}
	if (li >= (int) n) {
		li = (int) n - 1;
	}
	return byteAtLineX(tv_->text, tv_->lines, (unsigned int) li, xPt * ppt);
}

/* ---- editing ---------------------------------------------------- */

void
TextView::deleteRange(unsigned int start, unsigned int end)
{
	if (end <= start) {
		return;
	}
	unsigned int len = (unsigned int) tv_->text.size();

	if (start >= len) {
		return;
	}
	if (end > len) {
		end = len;
	}
	tv_->text.erase(start, end - start);
	tv_->caret = start;
	tv_->anchor = start;
	tv_->dirty = true;
	tv_->goalXPx = -1.0;
	setAccessibilityValue(tv_->text.c_str());
	setNeedsDisplay();
	valueChanged();
	scrollCaretToVisible();
}

void
TextView::insertAtCaret(const char *utf8)
{
	unsigned int ins = utf8 ? (unsigned int) std::strlen(utf8) : 0;

	if (ins == 0) {
		return;
	}
	/* inserting replaces the selection */
	unsigned int s = selectionStart();
	unsigned int e = selectionEnd();

	if (e > s) {
		tv_->text.erase(s, e - s);
		tv_->caret = s;
		tv_->anchor = s;
	}
	tv_->text.insert(tv_->caret, utf8, ins);
	tv_->caret += ins;
	tv_->anchor = tv_->caret;
	tv_->dirty = true;
	tv_->goalXPx = -1.0;
	setAccessibilityValue(tv_->text.c_str());
	setNeedsDisplay();
	valueChanged();
	scrollCaretToVisible();
}

void
TextView::keyDown(const KeyEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	/* printable text (no ctrl/alt modifiers; real glyphs only —
	 * backspace/delete deliver control chars in e.chars) types at
	 * the caret, replacing any selection */
	if (e.chars[0] &&
	    !(e.modifiers & (ARGENTUM_MOD_CTRL | ARGENTUM_MOD_ALT)) &&
	    (unsigned char) e.chars[0] >= 0x20 &&
	    (unsigned char) e.chars[0] != 0x7f) {
		insertAtCaret(e.chars);
		return;
	}
	/* Return / keypad-Enter insert a line break (a document editor
	 * has no end-edit commit) */
	if (e.keysym == 0xff0d || e.keysym == 0xff8d) {
		insertAtCaret("\n");
		return;
	}
	bool shift = (e.modifiers & ARGENTUM_MOD_SHIFT) != 0;
	bool moved = false;

	switch (e.keysym) {
	case 0xff08: {		/* BackSpace */
		unsigned int s = selectionStart();
		unsigned int en = selectionEnd();

		if (en > s) {
			deleteRange(s, en);
		} else if (tv_->caret > 0) {
			deleteRange(prevCharStart(tv_->text.c_str(),
						 tv_->caret),
				    tv_->caret);
		}
		break;
	}
	case 0xffff: {		/* Delete */
		unsigned int s = selectionStart();
		unsigned int en = selectionEnd();

		if (en > s) {
			deleteRange(s, en);
		} else if (tv_->caret < tv_->text.size()) {
			deleteRange(tv_->caret,
				    nextCharEnd(tv_->text.c_str(),
						tv_->caret));
		}
		break;
	}
	case 0xff51:		/* Left */
		if (selectionEnd() > selectionStart() && !shift) {
			setSelection(selectionStart(), selectionStart());
		} else if (tv_->caret > 0) {
			tv_->caret = prevCharStart(tv_->text.c_str(),
						   tv_->caret);
			if (!shift) {
				tv_->anchor = tv_->caret;
			}
			moved = true;
		}
		break;
	case 0xff53:		/* Right */
		if (selectionEnd() > selectionStart() && !shift) {
			setSelection(selectionEnd(), selectionEnd());
		} else if (tv_->caret < tv_->text.size()) {
			tv_->caret = nextCharEnd(tv_->text.c_str(),
						 tv_->caret);
			if (!shift) {
				tv_->anchor = tv_->caret;
			}
			moved = true;
		}
		break;
	case 0xff50:		/* Home: start of the visual line */
	case 0xff57: {		/* End: end of the visual line */
		relayout();
		double x;
		unsigned int li = lineForByte(tv_->text, tv_->lines, tv_->caret, &x);
		TextViewLine v = tv_->lines[li];
		unsigned int nb = (e.keysym == 0xff50) ? v.start : v.end;

		tv_->caret = nb;
		if (!shift) {
			tv_->anchor = nb;
		}
		moved = true;
		break;
	}
	case 0xff52:		/* Up */
	case 0xff54: {		/* Down */
		relayout();
		double gx = tv_->goalXPx;

		if (gx < 0.0) {
			double cx;
			lineForByte(tv_->text, tv_->lines, tv_->caret, &cx);
			gx = cx;
		}
		unsigned int n = (unsigned int) tv_->lines.size();
		double x;
		int li = (int) lineForByte(tv_->text, tv_->lines, tv_->caret, &x);
		int target = li +
			(e.keysym == 0xff52 ? -1 : 1);

		if (target >= 0 && target < (int) n) {
			tv_->caret = byteAtLineX(tv_->text, tv_->lines,
						 (unsigned int) target,
						 gx);
			if (!shift) {
				tv_->anchor = tv_->caret;
			}
			moved = true;
		}
		break;
	}
	default:
		Control::keyDown(e);	/* bubble */
		return;
	}
	if (moved) {
		setNeedsDisplay();
		scrollCaretToVisible();
	}
}

void
TextView::mouseDown(const MouseEvent &e)
{
	if (!isEnabled()) {
		return;
	}
	tv_->caret = indexAt(e.x, e.y);
	tv_->anchor = tv_->caret;
	tv_->goalXPx = -1.0;
	setNeedsDisplay();
	scrollCaretToVisible();
}

void
TextView::scrollCaretToVisible()
{
	relayout();
	if (tv_->lines.empty()) {
		return;
	}
	double x;
	unsigned int li = lineForByte(tv_->text, tv_->lines, tv_->caret,
				      &x);
	Application &app = Application::shared();
	double ppt = app.pxPerPt();
	double box = lineBoxPx();
	double topPt = (2.0 + (double) li * box) / ppt;
	double hPt = box / ppt;

	for (View *s = superview(); s; s = s->superview()) {
		ScrollView *sv = dynamic_cast<ScrollView *>(s);

		if (sv) {
			Rect r = { {0, topPt},
				   {frame().size.w, hPt} };

			sv->scrollRectToVisible(r);
			return;
		}
	}
}

/* ---- drawing ---------------------------------------------------- */

void
TextView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();

	relayout();

	double box = lineBoxPx();
	unsigned int n = (unsigned int) tv_->lines.size();
	unsigned int selS = selectionStart();
	unsigned int selE = selectionEnd();
	bool hasSel = isEnabled() && selE > selS;
	const char *family = theme.fontFamily();
	double sizePt = theme.fontSizePt();
	char buf[2048];

	for (unsigned int i = 0; i < n; i++) {
		TextViewLine v = tv_->lines[i];
		unsigned int len = v.end - v.start;
		int yTop = 2 + (int) (i * box);

		if (len == 0) {
			continue;	/* an empty line draws nothing */
		}
		if (len >= sizeof(buf)) {
			len = sizeof(buf) - 1;
		}
		std::memcpy(buf, tv_->text.c_str() + v.start, len);
		buf[len] = 0;
		/* this line's overlap with the selection */
		unsigned int aS = hasSel && selS > v.start ? selS : v.start;
		unsigned int aE = hasSel && selE < v.end ? selE : v.end;
		bool selOn = hasSel && aE > aS;
		unsigned int preLen = selOn ? aS - v.start : len;
		unsigned int selLen = selOn ? aE - aS : 0;
		char pre[2048], selc[2048];

		pre[0] = 0;
		selc[0] = 0;
		if (preLen < sizeof(pre)) {
			std::memcpy(pre, buf, preLen);
			pre[preLen] = 0;
		}
		if (selLen && selLen < sizeof(selc)) {
			std::memcpy(selc, buf + preLen, selLen);
			selc[selLen] = 0;
		}
		double xPre = 4.0;
		double xSel = xPre + strAdvancePx(pre);
		double xPost = xSel + strAdvancePx(selc);

		if (pre[0]) {
			g.drawText(family, sizePt, (int) xPre, yTop, pre,
				   theme.text());
		}
		if (selOn && selc[0]) {
			int x0 = (int) (xSel - 0.5);
			int x1 = (int) (xPost + 0.5);

			if (x1 > x0) {
				g.fillRect(x0, yTop - 1,
					   (unsigned) (x1 - x0),
					   (unsigned) (box + 2),
					   theme.accent());
			}
			g.drawText(family, sizePt, (int) xSel, yTop, selc,
				   0xffffff);
		}
		if (selOn && preLen + selLen < len) {
			g.drawText(family, sizePt, (int) xPost, yTop,
				   buf + preLen + selLen, theme.text());
		}
	}
	/* caret: a 1px bar on the caret's line while focused and no
	 * selection is showing */
	if (isEnabled() && focused() && !hasSel) {
		double cx;
		unsigned int li = lineForByte(tv_->text, tv_->lines, tv_->caret, &cx);

		if (li < n) {
			int yTop = 2 + (int) (li * box);

			g.drawLine((int) cx, yTop - 1, (int) cx,
				   yTop - 1 + (int) box, theme.text());
		}
	}
}

} /* namespace argentum */
