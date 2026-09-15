/*
 * The text stack (U3): AttributedString / TextStorage / TextContainer /
 * LayoutManager (docs/design/cocoa-parity-plan.md).
 *
 * Cocoa's three-piece model in the same places: the storage holds the
 * characters and their attributes, the container says what region they
 * flow into and how they break, and the layout manager turns the two into
 * lines — lazily, because an edit should not cost a layout until someone
 * asks for one.
 *
 * Indices are UTF-8 BYTE offsets throughout (see the header's section
 * note): the engine shapes UTF-8, so a byte index is the one the engine
 * can use without a conversion.
 */
#include <argentum/argentum.h>

#include "argentum/text_utf8.h"

#include <algorithm>
#include <cstring>

namespace argentum {

/* ---- AttributedString ------------------------------------------------ */

AttributedString::AttributedString()
{
}

AttributedString::AttributedString(const char *utf8)
{
	setString(utf8);
}

AttributedString::~AttributedString()
{
}

void
AttributedString::setString(const char *utf8)
{
	text_ = utf8 ? utf8 : "";
	/* the runs described the OLD string, so they go with it */
	runs_.clear();
}

TextAttributes
AttributedString::attributesAt(int index) const
{
	for (const AttributeRun &r : runs_) {
		if (index >= r.location && index < r.location + r.length) {
			return r.attributes;
		}
	}
	return default_;
}

/* merge `a` over [location, location+length): split at the edges, then
 * give the covered span a single run */
void
AttributedString::addAttributes(const TextAttributes &a, int location,
				int length)
{
	if (length <= 0 || location < 0) {
		return;
	}
	int end = location + length;
	std::vector<AttributeRun> out;

	auto push = [&out](int loc, int len, const TextAttributes &attrs) {
		if (len <= 0) {
			return;
		}
		/* coalesce with the previous run when the attributes match */
		if (!out.empty()) {
			AttributeRun &last = out.back();

			if (last.location + last.length == loc
			    && last.attributes.font.family
				       == attrs.font.family
			    && last.attributes.font.sizePt == attrs.font.sizePt
			    && last.attributes.font.bold == attrs.font.bold
			    && last.attributes.color.r == attrs.color.r
			    && last.attributes.color.g == attrs.color.g
			    && last.attributes.color.b == attrs.color.b
			    && last.attributes.underline == attrs.underline) {
				last.length += len;
				return;
			}
		}
		AttributeRun r;

		r.location = loc;
		r.length = len;
		r.attributes = attrs;
		out.push_back(r);
	};

	for (const AttributeRun &r : runs_) {
		int rEnd = r.location + r.length;

		if (rEnd <= location || r.location >= end) {
			push(r.location, r.length, r.attributes);
			continue;
		}
		if (r.location < location) {
			push(r.location, location - r.location, r.attributes);
		}
		if (rEnd > end) {
			push(end, rEnd - end, r.attributes);
		}
	}
	push(location, length, a);
	/* keep the list sorted: an edit can cover text that lay between runs */
	std::sort(out.begin(), out.end(),
		  [](const AttributeRun &x, const AttributeRun &y) {
			  return x.location < y.location;
		  });
	runs_ = out;
}

void
AttributedString::setAttributes(const TextAttributes &a)
{
	runs_.clear();
	default_ = a;
}

void
AttributedString::setDefaultAttributes(const TextAttributes &a)
{
	default_ = a;
}

const AttributeRun &
AttributedString::run(int index) const
{
	static const AttributeRun none;

	if (index < 0 || index >= (int) runs_.size()) {
		return none;
	}
	return runs_[(size_t) index];
}

std::string
AttributedString::substring(int location, int length) const
{
	if (location < 0 || location > (int) text_.size() || length <= 0) {
		return "";
	}
	if (location + length > (int) text_.size()) {
		length = (int) text_.size() - location;
	}
	return text_.substr((size_t) location, (size_t) length);
}

static const Property AttributedString_PROPS[] = {
	{ "string",
	  [](const Object *o) {
		  return Value::of(static_cast<const AttributedString *>(o)
				   ->string()); },
	  nullptr },
	{ "length",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const
				   AttributedString *>(o)->length()); },
	  nullptr },
};

