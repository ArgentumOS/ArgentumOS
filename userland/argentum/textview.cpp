/*
 * The text views (U3b): TextView and TextFieldCell/TextField — the stack
 * put on screen (docs/design/cocoa-parity-plan.md).
 *
 * Both are thin on purpose: the storage holds the characters, the
 * container says how they break, the layout manager turns them into lines,
 * and these classes own those pieces and DRAW them. A resize resizes the
 * container and the lazy layout re-wraps — no view ever has to be told to
 * lay out again.
 */
#include <argentum/argentum.h>

#include "argentum/text_utf8.h"

#include <cstring>

namespace argentum {

/* ---- TextView -------------------------------------------------------- */

TextView::TextView()
{
	storage_ = new TextStorage();
	container_ = new TextContainer();
	layout_ = new LayoutManager();
	layout_->setTextStorage(storage_);
	layout_->setTextContainer(container_);
	container_->setBreakMode(LineBreakMode::WordWrap);
	syncContainer();
}

TextView::~TextView()
{
	delete layout_;		/* does not own the storage or the container */
	delete container_;
	delete storage_;
	layout_ = nullptr;
	container_ = nullptr;
	storage_ = nullptr;
}

void
TextView::syncContainer()
{
	Rect b = bounds();
	double w = b.size.w - 2.0 * inset_;
	double h = b.size.h - 2.0 * inset_;

	container_->setSize(Size{ w > 1.0 ? w : 1.0,
				  h > 1.0 ? h : 1.0 });
	container_->setLineFragmentPadding(0);
}

void
TextView::setFrame(const Rect &r)
{
	View::setFrame(r);
	syncContainer();
}

const char *
TextView::string() const
{
	return storage_->string();
}

void
TextView::setString(const char *utf8)
{
	storage_->setString(utf8);
	setNeedsDisplay();
}

TextAttributes
TextView::textAttributes() const
{
	return storage_->defaultAttributes();
}

void
TextView::setTextAttributes(const TextAttributes &a)
{
	storage_->setDefaultAttributes(a);
	setNeedsDisplay();
}

void
TextView::setTextInset(double pt)
{
	inset_ = pt < 0 ? 0 : pt;
	syncContainer();
	setNeedsDisplay();
}

LineBreakMode
TextView::breakMode() const
{
	return container_->breakMode();
}

void
TextView::setBreakMode(LineBreakMode m)
{
	container_->setBreakMode(m);
	setNeedsDisplay();
}

void
TextView::drawRect(const Rect &dirty)
{
	Context *ctx = Context::current();

	if (!ctx || !layout_) {
		return;
	}
	(void) dirty;
	Rect b = bounds();

	syncContainer();
	if (drawsBackground_) {
		ctx->fillRect(b, bg_);
	}
	layout_->drawInContext(*ctx, Point{ b.origin.x + inset_,
					    b.origin.y + inset_ });
}

static const Property TextView_PROPS[] = {
	{ "string",
	  [](const Object *o) {
		  return Value::of(static_cast<const TextView *>(o)
				   ->string()); },
	  [](Object *o, const Value &v) {
		  static_cast<TextView *>(o)->setString(
			  v.kind == Value::Text ? v.text.c_str() : "");
		  return true; } },
	{ "lineCount",
	  [](const Object *o) {
		  LayoutManager *lm = static_cast<const TextView *>(o)
					      ->layoutManager();

		  return Value::of((double) (lm ? lm->lineCount() : 0)); },
	  nullptr },
};

const ObjectClass TextView::kClass = {
	"TextView", &View::kClass, TextView_PROPS,
	(int) (sizeof(TextView_PROPS) / sizeof(TextView_PROPS[0])), nullptr, 0
};

/* ---- TextFieldCell --------------------------------------------------- */

TextFieldCell::TextFieldCell()
{
	storage_ = new TextStorage();
	container_ = new TextContainer();
	layout_ = new LayoutManager();
	layout_->setTextStorage(storage_);
	layout_->setTextContainer(container_);
	/* a field is ONE line, and the end is what gives */
	container_->setBreakMode(LineBreakMode::TruncateTail);
	container_->setLineFragmentPadding(0);
	setAlignment(TextAlignment::Left);
}

TextFieldCell::~TextFieldCell()
{
	delete layout_;
	delete container_;
	delete storage_;
	layout_ = nullptr;
	container_ = nullptr;
	storage_ = nullptr;
}

const char *
TextFieldCell::stringValue() const
{
	return storage_ ? storage_->string() : "";
}

void
TextFieldCell::setStringValue(const char *utf8)
{
	if (storage_) {
		storage_->setString(utf8);
	}
}

void
TextFieldCell::setPlaceholder(const char *utf8)
{
	placeholder_ = utf8 ? utf8 : "";
}

int
TextFieldCell::lineCount() const
{
	return layout_ ? layout_->lineCount() : 0;
}

/* ---- editing (U3c) ---------------------------------------------------
 *
 * The insertion point is a byte offset, and every operation keeps it on a
 * CHARACTER boundary by stepping with the UTF-8 helpers - so a backspace
 * on a multi-byte character removes the whole character, not a byte of it.
 */
void
TextFieldCell::setInsertionPoint(int index)
{
	int len = storage_ ? storage_->length() : 0;

	if (index < 0) {
		index = 0;
	}
	if (index > len) {
		index = len;
	}
	caret_ = index;
}

void
TextFieldCell::insertText(const char *utf8)
{
	if (!storage_ || !utf8 || !utf8[0]) {
		return;
	}
	storage_->insertString(caret_, utf8);
	caret_ += (int) std::strlen(utf8);
}

void
TextFieldCell::deleteBackward()
{
	if (!storage_ || caret_ <= 0) {
		return;
	}
	const char *s = storage_->string();
	int start = prevCharStart(s, (unsigned) caret_);

	if (start < 0) {
		start = 0;
	}
	storage_->deleteCharacters(start, caret_ - start);
	caret_ = start;
}

void
TextFieldCell::deleteForward()
{
	if (!storage_ || caret_ >= storage_->length()) {
		return;
	}
	const char *s = storage_->string();
	int end = nextCharEnd(s, (unsigned) caret_);

	if (end <= caret_) {
		return;
	}
	storage_->deleteCharacters(caret_, end - caret_);
}

void
TextFieldCell::moveLeft()
{
	if (!storage_ || caret_ <= 0) {
		return;
	}
	int at = prevCharStart(storage_->string(), (unsigned) caret_);

	caret_ = at < 0 ? 0 : at;
}

void
TextFieldCell::moveRight()
{
	if (!storage_) {
		return;
	}
	int end = nextCharEnd(storage_->string(), (unsigned) caret_);

	if (end > caret_ && end <= storage_->length()) {
		caret_ = end;
	}
}

void
TextFieldCell::moveToStart()
{
	caret_ = 0;
}

void
TextFieldCell::moveToEnd()
{
	caret_ = storage_ ? storage_->length() : 0;
}

void
TextFieldCell::setEditing(bool on)
{
	editing_ = on;
}

void
TextFieldCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx || !layout_) {
		return;
	}
	(void) inView;
	double pad = 6.0;
	double radius = bezeled_ ? 4.0 : 0;

	if (bezeled_ || drawsBackground_) {
		Color fill = drawsBackground_ ? bg_
					      : Color::rgb(0.98, 0.98, 0.99);

		if (radius > 0) {
			ctx->fillRoundRect(frame, radius, fill);
		} else {
			ctx->fillRect(frame, fill);
		}
		if (bezeled_) {
			ctx->strokeRoundRect(frame, radius, border_, 1.0);
		}
	}
	/* the value: the placeholder when there is none, greyed */
	std::string shown = storage_->string();
	bool placeholder = shown.empty() && !placeholder_.empty();
	TextAttributes attrs = defaultAttributesOrMarked(placeholder);

	if (placeholder) {
		shown = placeholder_;
	}
	/* size the container to the text area and lay the string out */
	container_->setSize(Size{ frame.size.w - 2.0 * pad, frame.size.h });
	TextAttributes want = attrs;

	storage_->setDefaultAttributes(want);
	if (placeholder) {
		/* draw the placeholder WITHOUT touching the cell's string */
		TextStorage tmp;

		tmp.setString(shown.c_str());
		tmp.setDefaultAttributes(want);
		LayoutManager lm;

		lm.setTextStorage(&tmp);
		lm.setTextContainer(container_);
		lm.drawInContext(*ctx, Point{ frame.origin.x + pad,
					      frame.origin.y + 3.0 });
		return;
	}
	layout_->drawInContext(*ctx, Point{ frame.origin.x + pad,
					    frame.origin.y + 3.0 });
	/* the insertion point: a hairline where the next character lands.
	 * Its x comes from measuring the text BEFORE the insertion point,
	 * which is the same measurement the layout draws with, so the caret
	 * sits where the gap is. */
	if (editing_) {
		const char *s = storage_->string();
		double x = 0;

		for (int at = 0; at < caret_ && s[at]; ) {
			int next = nextCharEnd(s, (unsigned) at);

			if (next <= at || next > caret_) {
				next = caret_;
			}
			x += layout_->textWidthOf(at, next - at);
			at = next;
		}
		ctx->fillRect(Rect{ { frame.origin.x + pad + x,
				      frame.origin.y + 4.0 },
				    { 1.0, frame.size.h - 8.0 } }, caretColor_);
	}
}

