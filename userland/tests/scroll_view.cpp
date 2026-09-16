/*
 * U5 acceptance (docs/design/cocoa-parity-plan.md): the ScrollView.
 *
 * DISPLAY-FREE, like the U0 probes and stack_view: a scroll view's whole
 * contract is arithmetic over sizes plus one drawing offset, so it needs no
 * window - build one at a known size, put a document of a known size in it,
 * scroll, and read the numbers back.
 *
 * WHAT IS ASSERTED HERE IS THE LIBRARY'S, NOT AN INPUT PATH'S:
 *
 *   - SCROLLING IS AN OFFSET, NOT A FRAME. The document view's frame must
 *     come out of every scroll EXACTLY as it went in; only the clip view's
 *     offset moves. That is the property the whole design rests on (a
 *     constrained document view keeps being laid out while it is scrolled),
 *     and it is why this file checks the frame on EVERY scroll rather than
 *     once at the start.
 *   - THE OFFSET IS CLAMPED, and by the SIZE DIFFERENCE: never negative,
 *     never past content-minus-hole.
 *   - THE BAR IS DERIVED FROM THE SAME NUMBERS: its knob's length is the
 *     visible share, its position is the offset's share of the range, and
 *     partAt() agrees with knobRect() about where the knob is (one
 *     arithmetic for drawing and for hitting, so a click cannot land
 *     anywhere the knob is not drawn).
 *   - THE WHEEL MOVES THE OFFSET AGAINST THE DELTA, and declines when there
 *     is nothing to scroll so an enclosing scroll view gets its turn.
 *
 *   scroll_view
 *     prints  U5B-OK   (what was checked)
 *     or      U5B-FAIL (n)   and exits 1
 */
#include <argentum/argentum.h>

#include <cstdio>

using namespace argentum;

static int failures = 0;

static void
ok(bool cond, const char *what)
{
	if (cond) {
		std::printf("U5B-OK %s\n", what);
		return;
	}
	failures++;
	std::printf("U5B-FAIL %s\n", what);
}

static bool
near(double a, double b)
{
	double d = a - b;

	return d < 0.01 && d > -0.01;
}

/* A scroll view of 200x200 looking at a document of 500x600, both bars on:
 * 185x185 visible inside the classic 15pt bars, so the range is 315 and 415 -
 * two different numbers, which keeps a mix-up of the axes visible. */
struct Fixture {
	ScrollView sv;
	View doc;

	Fixture()
	{
		sv.setFrame(Rect{ { 40, 30 }, { 200, 200 } });
		sv.setHasVerticalScroller(true);
		sv.setHasHorizontalScroller(true);
		sv.setLineScroll(16);
		doc.setFrame(Rect{ { 0, 0 }, { 500, 600 } });
		sv.setDocumentView(&doc);
		/* the layout pass a window would run before drawing */
		sv.layoutSubtreeIfNeeded();
	}
};

