/*
 * Argentum UIKit — the foundation (2026-09 restart).
 *
 * The Cocoa-parity program (docs/design/cocoa-parity-plan.md) starts from
 * scratch: the previous view/control class layer was discarded, and this
 * header begins the new one at its lowest level — the geometry types
 * every class needs, and the TEXT ENGINE, which is platform work rather
 * than a view class (FreeType + HarfBuzz + fontconfig, with the face,
 * glyph and shaped-run caches).
 *
 * The view classes (View, Control, Window, the widgets) and the drawing
 * API are NOT here yet: they are rebuilt milestone by milestone, U0
 * (Auto Layout) first, each landing with its own gate. The engine's API
 * is shaped like Cocoa's text stack (NSTextStorage/NSLayoutManager will
 * sit on top of TextRun), so the layers above it can be faithful.
 */
#ifndef FNX_ARGENTUM_ARGENTUM_H
#define FNX_ARGENTUM_ARGENTUM_H

#include <cstdint>

namespace argentum {

/* ---- geometry (points; 1 pt = 1/72 in) ------------------------------ */

struct Point {
	double x = 0;
	double y = 0;
};

struct Size {
	double w = 0;
	double h = 0;
};

struct Rect {
	Point origin;			/* top-left in the parent's space */
	Size size;
};

/* ---- text metrics (points) ------------------------------------------ */

struct TextMetrics {
	double widthPt = 0;
	double ascentPt = 0;
	double descentPt = 0;
};

TextMetrics textMetrics(const char *family, double sizePt,
			const char *utf8, bool bold = false);

/* the horizontal pad a run box carries before its ink */
int textInkInsetPx(void);

/* ---- the text engine ------------------------------------------------ */

/* Initialise fontconfig + FreeType once (idempotent). The engine owns its
 * own handles; callers ask whether it is ready rather than reaching into
 * a session object. */
bool textEngineInit();
bool textEngineReady();
/* px per point: the session sets it once the display is known (the
 * 96 dpi fallback applies until then) */
void textEngineSetPxPerPt(double pxPerPt);
double textEnginePxPerPt();

/* A TextRun is single-shot: prepare, inspect, compose at most once,
 * finish. Runs and faces are cached for the life of the process. */
struct TextRun;

TextRun *textRunPrepare(const char *family, const char *utf8,
			unsigned int pixelSize, bool quiet = false,
			bool bold = false);
void textRunFinish(TextRun *t);
unsigned int textRunGlyphCount(const TextRun *t);
/* run box geometry (px) */
int textRunBoxW(const TextRun *t);
int textRunBoxH(const TextRun *t);
/* ascent (px above the baseline): the baseline sits textRunAscent() +
 * PADY below the box top */
int textRunAscent(const TextRun *t);
/* total 26.6 advance of the shaped run */
long textRunAdvance26(const TextRun *t);
/* rasterize every glyph fg-over-bg into an opaque RGB32 box
 * (boxW*boxH words, 0x00RRGGBB) */
unsigned int textRunComposeRgb(TextRun *t, std::uint32_t *box,
			       std::uint32_t fg, std::uint32_t bg);
/* rasterize every glyph's AA coverage into an A8 mask (boxW*boxH bytes) */
unsigned int textRunComposeMask(TextRun *t, unsigned char *cov);

} /* namespace argentum */

#endif /* FNX_ARGENTUM_ARGENTUM_H */