const ObjectClass AttributedString::kClass = {
	"AttributedString", &Object::kClass, AttributedString_PROPS,
	(int) (sizeof(AttributedString_PROPS)
	       / sizeof(AttributedString_PROPS[0])), nullptr, 0
};

/* ---- TextStorage ----------------------------------------------------- */

TextStorage::TextStorage()
{
}

/* keep the runs consistent with an edit: everything after the edit shifts
 * by the delta, and the inserted text takes the attributes that were in
 * effect at the edit point */
void
TextStorage::adjustRuns(int location, int removed, int inserted)
{
	int delta = inserted - removed;
	std::vector<AttributeRun> out;

	for (const AttributeRun &r : runs_) {
		int rEnd = r.location + r.length;

		if (rEnd <= location) {
			out.push_back(r);			/* before the edit */
			continue;
		}
		if (r.location >= location + removed) {
			AttributeRun shifted = r;		/* after it */

			shifted.location += delta;
			if (shifted.location < 0) {
				shifted.location = 0;
			}
			out.push_back(shifted);
			continue;
		}
		/* overlapping: keep the part before, drop the covered part */
		if (r.location < location) {
			AttributeRun head = r;

			head.length = location - r.location;
			out.push_back(head);
		}
	}
	if (inserted > 0) {
		/* the inserted text carries the attributes in effect there (the
		 * ones read BEFORE the edit - this is why the caller passes the
		 * attributes down) */
		AttributeRun ins;

		ins.location = location;
		ins.length = inserted;
		ins.attributes = default_;
		for (const AttributeRun &r : runs_) {
			if (location >= r.location
			    && location < r.location + r.length) {
				ins.attributes = r.attributes;
				break;
			}
		}
		out.push_back(ins);
	}
	std::sort(out.begin(), out.end(),
		  [](const AttributeRun &x, const AttributeRun &y) {
			  return x.location < y.location;
		  });
	runs_.clear();
	for (const AttributeRun &r : out) {
		if (r.length > 0) {
			addAttributes(r.attributes, r.location, r.length);
		}
	}
}

void
TextStorage::replaceCharacters(int location, int length, const char *utf8)
{
	if (location < 0) {
		location = 0;
	}
	if (location > (int) text_.size()) {
		location = (int) text_.size();
	}
	if (length < 0) {
		length = 0;
	}
	if (location + length > (int) text_.size()) {
		length = (int) text_.size() - location;
	}
	const char *ins = utf8 ? utf8 : "";
	int inserted = (int) std::strlen(ins);
	TextAttributes at = attributesAt(location);

	text_.replace((size_t) location, (size_t) length, ins);
	adjustRuns(location, length, inserted);
	if (inserted > 0) {
		/* make sure the inserted span carries the attributes the edit
		 * point had, even for the FIRST run (the adjust pass put the
		 * default there when nothing covered it) */
		addAttributes(at, location, inserted);
	}
	changeCount_++;
}

void
TextStorage::appendString(const char *utf8)
{
	replaceCharacters((int) text_.size(), 0, utf8);
}

void
TextStorage::insertString(int location, const char *utf8)
{
	replaceCharacters(location, 0, utf8);
}

void
TextStorage::deleteCharacters(int location, int length)
{
	replaceCharacters(location, length, "");
}

const ObjectClass TextStorage::kClass = {
	"TextStorage", &AttributedString::kClass, nullptr, 0, nullptr, 0
};

/* ---- TextContainer --------------------------------------------------- */

TextContainer::TextContainer()
{
}

double
TextContainer::textWidth() const
{
	double w = size_.w - 2.0 * padding_;

	return w < 1.0 ? 1.0 : w;
}

const ObjectClass TextContainer::kClass = {
	"TextContainer", &Object::kClass, nullptr, 0, nullptr, 0
};

/* ---- LayoutManager --------------------------------------------------- */

LayoutManager::LayoutManager()
{
}

LayoutManager::~LayoutManager()
{
}

void
LayoutManager::setTextStorage(TextStorage *s)
{
	storage_ = s;
	laidOutChange_ = -1;		/* lay out again on the next query */
}

void
LayoutManager::setTextContainer(TextContainer *c)
{
	container_ = c;
	laidOutSize_ = Size{ -1, -1 };
}