int
main()
{
	/* ---- the hole and the content ---- */
	{
		Fixture f;

		/* the bars come out of the hole: 200 less a 15pt bar */
		ok(near(f.sv.visibleSize().w, 185) && near(f.sv.visibleSize().h, 185),
		   "the visible area is the scroll view less the bars it shows");
		ok(near(f.sv.contentSize().w, 500) && near(f.sv.contentSize().h, 600),
		   "the content is the DOCUMENT's size, not the hole's");
		ok(near(f.sv.contentOffset().x, 0) && near(f.sv.contentOffset().y, 0),
		   "a fresh scroll view looks at the top of its content");

		/* and the bars know it: 185/500 and 185/600 */
		ok(near(f.sv.contentView()->contentOffset().x, 0)
		   && near(f.sv.contentView()->contentOffset().y, 0),
		   "the offset lives on the CLIP VIEW, which starts at zero");
	}

	/* ---- THE INVARIANT: scrolling moves the view of the content, never
	 * the content's frame ---- */
	{
		Fixture f;
		Rect before = f.doc.frame();

		f.sv.setContentOffset(Point{ 200, 300 });
		ok(near(f.sv.contentOffset().x, 200)
		   && near(f.sv.contentOffset().y, 300),
		   "setContentOffset moves the offset it is given");
		ok(f.doc.frame().origin.x == before.origin.x
		   && f.doc.frame().origin.y == before.origin.y
		   && f.doc.frame().size.w == before.size.w
		   && f.doc.frame().size.h == before.size.h,
		   "and the DOCUMENT's frame is untouched by the scroll");

		/* the same, driven the way the wheel drives it */
		f.sv.scrollBy(Point{ 50, 50 });
		ok(near(f.sv.contentOffset().x, 250)
		   && near(f.sv.contentOffset().y, 350),
		   "scrollBy is relative to where the view already is");
		ok(f.doc.frame().origin.x == before.origin.x
		   && f.doc.frame().origin.y == before.origin.y,
		   "and still the document has not moved");
	}

	/* ---- the clamp: the range is content minus hole, and nothing else ---- */
	{
		Fixture f;

		f.sv.setContentOffset(Point{ -100, -100 });
		ok(near(f.sv.contentOffset().x, 0) && near(f.sv.contentOffset().y, 0),
		   "an offset before the content clamps to its start");

		f.sv.setContentOffset(Point{ 9999, 9999 });
		ok(near(f.sv.contentOffset().x, 315)
		   && near(f.sv.contentOffset().y, 415),
		   "and past the end clamps to the range: 500-185 and 600-185");
	}

	/* ---- nothing to scroll: clamped flat, and the wheel says so ---- */
	{
		ScrollView sv;
		View small;

		sv.setFrame(Rect{ { 0, 0 }, { 200, 200 } });
		small.setFrame(Rect{ { 0, 0 }, { 100, 100 } });
		sv.setDocumentView(&small);
		sv.layoutSubtreeIfNeeded();
		sv.setContentOffset(Point{ 40, 40 });
		ok(near(sv.contentOffset().x, 0) && near(sv.contentOffset().y, 0),
		   "content that fits leaves nothing to scroll");

		Event e = Event::otherEvent(EventType::ScrollWheel, Point{ 10, 10 },
					    0, 0.0, 0);

		e.setScrollDeltas(0, 1, 0, 1);
		ok(!sv.scrollWheel(e),
		   "and the wheel DECLINES, so an enclosing scroll view gets it");
	}

	/* ---- the wheel, when there IS something to scroll ---- */
	{
		Fixture f;
		Event down = Event::otherEvent(EventType::ScrollWheel,
					       Point{ 10, 10 }, 0, 0.0, 0);

		down.setScrollDeltas(0, -1, 0, -1);	/* rolled down */
		ok(f.sv.scrollWheel(down), "the wheel is handled when there is range");
		ok(near(f.sv.contentOffset().y, 16),
		   "one notch down moves the offset one line (16) down");

		Event up = Event::otherEvent(EventType::ScrollWheel,
					     Point{ 10, 10 }, 0, 0.0, 0);

		up.setScrollDeltas(0, 1, 0, 1);		/* rolled up */
		f.sv.scrollWheel(up);
		ok(near(f.sv.contentOffset().y, 0),
		   "and a notch up brings it back");

		/* ACROSS is the other axis, and only the other axis. The deltas
		 * read the way the wheel was rolled and the offset moves AGAINST
		 * them, so a notch to the LEFT (delta -1) advances the offset. A
		 * notch the other way from offset 0 clamps flat — which is what
		 * the first version of this check mistook for an axis that does
		 * not move. */
		Event across = Event::otherEvent(EventType::ScrollWheel,
						 Point{ 10, 10 }, 0, 0.0, 0);

		across.setScrollDeltas(-1, 0, -1, 0);
		f.sv.scrollWheel(across);
		ok(near(f.sv.contentOffset().x, 16)
		   && near(f.sv.contentOffset().y, 0),
		   "a horizontal notch moves x and leaves y alone");
	}

	/* ---- the bar's geometry, from the same numbers ---- */
	{
		Fixture f;

		f.sv.setContentOffset(Point{ 0, 0 });
		Scroller *v = nullptr;

		/* the vertical bar is the scroll view's second child (the clip
		 * view is the first) */
		for (View *c : f.sv.subviews()) {
			Scroller *s = dynamic_cast<Scroller *>(c);

			if (s && s->orientation() == ScrollerOrientation::Vertical) {
				v = s;
			}
		}
		ok(v != nullptr, "the scroll view has a vertical bar to ask");
		if (!v) {
			std::printf("U5B-FAIL (%d)\n", failures);
			return 1;
		}
		/* the bar is as tall as the hole: 200 less the horizontal bar */
		ok(near(v->frame().size.h, 185) && near(v->frame().size.w, 15),
		   "the bar is 15 across and as tall as the visible area");
		ok(near(v->knobProportion(), 185.0 / 600.0),
		   "the knob's length is the visible SHARE of the content");
		ok(near(v->doubleValue(), 0), "at the top, the knob is at the start");

		Rect track = v->trackRect();
		Rect knob0 = v->knobRect();

		ok(near(track.origin.y, 15)
		   && near(track.size.h, 185 - 30),
		   "the track is what is left between the two 15pt arrows");
		ok(near(knob0.origin.y, track.origin.y),
		   "and at the top the knob sits at the track's start");

		/* scrolled to the end, the knob is at the other end: one
		 * arithmetic for the position, so the travel is exact */
		f.sv.setContentOffset(Point{ 0, 415 });
		ok(near(v->doubleValue(), 1),
		   "scrolled to the end, the bar reads 1");
		Rect knob1 = v->knobRect();

		ok(near(knob1.origin.y + knob1.size.h,
			track.origin.y + track.size.h),
		   "and the knob's far end lands on the track's far end");

		/* THE ONE ARITHMETIC: what is drawn is what is hit */
		Point inKnob = { v->frame().size.w / 2,
				 knob1.origin.y + knob1.size.h / 2 };

		ok(v->partAt(inKnob) == ScrollerPart::Knob,
		   "a point in the knob's middle is the KNOB");
		ok(v->partAt(Point{ v->frame().size.w / 2, 7 })
		   == ScrollerPart::DecrementArrow,
		   "a point in the top button is the DECREMENT arrow");
		ok(v->partAt(Point{ v->frame().size.w / 2,
				    v->frame().size.h - 7 })
		   == ScrollerPart::IncrementArrow,
		   "and in the bottom button, the INCREMENT arrow");
		ok(v->partAt(Point{ v->frame().size.w / 2, track.origin.y + 2 })
		   == ScrollerPart::DecrementPage,
		   "track above the knob pages backwards");
		/* and the mirror of it: with the knob at the START, the track
		 * BELOW it is the forward page. (With the knob at the end it is
		 * not — there the knob IS the bottom of the track, which is the
		 * case just above; assuming otherwise is what the first version
		 * of this check got wrong.) */
		f.sv.setContentOffset(Point{ 0, 0 });
		ok(v->partAt(Point{ v->frame().size.w / 2,
				    track.origin.y + track.size.h - 2 })
		   == ScrollerPart::IncrementPage,
		   "and with the knob at the start, track below it pages on");
	}

	/* ---- an arrow click and a page, through the bar ---- */
	{
		Fixture f;
		Scroller *v = nullptr;

		for (View *c : f.sv.subviews()) {
			Scroller *s = dynamic_cast<Scroller *>(c);

			if (s && s->orientation() == ScrollerOrientation::Vertical) {
				v = s;
			}
		}
		if (!v) {
			std::printf("U5B-FAIL (%d)\n", failures);
			return 1;
		}
		/* the bar acts THROUGH its scroll view: the step is the view's
		 * (lineScroll), not the bar's */
		f.sv.scrollerPartPressed(v, ScrollerPart::IncrementArrow);
		ok(near(f.sv.contentOffset().y, 16),
		   "the increment arrow moves one line (the VIEW's step)");
		f.sv.scrollerPartPressed(v, ScrollerPart::DecrementArrow);
		ok(near(f.sv.contentOffset().y, 0), "and the other arrow undoes it");

		f.sv.scrollerPartPressed(v, ScrollerPart::IncrementPage);
		ok(near(f.sv.contentOffset().y, 185 - 16),
		   "a page is the visible height less a line");
		/* the knob's own fraction: half way down the track is half way
		 * through the range */
		f.sv.scrollerKnobDragged(v, 0.5);
		ok(near(f.sv.contentOffset().y, 415 / 2.0),
		   "dragging the knob to half way scrolls half the range");

		/* AND THE DOCUMENT STILL HAS NOT MOVED — through all of it */
		ok(near(f.doc.frame().origin.x, 0)
		   && near(f.doc.frame().origin.y, 0),
		   "after arrows, a page and a knob drag, the document is where"
		   " it was");
	}

	/* ---- hit-testing follows the scroll, and stops at the hole ---- */
	{
		Fixture f;
		View *row = new View();

		row->setFrame(Rect{ { 0, 100 }, { 500, 50 } });
		f.doc.addSubview(row);

		/* NOTHING SCROLLED: 110 down the hole is 110 down the content,
		 * which is 10 into the row */
		ok(f.sv.hitTest(Point{ 10, 110 }) == row,
		   "a point in the hole lands on the content view under it");
		/* 50 down the hole is content y=50 — above the row */
		ok(f.sv.hitTest(Point{ 10, 50 }) != row,
		   "and a point above the row does not");

		/* SCROLLED DOWN 200, the content has moved UP behind the hole: the
		 * same point is now content y=310, past the row — so a scroll
		 * MOVES WHAT A CLICK LANDS ON, which is the whole reason the hit
		 * test reads the same offset the paint pass does. */
		f.sv.setContentOffset(Point{ 0, 200 });

		View *after = f.sv.hitTest(Point{ 10, 110 });

		ok(after == &f.doc,
		   "after a scroll the same point lands on the content but no"
		   " longer on the row the row has moved out from under it");

		/* AND THE HOLE IS THE BOUNDARY. The visible area is 185 tall and
		 * the horizontal bar has the next 15pt, so a point at y=190 is the
		 * BAR's — content scrolled up behind it is not reachable, however
		 * far off the top it is. */
		View *atBar = f.sv.hitTest(Point{ 10, 190 });

		ok(atBar != nullptr && atBar != &f.doc
		   && atBar != f.sv.contentView(),
		   "a point past the hole is not the content but the bar that is"
		   " there");
		ok(f.sv.hitTest(Point{ 190, 10 }) != f.sv.contentView(),
		   "and the same across the other bar");

		f.doc.removeFromSuperview();
		delete row;
	}

	if (failures) {
		std::printf("U5B-FAIL (%d)\n", failures);
		return 1;
	}
	std::printf("U5B-OK (the scroll view looks at content larger than it is: "
		    "the offset is clamped to the content less the hole, the "
		    "document's frame never moves, the bars are derived from the "
		    "same numbers, the wheel and the arrows scroll it, and the "
		    "hole clips the hits)\n");
	return 0;
}