/* the attributes the field draws with, with the placeholder greyed */
TextAttributes
TextFieldCell::defaultAttributesOrMarked(bool placeholder) const
{
	TextAttributes a = storage_->defaultAttributes();

	if (placeholder) {
		a.color = Color::rgb(0.55, 0.55, 0.58);
	}
	return a;
}

Cell *
TextFieldCell::copy() const
{
	TextFieldCell *c = new TextFieldCell();

	c->setStringValue(stringValue());
	c->setPlaceholder(placeholder());
	c->setBezeled(bezeled_);
	c->setDrawsBackground(drawsBackground_);
	c->setBackgroundColor(bg_);
	return c;
}

static const Property TextFieldCell_PROPS[] = {
	{ "stringValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const TextFieldCell *>(o)
				   ->stringValue()); },
	  [](Object *o, const Value &v) {
		  static_cast<TextFieldCell *>(o)->setStringValue(
			  v.kind == Value::Text ? v.text.c_str() : "");
		  return true; } },
	{ "placeholder",
	  [](const Object *o) {
		  return Value::of(static_cast<const TextFieldCell *>(o)
				   ->placeholder()); },
	  nullptr },
	{ "lineCount",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const
				   TextFieldCell *>(o)->lineCount()); },
	  nullptr },
};