bool
LayoutManager::needsLayout() const
{
	if (!storage_ || !container_) {
		return false;
	}
	return laidOutChange_ != storage_->changeCount()
		|| laidOutSize_.w != container_->size().w
		|| laidOutSize_.h != container_->size().h
		|| laidOutMode_ != container_->breakMode();
}

/* the width of `length` bytes with one set of attributes (points) */
double
LayoutManager::measure(const char *utf8, int length,
		       const TextAttributes &a) const
{
	if (length <= 0 || !utf8) {
		return 0;
	}
	std::string s(utf8, (size_t) length);

	if (!textEngineReady()) {
		/* the same documented estimate Cell uses: a layout before any
		 * display or font configuration exists must still say something
		 * true-ish rather than collapse every width to zero */
		return (double) length * a.font.sizePt * 0.55;
	}
	TextMetrics m = textMetrics(a.font.family.empty() ? nullptr
							  : a.font.family.c_str(),
				    a.font.sizePt, s.c_str(), a.font.bold);
	return m.widthPt;
}

/* the height of one line holding [location, location+length): the tallest
 * font on it */
double
LayoutManager::lineHeightFor(int location, int length) const
{
	double h = 0;

	if (!textEngineReady()) {
		TextAttributes a = storage_ ? storage_->attributesAt(location)
					    : TextAttributes();

		return a.font.sizePt * 1.30;	/* the documented estimate */
	}

	if (!storage_ || length <= 0) {
		TextAttributes a = storage_ ? storage_->defaultAttributes()
					    : TextAttributes();
		TextMetrics m = textMetrics(a.font.family.empty()
						    ? nullptr
						    : a.font.family.c_str(),
					    a.font.sizePt, "Ag",
					    a.font.bold);

		return m.ascentPt + m.descentPt;
	}
	/* walk the run boundaries inside the line */
	int at = location;
	int end = location + length;

	while (at < end) {
		TextAttributes a = storage_->attributesAt(at);
		int next = end;

		/* where does this run or the line end, whichever is first? */
		for (int i = 0; i < storage_->runCount(); i++) {
			const AttributeRun &r = storage_->run(i);

			if (at >= r.location && at < r.location + r.length) {
				next = r.location + r.length;
				break;
			}
		}
		if (next > end) {
			next = end;
		}
		TextMetrics m = textMetrics(a.font.family.empty()
						    ? nullptr
						    : a.font.family.c_str(),
					    a.font.sizePt, "Ag", a.font.bold);
		double mh = m.ascentPt + m.descentPt;

		if (mh > h) {
			h = mh;
		}
		/* step a whole character past the run's start */
		int step = nextCharEnd(storage_->string(), (unsigned) at);

		at = (step > at) ? step : at + 1;
		if (at >= next) {
			at = next;
		}
	}
	return h;
}

/* how much of the text from `start` fits in `maxWidth`, and how wide it
 * is: word by word for wrapping, character by character otherwise */
