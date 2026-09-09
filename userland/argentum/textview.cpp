/* argentum/textview.cpp — TXT-a of docs/design/argentum-textview.md:
 * the L7 rich view. A multi-line UTF-8 document laid out (hard \n
 * breaks + greedy word wrap at the content width) and drawn line by
 * line in the theme font. v1 text is plain (no attributes/runs) and
 * the view is read-only until TXT-b adds the document editing
 * surface; TXT-c adds the ScrollView integration.
 *
 * Layout: the document is split into hard lines at '\n'; each hard
 * line is greedy word-wrapped at the content width (a space is the
 * only break opportunity; a single word wider than the width
 * character-wraps as a fallback). The layout memoizes the visual
 * lines as byte ranges over the document and is recomputed when the
 * text or the frame width changes. A11y role TextArea; the value
 * mirrors the document.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstring>
#include <string>

namespace argentum {

/* ---- layout helpers --------------------------------------------- */

static bool
isSpace(char c)
{
	return c == ' ';
}

#include <cstdio>

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

/* ---- drawing ---------------------------------------------------- */

void
TextView::draw(GraphicsContext &g)
{
	Application &app = Application::shared();
	Theme &theme = app.theme();
	double ppt = app.pxPerPt();

	relayout();

	/* the line box: ascent + descent + 4 pt leading, px-rounded */
	TextMetrics m = textMetrics(theme.fontFamily(),
				    theme.fontSizePt(), "Ag");
	double lineBoxPx = (m.ascentPt + m.descentPt + 4.0) * ppt + 0.5;
	unsigned int n = (unsigned int) tv_->lines.size();

	for (unsigned int i = 0; i < n; i++) {
		TextViewLine v = tv_->lines[i];
		char buf[2048];
		unsigned int len = v.end - v.start;

		if (len == 0) {
			continue;	/* an empty line draws nothing */
		}
		if (len >= sizeof(buf)) {
			len = sizeof(buf) - 1;
		}
		std::memcpy(buf, tv_->text.c_str() + v.start, len);
		buf[len] = 0;
		int yTop = 2 + (int) (i * lineBoxPx);

		g.drawText(theme.fontFamily(), theme.fontSizePt(), 4,
			   yTop, buf, theme.text());
	}
}

} /* namespace argentum */