const ObjectClass TextFieldCell::kClass = {
	"TextFieldCell", &ActionCell::kClass, TextFieldCell_PROPS,
	(int) (sizeof(TextFieldCell_PROPS) / sizeof(TextFieldCell_PROPS[0])),
	nullptr, 0
};

/* ---- TextField ------------------------------------------------------- */

TextField::TextField()
{
	setCell(new TextFieldCell());
}

TextField *
TextField::label(const char *utf8)
{
	TextField *f = new TextField();
	TextFieldCell *c = f->fieldCell();

	if (c) {
		c->setBezeled(false);
		c->setDrawsBackground(false);
	}
	f->setStringValue(utf8);
	f->setEditable(false);
	f->setSelectable(false);
	return f;
}

const char *
TextField::stringValue() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->stringValue() : "";
}

void
TextField::setStringValue(const char *utf8)
{
	if (TextFieldCell *c = fieldCell()) {
		c->setStringValue(utf8);
		c->setInsertionPoint(c->textStorage()
					     ? c->textStorage()->length()
					     : 0);
		setNeedsDisplay();
	}
}



TextFieldCell *
TextField::fieldCell() const
{
	return dynamic_cast<TextFieldCell *>(cell_);
}

const char *
TextField::placeholder() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->placeholder() : "";
}