int
LayoutManager::breakLine(int start, double maxWidth, double *usedWidth)
{
	const char *s = storage_->string();
	int len = (int) storage_->length();
	LineBreakMode mode = container_ ? container_->breakMode()
					: LineBreakMode::WordWrap;
	int at = start;
	double w = 0;

	*usedWidth = 0;
	while (at < len) {
		if (s[at] == '\n') {
			break;			/* an explicit break ends it */
		}
		/* one unit: a word (word wrap) or one character */
		int unitEnd = at;
		bool unitIsSpace = (s[at] == ' ');

		if (mode == LineBreakMode::WordWrap && !unitIsSpace) {
			while (unitEnd < len && s[unitEnd] != '\n'
			       && s[unitEnd] != ' ') {
				int next = nextCharEnd(s, (unsigned) unitEnd);

				unitEnd = (next > unitEnd) ? next : unitEnd + 1;
			}
		} else {
			int next = nextCharEnd(s, (unsigned) unitEnd);

			unitEnd = (next > unitEnd) ? next : unitEnd + 1;
		}
		int unitLen = unitEnd - at;
		double unitW = 0;

		/* a unit can span font runs: measure it piecewise so a bold
		 * word keeps its width */
		int piece = at;

		while (piece < unitEnd) {
			TextAttributes a = storage_->attributesAt(piece);
			int pieceEnd = unitEnd;

			for (int i = 0; i < storage_->runCount(); i++) {
				const AttributeRun &r = storage_->run(i);

				if (piece >= r.location
				    && piece < r.location + r.length) {
					pieceEnd = r.location + r.length;
					break;
				}
			}
			if (pieceEnd > unitEnd) {
				pieceEnd = unitEnd;
			}
			unitW += measure(s + piece, pieceEnd - piece, a);
			piece = pieceEnd;
		}
		if (w + unitW > maxWidth && at > start) {
			break;			/* does not fit: stop here */
		}
		if (w + unitW > maxWidth && at == start) {
			/* the FIRST unit is too long: an over-long word breaks
			 * at a character (Cocoa's word wrap does the same) and
			 * a single character that cannot fit still goes on the
			 * line, or the layout would never advance */
			if (mode == LineBreakMode::WordWrap && !unitIsSpace
			    && unitLen > 1) {
				int next = nextCharEnd(s, (unsigned) at);
				int oneLen = (next > at) ? next - at : 1;

				unitEnd = at + oneLen;
				unitLen = oneLen;
				unitW = measure(s + at, unitLen,
						storage_->attributesAt(at));
			}
		}
		w += unitW;
		at = unitEnd;
	}
	*usedWidth = w;
	/* do not leave a trailing space hanging on the line (Cocoa trims the
	 * break character out of the line) */
	int end = at;

	while (end > start && s[end - 1] == ' ') {
		end--;
	}
	return (end > start) ? end - start : 0;
}

/* the truncated copy a Clip/Truncate* container shows instead of wrapping */
std::string
LayoutManager::truncatedCopy(int start, double maxWidth,
			     LineBreakMode mode) const
{
	const AttributedString &st = *storage_;
	const char *s = st.string();
	int len = (int) st.length();
	int end = len;

	while (end > start && s[end - 1] == '\n') {
		end--;				/* trailing newlines are not shown */
	}
	if (mode == LineBreakMode::Clip) {
		/* cut, no ellipsis */
		return std::string(s + start, (size_t) (end - start));
	}
	const char *ell = "\xE2\x80\xA6";	/* U+2026 HORIZONTAL ELLIPSIS */
	size_t ellLen = 3;

	if (mode == LineBreakMode::TruncateTail) {
		/* drop from the end until head + ellipsis fits */
		int head = end - start;

		while (head > 0) {
			std::string cand(s + start, (size_t) head);

			cand += ell;
			if (measure(cand.c_str(), (int) cand.size(),
				    st.defaultAttributes()) <= maxWidth) {
				return cand;
			}
			head = prevCharStart(s + start, (unsigned) head);
			if (head < 0) {
				head = 0;
			}
		}
		return std::string(ell);
	}
	if (mode == LineBreakMode::TruncateHead) {
		int tail = end - start;

		while (tail > 0) {
			std::string cand = ell;

			cand += std::string(s + end - tail, (size_t) tail);
			if (measure(cand.c_str(), (int) cand.size(),
				    st.defaultAttributes()) <= maxWidth) {
				return cand;
			}
			tail--;
			/* keep the tail on a character boundary */
			while (tail > 0 && utf8IsCont((unsigned char)
						      s[end - tail])) {
				tail--;
			}
		}
		return std::string(ell);
	}
	/* TruncateMiddle: take from both ends, half each */
	int head = end - start;
	int tail = 0;

	while (head > 0) {
		std::string cand(s + start, (size_t) head);

		cand += ell;
		cand += std::string(s + end - tail, (size_t) tail);
		if (measure(cand.c_str(), (int) cand.size(),
			    st.defaultAttributes()) <= maxWidth) {
			return cand;
		}
		/* alternate: give back one from the head, then one from the
		 * tail, so the cut stays near the middle */
		int newHead = prevCharStart(s + start, (unsigned) head);
		int wantTail = (end - start) - newHead;

		if (wantTail > (end - start) / 2) {
			wantTail = (end - start) / 2;
		}
		while (wantTail > 0 && utf8IsCont((unsigned char)
						   s[end - wantTail])) {
			wantTail--;
		}
		tail = wantTail;
		head = newHead;
		if (head < 0) {
			head = 0;
		}
	}
	return std::string(ell);
}

