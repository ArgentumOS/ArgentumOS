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

} /* namespace argentum */