void
TextField::setPlaceholder(const char *utf8)
{
	if (TextFieldCell *c = fieldCell()) {
		c->setPlaceholder(utf8);
		setNeedsDisplay();
	}
}

TextStorage *
TextField::textStorage() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->textStorage() : nullptr;
}

int
TextField::lineCount() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->lineCount() : 0;
}

bool
TextField::isBezeled() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->isBezeled() : false;
}

void
TextField::setBezeled(bool on)
{
	if (TextFieldCell *c = fieldCell()) {
		c->setBezeled(on);
		setNeedsDisplay();
	}
}

bool
TextField::drawsBackground() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->drawsBackground() : false;
}

void
TextField::setDrawsBackground(bool on)
{
	if (TextFieldCell *c = fieldCell()) {
		c->setDrawsBackground(on);
		setNeedsDisplay();
	}
}

Color
TextField::textColor() const
{
	TextFieldCell *c = fieldCell();

	return c && c->textStorage() ? c->textStorage()->defaultAttributes().color
				     : Color::rgb(0.1, 0.1, 0.12);
}

void
TextField::setTextColor(const Color &c)
{
	if (TextFieldCell *cell = fieldCell()) {
		TextStorage *st = cell->textStorage();

		if (st) {
			TextAttributes a = st->defaultAttributes();

			a.color = c;
			st->setDefaultAttributes(a);
			setNeedsDisplay();
		}
	}
}

bool
TextField::acceptsFirstResponder() const
{
	/* an editable field takes the keyboard; a label does not (Cocoa: a
	 * label is a text field configured not to edit, and it stays out of
	 * the key loop) */
	return editable_;
}

bool
TextField::keyDown(const KeyEvent &e)
{
	TextFieldCell *c = fieldCell();

	if (!c || !editable_) {
		return false;
	}
	Cell *cell = c;

	(void) cell;
	if (e.isReturn) {
		/* COMMIT: the action is the field's reason to exist, and it goes
		 * with the field as the sender (Control's rule) */
		TextField *self = this;

		sendAction();
		(void) self;
		setNeedsDisplay();
		return true;
	}
	if (e.isDelete) {
		c->deleteBackward();
	} else if (e.isForwardDelete) {
		c->deleteForward();
	} else if (e.isLeft) {
		c->moveLeft();
	} else if (e.isRight) {
		c->moveRight();
	} else if (e.isHome) {
		c->moveToStart();
	} else if (e.isEnd) {
		c->moveToEnd();
	} else if (!e.characters.empty()) {
		c->insertText(e.characters.c_str());
	} else {
		return false;		/* Tab and Escape are not ours */
	}
	setNeedsDisplay();
	return true;
}

Color
TextField::caretColor() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->caretColor() : Color::rgb(0.15, 0.15, 0.20);
}

void
TextField::setCaretColor(const Color &c)
{
	if (TextFieldCell *cell = fieldCell()) {
		cell->setCaretColor(c);
		setNeedsDisplay();
	}
}

int
TextField::insertionPoint() const
{
	TextFieldCell *c = fieldCell();

	return c ? c->insertionPoint() : 0;
}

bool
TextField::isEditable() const
{
	return editable_;
}

void
TextField::setEditable(bool on)
{
	editable_ = on;
}

bool
TextField::isSelectable() const
{
	return selectable_;
}

void
TextField::setSelectable(bool on)
{
	selectable_ = on;
}

static const Property TextField_PROPS[] = {
	{ "stringValue",
	  [](const Object *o) {
		  return Value::of(static_cast<const TextField *>(o)
				   ->stringValue()); },
	  [](Object *o, const Value &v) {
		  static_cast<TextField *>(o)->setStringValue(
			  v.kind == Value::Text ? v.text.c_str() : "");
		  return true; } },
	{ "placeholder",
	  [](const Object *o) {
		  return Value::of(static_cast<const TextField *>(o)
				   ->placeholder()); },
	  [](Object *o, const Value &v) {
		  if (v.kind != Value::Text) {
			  return false;
		  }
		  static_cast<TextField *>(o)->setPlaceholder(v.text.c_str());
		  return true; } },
	{ "editable",
	  [](const Object *o) {
		  return Value::of(static_cast<const TextField *>(o)
				   ->isEditable()); },
	  nullptr },
	{ "lineCount",
	  [](const Object *o) {
		  return Value::of((double) static_cast<const TextField *>(o)
					   ->lineCount()); },
	  nullptr },
};