void
LayoutManager::layout()
{
	lines_.clear();
	if (!storage_ || !container_) {
		laidOutChange_ = storage_ ? storage_->changeCount() : -1;
		return;
	}
	const char *s = storage_->string();
	int len = (int) storage_->length();
	double maxWidth = container_->textWidth();
	bool sized = container_->size().w > 0;
	LineBreakMode mode = container_->breakMode();
	double y = 0;

	if (!sized) {
		/* no width: one unlimited line (the measuring case) */
		maxWidth = 1e9;
	}
	if (sized && (mode == LineBreakMode::Clip
		      || mode == LineBreakMode::TruncateHead
		      || mode == LineBreakMode::TruncateTail
		      || mode == LineBreakMode::TruncateMiddle)) {
		TextLine l;
		std::string shown = truncatedCopy(0, maxWidth, mode);
		double h = lineHeightFor(0, (int) shown.size());

		l.location = 0;
		l.length = (int) shown.size();
		/* the truncated COPY is what the line SHOWS: lineString() would
		 * otherwise re-read the raw prefix out of the storage and the
		 * ellipsis would never be seen */
		l.text = shown;
		l.frame = Rect{ { container_->lineFragmentPadding(), 0 },
				{ maxWidth, h } };
		lines_.push_back(l);
		laidOutChange_ = storage_->changeCount();
		laidOutSize_ = container_->size();
		laidOutMode_ = mode;
		return;
	}
	int at = 0;

	while (at <= len) {
		if (at == len) {
			/* a trailing newline makes an empty last line (Cocoa
			 * does the same), but an EMPTY string is one line */
			if (len > 0 && s[len - 1] == '\n') {
				TextLine l;

				l.location = len;
				l.length = 0;
				l.frame = Rect{ { container_->lineFragmentPadding(),
						  y },
						{ maxWidth, lineHeightFor(at, 0) } };
				lines_.push_back(l);
			}
			break;
		}
		double used = 0;
		int n = breakLine(at, maxWidth, &used);
		TextLine l;

		l.location = at;
		l.length = n;
		l.frame = Rect{ { container_->lineFragmentPadding(), y },
				{ used, lineHeightFor(at, n) } };
		lines_.push_back(l);
		y += l.frame.size.h;
		at += n;
		if (at < len && s[at] == '\n') {
			at++;			/* consume the break */
		} else if (at < len && s[at] == ' ' && at > 0
			   && s[at - 1] != '\n') {
			/* the space we broke AT belongs to the break, not to the
			 * next line (otherwise every wrapped line but the first
			 * would start with one) */
			at++;
		}
	}
	laidOutChange_ = storage_->changeCount();
	laidOutSize_ = container_->size();
	laidOutMode_ = mode;
}

void
LayoutManager::ensureLayout()
{
	if (needsLayout()) {
		layout();
	}
}

int
LayoutManager::lineCount() const
{
	const_cast<LayoutManager *>(this)->ensureLayout();
	return (int) lines_.size();
}

const TextLine &
LayoutManager::line(int index) const
{
	static const TextLine none;

	const_cast<LayoutManager *>(this)->ensureLayout();
	if (index < 0 || index >= (int) lines_.size()) {
		return none;
	}
	return lines_[(size_t) index];
}

Rect
LayoutManager::usedRect() const
{
	const_cast<LayoutManager *>(this)->ensureLayout();
	Rect r = { { 0, 0 }, { 0, 0 } };

	for (const TextLine &l : lines_) {
		double bottom = l.frame.origin.y + l.frame.size.h;
		double right = l.frame.origin.x + l.frame.size.w;

		if (bottom > r.size.h) {
			r.size.h = bottom;
		}
		if (right > r.size.w) {
			r.size.w = right;
		}
	}
	return r;
}

double
LayoutManager::usedHeight() const
{
	return usedRect().size.h;
}

int
LayoutManager::lineIndexAt(const Point &p) const
{
	const_cast<LayoutManager *>(this)->ensureLayout();
	if (lines_.empty()) {
		return -1;
	}
	for (size_t i = 0; i < lines_.size(); i++) {
		const TextLine &l = lines_[i];

		if (p.y < l.frame.origin.y + l.frame.size.h) {
			return (int) i;
		}
	}
	return (int) lines_.size() - 1;
}

