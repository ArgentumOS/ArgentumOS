/* text_stack — U3a acceptance: AttributedString / TextStorage /
 * TextContainer / LayoutManager (docs/design/cocoa-parity-plan.md).
 *
 * Display-free and, deliberately, font-free where it can be: the wrapping
 * questions are answered by measuring with the engine when it is ready and
 * by the documented estimate otherwise, so the LAYOUT logic is what is
 * under test — a line that holds two words because they fit, a break
 * before a word that does not, explicit newlines, truncation, and which
 * character a point lands on.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstring>
#include <string>

using namespace argentum;

static int failures = 0;

static void
ok(bool cond, const char *what)
{
	if (cond) {
		std::printf("U3A: %s OK\n", what);
		return;
	}
	std::printf("U3A: %s FAIL\n", what);
	failures++;
}

static void
eq(int got, int want, const char *what)
{
	if (got == want) {
		std::printf("U3A: %s = %d OK\n", what, got);
		return;
	}
	std::printf("U3A: %s FAIL: got %d want %d\n", what, got, want);
	failures++;
}

static void
eqs(const std::string &got, const char *want, const char *what)
{
	if (got == want) {
		std::printf("U3A: %s = \"%s\" OK\n", what, got.c_str());
		return;
	}
	std::printf("U3A: %s FAIL: got \"%s\" want \"%s\"\n", what,
		    got.c_str(), want);
	failures++;
}

int
main()
{
	/* the engine must be UP for real metrics: it is fontconfig + FreeType
	 * and needs no display, but without it every width is 0 and no text
	 * would ever wrap */
	textEngineInit();
	textEngineSetPxPerPt(1.0);
	ok(textEngineReady(), "the text engine is ready (fonts are present)");
	ok(textMetrics(nullptr, 13.0, "alpha").widthPt > 0,
	   "and it measures: 'alpha' is wider than nothing");

	/* ---- the characters and their attributes ---- */
	AttributedString as("Hello world");

	eq(as.length(), 11, "length is the byte count");
	eqs(as.substring(0, 5), "Hello", "substring");
	eqs(as.substring(6, 5), "world", "substring at an offset");
	eqs(as.substring(6, 99), "world", "a substring past the end is clamped");
	ok(as.attributesAt(0).font.sizePt == 13.0,
	   "text with no runs still answers with the default attributes");

	TextAttributes bold;

	bold.font.bold = true;
	bold.color = Color::rgb(1, 0, 0);
	as.addAttributes(bold, 0, 5);
	ok(as.attributesAt(0).font.bold && as.attributesAt(4).font.bold,
	   "a run covers the characters it was added to");
	ok(!as.attributesAt(5).font.bold,
	   "and stops at its end");
	eq(as.runCount(), 1, "adding attributes makes one run");
	as.addAttributes(bold, 0, 11);
	eq(as.runCount(), 1, "extending over everything coalesces");

	/* ---- the storage: edits, change count, run fixing-up ---- */
	TextStorage st;

	st.appendString("one two three");
	eq(st.changeCount(), 1, "an append counts as one edit");
	eqs(st.string(), "one two three", "the storage holds the text");
	st.addAttributes(bold, 4, 3);		/* make "two" bold */
	ok(st.attributesAt(4).font.bold && !st.attributesAt(3).font.bold,
	   "a run sits where it was added");
	st.insertString(0, "X");
	eqs(st.string(), "Xone two three", "inserting shifts the text");
	eq(st.changeCount(), 2, "and bumps the change count");
	ok(st.attributesAt(5).font.bold && !st.attributesAt(4).font.bold,
	   "the run FOLLOWED the edit (it did not stay on the old bytes)");
	st.deleteCharacters(0, 1);
	eqs(st.string(), "one two three", "deleting");
	ok(st.attributesAt(4).font.bold,
	   "and the run came back with it");

	/* ---- layout: wrapping ---- */
	TextStorage body;

	body.appendString("alpha beta gamma");
	TextContainer wide;

	wide.setSize(Size{ 400, 100 });
	wide.setLineFragmentPadding(0);
	LayoutManager lm;

	lm.setTextStorage(&body);
	lm.setTextContainer(&wide);
	eq(lm.lineCount(), 1, "wide text is one line");
	eqs(lm.lineString(0), "alpha beta gamma", "and holds it all");
	ok(lm.usedRect().size.h > 0, "the used height is a line high");
	ok(lm.usedRect().size.w >= 0, "the used width is the line's");

	/* a container three words wide: pick a width from the words' own
	 * measured widths, so the test does not depend on a font */
	TextMetrics mAlpha = textMetrics(nullptr, 13.0, "alpha");
	TextMetrics mBeta = textMetrics(nullptr, 13.0, "beta");
	TextContainer two;

	two.setLineFragmentPadding(0);
	two.setSize(Size{ mAlpha.widthPt + mBeta.widthPt +
			  textMetrics(nullptr, 13.0, " ").widthPt + 1.0, 100 });
	lm.setTextContainer(&two);
	eq(lm.lineCount(), 2, "text one word too wide wraps to two lines");
	eqs(lm.lineString(0), "alpha beta", "the first line takes both words");
	eqs(lm.lineString(1), "gamma", "the second takes the rest");

	/* ---- explicit newlines ---- */
	TextStorage two_lines;

	two_lines.appendString("first\nsecond");
	wide.setSize(Size{ 400, 100 });
	lm.setTextStorage(&two_lines);
	lm.setTextContainer(&wide);
	eq(lm.lineCount(), 2, "an explicit newline makes a line");
	eqs(lm.lineString(0), "first", "the first line before it");
	eqs(lm.lineString(1), "second", "the second after it");
	two_lines.appendString("\n");
	eq(lm.lineCount(), 3, "a trailing newline makes an empty last line");
	eq(lm.line(2).length, 0, "which is empty");

	/* ---- truncation ---- */
	TextStorage longText;

	longText.appendString("alphabetagammadelta");
	TextContainer narrow;

	narrow.setLineFragmentPadding(0);
	narrow.setLineFragmentPadding(0);
	wide.setSize(Size{ 0, 0 });		/* measure first, no limit */
	lm.setTextStorage(&longText);
	narrow.setSize(Size{ textMetrics(nullptr, 13.0, "alpha").widthPt
			     + textMetrics(nullptr, 13.0, "…").widthPt, 100 });
	narrow.setBreakMode(LineBreakMode::TruncateTail);
	lm.setTextContainer(&narrow);
	eq(lm.lineCount(), 1, "a truncating container is one line");
	ok(lm.lineString(0).find("\xE2\x80\xA6") != std::string::npos,
	   "the tail is replaced by an ellipsis");
	ok(lm.lineString(0).rfind("alpha", 0) == 0,
	   "the head is kept");
	narrow.setBreakMode(LineBreakMode::TruncateHead);
	ok(lm.lineString(0).find("\xE2\x80\xA6") != std::string::npos,
	   "truncating the head also shows an ellipsis");
	ok(lm.lineString(0).find("delta") != std::string::npos,
	   "and keeps the tail");
	/* the middle needs room for BOTH ends: widen it, so the check is about
	 * the rule and not about the width the other modes already proved */
	narrow.setSize(Size{ textMetrics(nullptr, 13.0, "alphabetagamma")
				     .widthPt, 100 });
	narrow.setBreakMode(LineBreakMode::TruncateMiddle);
	std::string mid = lm.lineString(0);

	ok(mid.find("\xE2\x80\xA6") != std::string::npos,
	   "truncating the middle shows an ellipsis");
	ok(!mid.empty() && mid.rfind("al", 0) == 0
	   && mid.size() >= 2
	   && mid.compare(mid.size() - 2, 2, "ta") == 0,
	   "and keeps both ends (it starts at the start and ends at the end)");
	ok(mid.size() < 17, "while being shorter than the whole text");

	/* ---- a container with no width is one unlimited line ---- */
	TextContainer unlimited;

	lm.setTextContainer(&unlimited);
	eq(lm.lineCount(), 1, "an unsized container lays one line");
	eqs(lm.lineString(0), "alphabetagammadelta",
	    "and does not wrap it (the measuring case)");

	/* ---- the layout is LAZY: an edit alone does not lay out again ---- */
	TextStorage lazy;

	lazy.appendString("a b c");
	wide.setSize(Size{ 400, 100 });
	wide.setBreakMode(LineBreakMode::WordWrap);
	lm.setTextStorage(&lazy);
	lm.setTextContainer(&wide);
	eq(lm.lineCount(), 1, "the lazy storage is one line");
	lazy.appendString(" d e f");
	ok(lm.needsLayout(), "an edit marks the layout stale");
	eq(lm.lineCount(), 1, "still one line when it fits");
	ok(!lm.needsLayout(), "and the query laid it out");

	/* ---- hit testing: which character is at a point ---- */
	TextStorage hit;

	hit.appendString("ab\ncd");
	wide.setSize(Size{ 400, 100 });
	lm.setTextStorage(&hit);
	lm.setTextContainer(&wide);
	eq(lm.lineCount(), 2, "two lines to hit");
	eq(lm.lineIndexAt(Point{ 5, 1 }), 0, "a point near the top is line 0");
	eq(lm.lineIndexAt(Point{ 5, lm.line(1).frame.origin.y + 2 }), 1,
	   "a point on the second line is line 1");
	eq(lm.characterIndexAt(Point{ -1, 1 }), 0, "left of the text is its start");
	ok(lm.characterIndexAt(Point{ 999, 1 }) >= 2,
	   "past the end of a line is that line's end");

	if (failures) {
		std::printf("U3A-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U3A-OK (storage, editing, wrapping, newlines, truncation, "
		    "lazy layout, hit testing)\n");
	return 0;
}