const ObjectClass TextField::kClass = {
	"TextField", &Control::kClass, TextField_PROPS,
	(int) (sizeof(TextField_PROPS) / sizeof(TextField_PROPS[0])), nullptr, 0
};

/* ---- SearchFieldCell / SearchField -------------------------------- */

#define SEARCH_GLYPH_PT	14.0	/* the magnifier's box */
#define SEARCH_CLEAR_PT	12.0	/* the clear button's box */

SearchFieldCell::SearchFieldCell()
{
	setAlignment(TextAlignment::Left);
}

Rect
SearchFieldCell::clearButtonRect(const Rect &frame) const
{
	double d = SEARCH_CLEAR_PT;

	return Rect{ { frame.origin.x + frame.size.w - d - 5.0,
		       frame.origin.y + (frame.size.h - d) / 2.0 }, { d, d } };
}

void
SearchFieldCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	/* the bezel and the background, exactly as a text field does */
	double pad = 6.0;
	double radius = 4.0;

	if (drawsBackground()) {
		ctx->fillRoundRect(frame, radius, backgroundColor());
	}
	if (isBezeled()) {
		ctx->strokeRoundRect(frame, radius, borderColor(), 1.0);
	}
	/* the magnifier: a ring with a handle, drawn from the same shapes a
	 * button's chrome uses */
	double g = SEARCH_GLYPH_PT;
	Point c = { frame.origin.x + pad + g * 0.4,
		    frame.origin.y + frame.size.h / 2.0 - g * 0.1 };
	Color mark = Color::rgb(0.45, 0.45, 0.50);

	ctx->fillCircle(c, g * 0.32, mark);
	ctx->fillCircle(c, g * 0.32 - 1.5, backgroundColor());
	{
		double hx = c.x + g * 0.22, hy = c.y + g * 0.22;
		Point handle[4] = { { hx, hy - 1.0 }, { hx + g * 0.28, hy + g * 0.28 - 1.0 },
				    { hx + g * 0.28, hy + g * 0.28 + 1.0 }, { hx, hy + 1.0 } };

		ctx->fillPolygon(handle, 4, mark);
	}
	/* the text: drawn by the base cell into the room the glyphs leave */
	Rect text = frame;

	text.origin.x += g + pad;
	text.size.w -= g + pad;
	if (stringValue()[0] == '\0' && placeholder()[0] != '\0') {
		TextStorage tmp;

		tmp.setString(placeholder());
		TextAttributes a = tmp.defaultAttributes();

		a.color = Color::rgb(0.55, 0.55, 0.58);
		tmp.setDefaultAttributes(a);
		LayoutManager lm;
		TextContainer tc;

		tc.setSize(Size{ text.size.w - 2 * pad, text.size.h });
		tc.setLineFragmentPadding(0);
		lm.setTextStorage(&tmp);
		lm.setTextContainer(&tc);
		lm.drawInContext(*ctx, Point{ text.origin.x, text.origin.y + 3.0 });
	} else {
		TextFieldCell::drawInFrame(text, inView);
	}
	/* the clear button, when there is something to clear */
	if (stringValue()[0] != '\0') {
		Rect cb = clearButtonRect(frame);
		Point c2 = { cb.origin.x + cb.size.w / 2.0,
			     cb.origin.y + cb.size.h / 2.0 };
		double r = cb.size.w / 2.0;
		Point a[4] = { { c2.x - r * 0.6, c2.y - r * 0.6 + 0.9 },
			       { c2.x + r * 0.6, c2.y + r * 0.6 + 0.9 },
			       { c2.x + r * 0.6, c2.y + r * 0.6 - 0.9 },
			       { c2.x - r * 0.6, c2.y - r * 0.6 - 0.9 } };
		Point b[4] = { { c2.x - r * 0.6, c2.y + r * 0.6 - 0.9 },
			       { c2.x + r * 0.6, c2.y - r * 0.6 - 0.9 },
			       { c2.x + r * 0.6, c2.y - r * 0.6 + 0.9 },
			       { c2.x - r * 0.6, c2.y + r * 0.6 + 0.9 } };

		ctx->fillCircle(c2, r, Color::rgb(0.78, 0.78, 0.82));
		ctx->fillPolygon(a, 4, Color::rgb(0.35, 0.35, 0.40));
		ctx->fillPolygon(b, 4, Color::rgb(0.35, 0.35, 0.40));
	}
}