int
LayoutManager::characterIndexAt(const Point &p) const
{
	int li = lineIndexAt(p);

	if (li < 0) {
		return 0;
	}
	const TextLine &l = line(li);
	const char *s = storage_ ? storage_->string() : "";
	int at = l.location;
	int end = l.location + l.length;
	double x = l.frame.origin.x;

	while (at < end) {
		TextAttributes a = storage_->attributesAt(at);
		int next = nextCharEnd(s, (unsigned) at);

		if (next > end) {
			next = end;
		}
		double w = measure(s + at, next - at, a);

		if (p.x < x + w / 2.0) {
			return at;
		}
		x += w;
		at = next;
	}
	return end;
}

std::string
LayoutManager::lineString(int index) const
{
	const TextLine &l = line(index);

	if (!l.text.empty()) {
		return l.text;		/* the truncated copy the layout made */
	}
	if (!storage_) {
		return "";
	}
	return storage_->substring(l.location, l.length);
}

double
LayoutManager::textWidthOf(int location, int length) const
{
	if (!storage_ || length <= 0) {
		return 0;
	}
	const char *s = storage_->string();
	double x = 0;
	int at = location;
	int end = location + length;

	while (at < end) {
		TextAttributes a = storage_->attributesAt(at);
		int next = end;

		for (int i = 0; i < storage_->runCount(); i++) {
			const AttributeRun &r = storage_->run(i);

			if (at >= r.location && at < r.location + r.length) {
				next = r.location + r.length;
				break;
			}
		}
		if (next > end) {
			next = end;
		}
		x += measure(s + at, next - at, a);
		at = next;
	}
	return x;
}

void
LayoutManager::drawInContext(Context &ctx, const Point &origin)
{
	ensureLayout();
	if (!storage_) {
		return;
	}
	const char *s = storage_->string();

	for (const TextLine &l : lines_) {
		int at = l.location;
		int end = l.location + l.length;
		/* the baseline sits at the line's top + the font's ascent */
		TextAttributes first = storage_->attributesAt(at);
		TextMetrics fm = textMetrics(first.font.family.empty()
						     ? nullptr
						     : first.font.family.c_str(),
					     first.font.sizePt, "Ag",
					     first.font.bold);
		double lineTop = origin.y + l.frame.origin.y;
		double penX = origin.x + l.frame.origin.x;
		double baseline = lineTop + fm.ascentPt;

		while (at < end) {
			TextAttributes a = storage_->attributesAt(at);
			int next = end;

			/* stop at the next attribute change so each run is drawn
			 * in its own colour and font */
			for (int i = 0; i < storage_->runCount(); i++) {
				const AttributeRun &r = storage_->run(i);

				if (at >= r.location
				    && at < r.location + r.length) {
					next = r.location + r.length;
					break;
				}
			}
			if (next > end) {
				next = end;
			}
			std::string piece(s + at, (size_t) (next - at));

			ctx.drawText(a.font.family.empty()
					     ? nullptr
					     : a.font.family.c_str(),
				     a.font.sizePt, Point{ penX, baseline },
				     piece.c_str(), a.color, a.font.bold);
			if (a.underline) {
				TextMetrics m = textMetrics(
					a.font.family.empty()
						? nullptr
						: a.font.family.c_str(),
					a.font.sizePt, piece.c_str(),
					a.font.bold);

				ctx.fillRect(Rect{ { penX, baseline + 1.0 },
						   { m.widthPt, 1.0 } }, a.color);
			}
			penX += measure(s + at, next - at, a);
			at = next;
		}
	}
}

static const Property LayoutManager_PROPS[] = {
	{ "lineCount",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const
				   LayoutManager *>(o)->lineCount()); },
	  nullptr },
};

const ObjectClass LayoutManager::kClass = {
	"LayoutManager", &Object::kClass, LayoutManager_PROPS,
	(int) (sizeof(LayoutManager_PROPS) / sizeof(LayoutManager_PROPS[0])),
	nullptr, 0
};

} /* namespace argentum */