Cell *
SearchFieldCell::copy() const
{
	SearchFieldCell *c = new SearchFieldCell();

	c->setStringValue(stringValue());
	c->setPlaceholder(placeholder());
	return c;
}

const ObjectClass SearchFieldCell::kClass = {
	"SearchFieldCell", &TextFieldCell::kClass, nullptr, 0, nullptr, 0
};

SearchField::SearchField()
{
	setCell(new SearchFieldCell());
	setPlaceholder("Search");
}

SearchFieldCell *
SearchField::searchCell() const
{
	return dynamic_cast<SearchFieldCell *>(cell_);
}

void
SearchField::mouseUpInside(const MouseEvent &e)
{
	SearchFieldCell *c = searchCell();

	if (!c) {
		TextField::mouseUpInside(e);
		return;
	}
	/* a click on the clear button empties the field and sends the action;
	 * a click anywhere else in the field is not a press of anything, so
	 * no action (Cocoa does the same: a field sends on Return and on the
	 * clear button, not on a plain click) */
	Rect cb = c->clearButtonRect(bounds());

	if (e.location.x >= cb.origin.x
	    && e.location.x < cb.origin.x + cb.size.w
	    && e.location.y >= cb.origin.y
	    && e.location.y < cb.origin.y + cb.size.h) {
		setStringValue("");
		setNeedsDisplay();
		sendAction();
	}
}

const ObjectClass SearchField::kClass = {
	"SearchField", &TextField::kClass, nullptr, 0, nullptr, 0
};

/* ---- TokenFieldCell / TokenField ---------------------------------- */

TokenFieldCell::TokenFieldCell()
{
	setAlignment(TextAlignment::Left);
}

void
TokenFieldCell::addToken(const std::string &token)
{
	if (!token.empty()) {
		tokens_.push_back(token);
	}
}

bool
TokenFieldCell::removeLastToken()
{
	if (tokens_.empty()) {
		return false;
	}
	tokens_.pop_back();
	return true;
}

void
TokenFieldCell::removeAllTokens()
{
	tokens_.clear();
}

double
TokenFieldCell::entryOriginX(const Rect &frame) const
{
	double x = frame.origin.x + 5.0;

	for (const std::string &t : tokens_) {
		TextMetrics m = textMetrics(nullptr, 12.0, t.c_str());

		x += m.widthPt + 14.0;		/* the chip's padding */
	}
	return x;
}

void
TokenFieldCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx) {
		return;
	}
	double pad = 4.0;
	double radius = 4.0;

	if (drawsBackground()) {
		ctx->fillRoundRect(frame, radius, backgroundColor());
	}
	if (isBezeled()) {
		ctx->strokeRoundRect(frame, radius, borderColor(), 1.0);
	}
	/* the chips, left to right */
	double x = frame.origin.x + 5.0;
	double h = frame.size.h - 2 * pad;

	for (const std::string &t : tokens_) {
		TextMetrics m = textMetrics(nullptr, 12.0, t.c_str());
		Rect chip = { { x, frame.origin.y + pad },
			      { m.widthPt + 14.0, h } };

		ctx->fillRoundRect(chip, h / 2.0, chip_);
		ctx->drawText(nullptr, 12.0,
			      Point{ x + 7.0, chip.origin.y + h * 0.5 + 4.0 },
			      t.c_str(), Color::rgb(0.15, 0.18, 0.24));
		x += chip.size.w;
	}
	/* the entry text after the last chip */
	Rect entry = { { entryOriginX(frame), frame.origin.y },
		       { frame.origin.x + frame.size.w - entryOriginX(frame)
			 - 5.0, frame.size.h } };

	if (entry.size.w < 4.0) {
		return;
	}
	if (stringValue()[0] == '\0' && placeholder()[0] != '\0'
	    && tokens_.empty()) {
		TextStorage tmp;

		tmp.setString(placeholder());
		TextAttributes a = tmp.defaultAttributes();

		a.color = Color::rgb(0.55, 0.55, 0.58);
		tmp.setDefaultAttributes(a);
		LayoutManager lm;
		TextContainer tc;

		tc.setSize(Size{ entry.size.w, entry.size.h });
		tc.setLineFragmentPadding(0);
		lm.setTextStorage(&tmp);
		lm.setTextContainer(&tc);
		lm.drawInContext(*ctx, Point{ entry.origin.x,
					      entry.origin.y + 3.0 });
		return;
	}
	TextFieldCell::drawInFrame(entry, inView);
}

Cell *
TokenFieldCell::copy() const
{
	TokenFieldCell *c = new TokenFieldCell();

	c->setStringValue(stringValue());
	c->setPlaceholder(placeholder());
	c->tokens_ = tokens_;
	return c;
}

const ObjectClass TokenFieldCell::kClass = {
	"TokenFieldCell", &TextFieldCell::kClass, nullptr, 0, nullptr, 0
};

TokenField::TokenField()
{
	setCell(new TokenFieldCell());
	setPlaceholder("Add a token");
}

TokenFieldCell *
TokenField::tokenCell() const
{
	return dynamic_cast<TokenFieldCell *>(cell_);
}

const std::vector<std::string> &
TokenField::tokens() const
{
	static const std::vector<std::string> none;
	TokenFieldCell *c = tokenCell();

	return c ? c->tokens() : none;
}

void
TokenField::addToken(const char *utf8)
{
	if (TokenFieldCell *c = tokenCell()) {
		c->addToken(utf8 ? utf8 : "");
		setNeedsDisplay();
	}
}

void
TokenField::removeAllTokens()
{
	if (TokenFieldCell *c = tokenCell()) {
		c->removeAllTokens();
		setNeedsDisplay();
	}
}

void
TokenField::commitEntry()
{
	TokenFieldCell *c = tokenCell();

	if (!c) {
		return;
	}
	std::string entry = stringValue();

	if (entry.empty()) {
		return;
	}
	c->addToken(entry);
	setStringValue("");
	setInsertionPointFor("");
	setNeedsDisplay();
}

void
TokenField::setInsertionPointFor(const char *utf8)
{
	(void) utf8;
	if (TokenFieldCell *c = tokenCell()) {
		c->setInsertionPoint(0);
	}
}

bool
TokenField::keyDown(const KeyEvent &e)
{
	TokenFieldCell *c = tokenCell();

	if (!c) {
		return false;
	}
	if (e.isReturn) {
		commitEntry();
		sendAction();		/* Return commits AND sends */
		return true;
	}
	/* a comma commits WITHOUT sending: "a, b, c" is one edit, not three
	 * actions (Cocoa's rule for a token field) */
	if (!e.characters.empty() && e.characters == ",") {
		commitEntry();
		return true;
	}
	if (e.isDelete && stringValue()[0] == '\0' && !c->tokens().empty()) {
		/* backspace on an EMPTY entry takes the last token back */
		c->removeLastToken();
		setNeedsDisplay();
		return true;
	}
	return TextField::keyDown(e);
}

const ObjectClass TokenField::kClass = {
	"TokenField", &TextField::kClass, nullptr, 0, nullptr, 0
};

} /* namespace argentum */
