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

/* A FIELD EDITOR IS NOT A MOUSE TARGET. It is drawn OVER the control it
 * edits, and the CONTROL keeps the mouse: a click inside the field being
 * edited has to reach the field (which keeps the focus and the caret), not
 * the editor floating above it. A TextView used as an ordinary view (the zoo
 * board's) hit-tests as usual. */
View *
TextView::hitTest(const Point &p)
{
	if (owner_) {
		return nullptr;
	}
	return View::hitTest(p);
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

/* ---- editing: this view IS the field editor ------------------------- *
 * The operations work on the storage being edited — the view's OWN, or the
 * CONTROL's when the window has lent this view out as its field editor
 * (setEditedStorage) — and keep the insertion point on a CHARACTER boundary,
 * stepping with the same UTF-8 helpers the cell used, so a backspace on a
 * multi-byte character removes the whole character, not a byte of it.
 */
void
TextView::setEditedStorage(TextStorage *s)
{
	edited_ = s;
	/* The LAYOUT draws what is EDITED, so it has to follow the binding: a
	 * field editor draws the cell's text, not its own. */
	layout_->setTextStorage(s ? s : storage_);
	syncContainer();
	setNeedsDisplay();
}

void
TextView::setInsertionPoint(int index)
{
	TextStorage *st = editedStorage();
	int len = st ? st->length() : 0;

	if (index < 0) {
		index = 0;
	}
	if (index > len) {
		index = len;
	}
	caret_ = index;
}

void
TextView::clampCaret()
{
	TextStorage *st = editedStorage();
	int len = st ? st->length() : 0;

	if (caret_ < 0) {
		caret_ = 0;
	}
	if (caret_ > len) {
		caret_ = len;
	}
}

bool
TextView::insertText(const char *text)
{
	TextStorage *st = editedStorage();

	if (!st || !text || !text[0]) {
		return false;
	}
	clampCaret();
	st->insertString(caret_, text);
	caret_ += (int) std::strlen(text);
	setNeedsDisplay();
	return true;
}

bool
TextView::deleteBackward()
{
	TextStorage *st = editedStorage();

	clampCaret();
	if (!st || caret_ <= 0) {
		return false;
	}
	const char *s = st->string();
	int start = prevCharStart(s, (unsigned) caret_);

	if (start < 0) {
		start = 0;
	}
	st->deleteCharacters(start, caret_ - start);
	caret_ = start;
	setNeedsDisplay();
	return true;
}

bool
TextView::deleteForward()
{
	TextStorage *st = editedStorage();

	clampCaret();
	if (!st || caret_ >= st->length()) {
		return false;
	}
	const char *s = st->string();
	int end = nextCharEnd(s, (unsigned) caret_);

	if (end <= caret_) {
		return false;
	}
	st->deleteCharacters(caret_, end - caret_);
	setNeedsDisplay();
	return true;
}

bool
TextView::moveLeft(const Event &)
{
	TextStorage *st = editedStorage();

	clampCaret();
	if (!st || caret_ <= 0) {
		return false;
	}
	int at = prevCharStart(st->string(), (unsigned) caret_);

	caret_ = at < 0 ? 0 : at;
	setNeedsDisplay();
	return true;
}

bool
TextView::moveRight(const Event &)
{
	TextStorage *st = editedStorage();

	clampCaret();
	if (!st) {
		return false;
	}
	int end = nextCharEnd(st->string(), (unsigned) caret_);

	if (end > caret_ && end <= st->length()) {
		caret_ = end;
		setNeedsDisplay();
		return true;
	}
	return false;
}

bool
TextView::moveToBeginningOfLine()
{
	if (caret_ == 0) {
		return false;
	}
	caret_ = 0;
	setNeedsDisplay();
	return true;
}

bool
TextView::moveToEndOfLine()
{
	TextStorage *st = editedStorage();
	int len = st ? st->length() : 0;

	if (caret_ == len) {
		return false;
	}
	caret_ = len;
	setNeedsDisplay();
	return true;
}

bool
TextView::insertNewline()
{
	/* RETURN COMMITS. Cocoa's field editor sends the action through its
	 * delegate and KEEPS EDITING — the caret stays and the next key still
	 * lands — which is what Window::commitEditing() does. The editor does not
	 * OWN the value, so it never copies it back itself. */
	if (owner_) {
		if (Window *w = window()) {
			return w->commitEditing();
		}
	}
	return false;
}

bool
TextView::cancelOperation()
{
	/* ESCAPE ENDS it: the edit goes back to the owner, and the value is
	 * dropped (Window::endEditing(false)). */
	if (owner_) {
		if (Window *w = window()) {
			return w->endEditing(false);
		}
	}
	return false;
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
	if (editing_) {
		/* THE INSERTION POINT, where the next character lands: its x comes
		 * from measuring the text BEFORE the caret, with the same layout that
		 * just drew it, so the hairline sits in the gap it belongs in. */
		const char *s = editedStorage() ? editedStorage()->string() : "";
		double x = 0;

		for (int at = 0; at < caret_ && s[at]; ) {
			int next = nextCharEnd(s, (unsigned) at);

			if (next <= at || next > caret_) {
				next = caret_;
			}
			x += layout_->textWidthOf(at, next - at);
			at = next;
		}
		ctx->fillRect(Rect{ { b.origin.x + inset_ + x, b.origin.y + inset_ },
				    { 1.0, b.size.h - 2.0 * inset_ } },
			      caretColor_);
	}
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

/* THE VALUE'S RECT is what the window's field editor takes over while the
 * field is edited: the cell keeps its bezel and background, and the text is
 * drawn by the editor, exactly where the cell would have drawn it. The one
 * arithmetic for it is here, so the drawing and the editor cannot disagree:
 * the run box sits kValueTopPt below the top of the value area. */
static const double kValueTopPt = 3.0;

Rect
TextFieldCell::valueRectInFrame(const Rect &frame) const
{
	double pad = 6.0;

	return Rect{ { frame.origin.x + pad, frame.origin.y + kValueTopPt },
		     { frame.size.w - 2.0 * pad,
		       frame.size.h - 2.0 * kValueTopPt } };
}

void
TextFieldCell::drawInFrame(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx || !layout_) {
		return;
	}
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
	/* THE CHROME IS THIS CELL'S; THE VALUE IS NOT. A cell that has already
	 * drawn a bezel of its own - the search field's, the token field's -
	 * must not get a SECOND one around its entry area, and that is exactly
	 * what delegating to this function did: one box outlined inside
	 * another. A subclass hands drawValue() the entry rect instead. */
	drawValue(valueRectInFrame(frame), inView);
}

/* the value: drawn in exactly the rect it is given — the run box's top is
 * that rect's top — so a subclass that has drawn its own chrome can hand
 * over its entry area, and the window's field editor takes over the SAME
 * rect without a second offset to keep in step */
void
TextFieldCell::drawValue(const Rect &frame, View *inView)
{
	Context *ctx = Context::current();

	if (!ctx || !layout_) {
		return;
	}
	std::string shown = storage_->string();
	bool placeholder = shown.empty() && !placeholder_.empty();

	/* THE WINDOW'S FIELD EDITOR DRAWS THE VALUE WHILE THE FIELD IS EDITED.
	 * The cell keeps its bezel and background and stops here, so the text is
	 * not drawn twice - and the placeholder goes with it, which is what Cocoa
	 * shows (a focused, empty field shows the caret, not the prompt). */
	if (hidesValue()) {
		return;
	}

	/* THE STORAGE KEEPS THE CELL'S OWN COLOUR, NEVER THE PLACEHOLDER'S.
	 * Writing the grey in here is what greyed the FIRST typed character:
	 * an empty field draws grey and wrote that grey into the storage's
	 * default, so the insert that replaced the placeholder took ITS
	 * attributes from the grey - one grey run at position 0 - while every
	 * later insert took the black default the following frame had written
	 * back. The placeholder is drawn from a temporary storage below and
	 * needs nothing of this. */
	TextAttributes want = defaultAttributesOrMarked(false);

	/* size the container to the text area and lay the string out */
	container_->setSize(Size{ frame.size.w, frame.size.h });
	storage_->setDefaultAttributes(want);
	if (placeholder) {
		/* draw the placeholder WITHOUT touching the cell's string, and in
		 * the grey a placeholder is */
		TextStorage tmp;

		tmp.setString(placeholder_.c_str());
		tmp.setDefaultAttributes(defaultAttributesOrMarked(true));
		LayoutManager lm;

		lm.setTextStorage(&tmp);
		lm.setTextContainer(container_);
		lm.drawInContext(*ctx, Point{ frame.origin.x,
					      frame.origin.y });
	} else {
		layout_->drawInContext(*ctx, Point{ frame.origin.x,
						    frame.origin.y });
	}
}

/* the attributes the field draws with, with the placeholder greyed */
TextAttributes
TextFieldCell::defaultAttributesOrMarked(bool placeholder) const
{
	TextAttributes a = storage_->defaultAttributes();

	/* THE COLOUR IS THE CELL'S, DECIDED FRESH EVERY FRAME. Taking the
	 * value's colour from the storage's CURRENT default made the
	 * placeholder colour STICKY: an empty field draws its placeholder grey,
	 * that grey is written back into the storage's default so the layout can
	 * draw with it, and the next frame reads it back as the colour of the
	 * VALUE - so text typed into any field that had ever shown a placeholder
	 * came out grey for the rest of the session. */
	a.color = placeholder ? Color::rgb(0.55, 0.55, 0.58) : textColor_;
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
		/* the insertion point lives in the window's field editor now; a
		 * programmatic set puts it at the end, as the cell used to */
		if (Window *w = window()) {
			if (w->editingControl() == this) {
				if (TextView *ed = w->fieldEditorIfAny()) {
					ed->setInsertionPoint(
						c->textStorage()
							? c->textStorage()->length()
							: 0);
				}
			}
		}
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
TextField::wantsFieldEditor() const
{
	/* Cocoa's NSTextField edits through the window's FIELD EDITOR, and a
	 * label never does. The window asks this on a click and on a Tab. */
	return editable_;
}

/* THE COMMANDS. Each is the same work keyDown used to do by testing bools,
 * reached the way Cocoa reaches it: a key binding turns the event into a
 * command, and the control answers whether it handled it. The text lives in
 * the CELL (its string is the value, like every control's); the EDITING —
 * the insertion point and the drawing — belongs to the window's ONE field
 * editor, which the field reaches through Window::beginEditing().
 *
 * Starts the edit on demand, so a command that arrives with no edit open
 * (a focus taken by some path other than a click) still lands. */
TextView *
TextField::editingEditor()
{
	Window *w = window();

	if (!w) {
		return nullptr;
	}
	if (w->editingControl() != this && !w->beginEditing(this)) {
		return nullptr;
	}
	return w->fieldEditor();
}

bool
TextField::insertText(const char *text)
{
	TextFieldCell *c = fieldCell();

	/* ARGENTUM_KEYLOG, stage 2: WHICH field is asked, whether it has a cell,
	 * whether it is editable. Its frame identifies it against ZOO-AT. */
	if (getenv("ARGENTUM_KEYLOG")) {
		Rect f = frame();

		std::printf("ARGENTUM-INSERT field=%.0f,%.0f %.0fx%.0f text=\"%s\" "
			    "cell=%d editable=%d\n",
			    f.origin.x, f.origin.y, f.size.w, f.size.h,
			    text ? text : "", c ? 1 : 0, editable_ ? 1 : 0);
		std::fflush(stdout);
	}

	if (!c || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	bool took = ed->insertText(text);

	setNeedsDisplay();
	return took;
}

bool
TextField::insertNewline()
{
	if (!fieldCell() || !editable_) {
		return false;
	}
	/* RETURN COMMITS AND KEEPS EDITING — Cocoa sends the action without
	 * ending the edit, so the caret stays and the next key still lands. */
	if (Window *w = window()) {
		if (w->editingControl() == this) {
			return w->commitEditing();
		}
	}
	sendAction();
	setNeedsDisplay();
	return true;
}

bool
TextField::deleteBackward()
{
	if (!fieldCell() || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	setNeedsDisplay();
	return ed->deleteBackward();
}

bool
TextField::deleteForward()
{
	if (!fieldCell() || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	setNeedsDisplay();
	return ed->deleteForward();
}

bool
TextField::moveLeft(const Event &e)
{
	(void) e;
	if (!fieldCell() || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	setNeedsDisplay();
	return ed->moveLeft(e);
}

bool
TextField::moveRight(const Event &e)
{
	(void) e;
	if (!fieldCell() || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	setNeedsDisplay();
	return ed->moveRight(e);
}

bool
TextField::moveToBeginningOfLine()
{
	if (!fieldCell() || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	setNeedsDisplay();
	return ed->moveToBeginningOfLine();
}

bool
TextField::moveToEndOfLine()
{
	if (!fieldCell() || !editable_) {
		return false;
	}
	TextView *ed = editingEditor();

	if (!ed) {
		return false;
	}
	setNeedsDisplay();
	return ed->moveToEndOfLine();
}

bool
TextField::cancelOperation()
{
	/* ESCAPE ENDS the edit, and drops the value (Cocoa's field editor). */
	if (Window *w = window()) {
		if (w->editingControl() == this) {
			return w->endEditing(false);
		}
	}
	return false;
}

bool
TokenField::insertNewline()
{
	commitEntry();
	sendAction();		/* Return commits AND sends */
	return true;
}

bool
TokenField::insertText(const char *text)
{
	/* a comma commits WITHOUT sending: "a, b, c" is one edit, not three
	 * actions (Cocoa's rule for a token field) */
	if (text && std::strcmp(text, ",") == 0) {
		commitEntry();
		return true;
	}
	return TextField::insertText(text);
}

bool
TokenField::deleteBackward()
{
	TokenFieldCell *c = tokenCell();

	if (c && stringValue()[0] == '\0' && !c->tokens().empty()) {
		/* backspace on an EMPTY entry takes the last token back */
		c->removeLastToken();
		/* the chips shrank, so the entry moved: the editor follows it */
		if (Window *w = window()) {
			w->updateFieldEditorFrame();
		}
		setNeedsDisplay();
		return true;
	}
	return TextField::deleteBackward();
}

Color
TextField::caretColor() const
{
	Window *w = window();

	if (w && w->editingControl() == this) {
		if (TextView *ed = w->fieldEditorIfAny()) {
			return ed->caretColor();
		}
	}
	return Color::rgb(0.15, 0.15, 0.20);
}

void
TextField::setCaretColor(const Color &c)
{
	Window *w = window();

	if (w && w->editingControl() == this) {
		if (TextView *ed = w->fieldEditorIfAny()) {
			ed->setCaretColor(c);
			setNeedsDisplay();
		}
	}
}

int
TextField::insertionPoint() const
{
	Window *w = window();

	if (w && w->editingControl() == this) {
		if (TextView *ed = w->fieldEditorIfAny()) {
			return ed->insertionPoint();
		}
	}
	return 0;
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
SearchFieldCell::magnifierRect(const Rect &frame) const
{
	/* ONE arithmetic for the drawing and the hit test, as clearButtonRect is
	 * for the clear button: the magnifier sits in the field's left padding. */
	double d = SEARCH_GLYPH_PT;
	double pad = 6.0;

	return Rect{ { frame.origin.x + pad,
		       frame.origin.y + (frame.size.h - d) / 2.0 }, { d, d } };
}

Rect
SearchFieldCell::clearButtonRect(const Rect &frame) const
{
	double d = SEARCH_CLEAR_PT;

	return Rect{ { frame.origin.x + frame.size.w - d - 5.0,
		       frame.origin.y + (frame.size.h - d) / 2.0 }, { d, d } };
}

/* The entry area: the field's own value rect (the same one the base cell
 * computes), shifted right by the room the magnifier leaves. The window's
 * field editor takes over exactly this rect, so the caret lands on the text. */
Rect
SearchFieldCell::valueRectInFrame(const Rect &frame) const
{
	Rect text = TextFieldCell::valueRectInFrame(frame);

	text.origin.x += SEARCH_GLYPH_PT;
	text.size.w -= SEARCH_GLYPH_PT;
	return text;
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
	Rect mb = magnifierRect(frame);
	Point c = { mb.origin.x + g * 0.4, mb.origin.y + g * 0.5 };
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
	Rect text = valueRectInFrame(frame);

	if (stringValue()[0] == '\0' && placeholder()[0] != '\0'
	    && !hidesValue()) {
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
		lm.drawInContext(*ctx, Point{ text.origin.x, text.origin.y });
	} else {
		drawValue(text, inView);
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
SearchField::mouseUpInside(const Event &e)
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
	if (getenv("ARGENTUM_KEYLOG")) {
		/* the magnifier's instrument: a MISSING line means mouseUpInside was
		 * never called, n=0 means the branch ran with no recents to show */
		std::printf("ARGENTUM-SEARCHUP n=%d p=%.0f,%.0f\n",
			    (int) recentSearches_.size(), e.locationInWindow().x,
			    e.locationInWindow().y);
		std::fflush(stdout);
	}
	/* the MAGNIFIER opens the recent searches: Cocoa's searchMenuTemplate,
	 * presented with the pop-up machinery the menu family just gained. */
	if (rectContains(c->magnifierRect(bounds()), e.locationInWindow())) {
		Menu m;

		for (size_t i = 0; i < recentSearches_.size(); i++) {
			MenuItem *mi = m.addItem(recentSearches_[i].c_str(),
						 "recent");

			if (mi) {
				mi->setTarget(target());
			}
		}
		if (m.numberOfItems() > 0) {
			Window *w = window();
			Rect mrf = c->magnifierRect(bounds());
			Rect inWin = rectInWindow(mrf);
			double sx = (w ? w->frame().origin.x : 0) + inWin.origin.x;
			double sy = (w ? w->frame().origin.y : 0) + inWin.origin.y
				    + inWin.size.h;

			m.popUp(Point{ sx, sy });
		}
		return;
	}
	Rect cb = c->clearButtonRect(bounds());

	if (e.locationInWindow().x >= cb.origin.x
	    && e.locationInWindow().x < cb.origin.x + cb.size.w
	    && e.locationInWindow().y >= cb.origin.y
	    && e.locationInWindow().y < cb.origin.y + cb.size.h) {
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

/* What is left of the field after the chips: the rect the window's field
 * editor takes over. It MOVES as tokens are committed, which is why the
 * window re-reads it after every change (Window::updateFieldEditorFrame). */
Rect
TokenFieldCell::valueRectInFrame(const Rect &frame) const
{
	double x = entryOriginX(frame);
	double w = frame.origin.x + frame.size.w - x - 5.0;
	Rect base = TextFieldCell::valueRectInFrame(frame);	/* y + top inset */

	return Rect{ { x, base.origin.y },
		     { w > 1.0 ? w : 1.0, base.size.h } };
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
		/* Context::drawText's y is the TOP of the run box, not a baseline.
		 * Handing it the chip's centre + 4 (a baseline-shaped value) hung
		 * every token's text a line low, so it was clipped by the field's
		 * bottom edge. Centre it the way Cell centres a title. */
		ctx->drawText(nullptr, 12.0,
			      Point{ chip.origin.x
					      + (chip.size.w - m.widthPt) * 0.5,
				      chip.origin.y
					      + (h - (m.ascentPt
						      + m.descentPt)) * 0.5 },
			      t.c_str(), Color::rgb(0.15, 0.18, 0.24));
		x += chip.size.w;
	}
	/* the entry text after the last chip */
	Rect entry = valueRectInFrame(frame);

	if (entry.size.w < 4.0) {
		return;
	}
	if (stringValue()[0] == '\0' && placeholder()[0] != '\0'
	    && tokens_.empty() && !hidesValue()) {
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
					      entry.origin.y });
		return;
	}
	drawValue(entry, inView);
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
	size_t a = entry.find_first_not_of(" \t,");
	size_t b = entry.find_last_not_of(" \t,");

	/* A TOKEN IS ITS TEXT. A space typed before the comma was stored as
	 * part of the token, and because a chip measures the string it stores,
	 * the chip came out wider than its text - which reads as text sitting
	 * left-aligned in its chip. Separators do not belong to the token. */
	if (a == std::string::npos) {
		return;			/* nothing but separators */
	}
	entry = entry.substr(a, b - a + 1);
	c->addToken(entry);
	setStringValue("");
	setInsertionPointFor("");
	/* THE CHIPS GREW, so the entry moved right: the window's field editor
	 * has to follow it, or the text and the caret would be drawn over the
	 * chip that was just committed. */
	if (Window *w = window()) {
		w->updateFieldEditorFrame();
	}
	setNeedsDisplay();
}

void
TokenField::setInsertionPointFor(const char *utf8)
{
	(void) utf8;
	/* the entry is empty again after a commit, so the insertion point goes
	 * home — in the WINDOW's field editor, which is what holds it */
	if (Window *w = window()) {
		if (w->editingControl() == this) {
			if (TextView *ed = w->fieldEditorIfAny()) {
				ed->setInsertionPoint(0);
			}
		}
	}
}

const ObjectClass TokenField::kClass = {
	"TokenField", &TextField::kClass, nullptr, 0, nullptr, 0
};


bool
SearchField::insertNewline()
{
	/* a COMMIT records the search, as Cocoa's does */
	bool sent = TextField::insertNewline();

	addRecentSearch(stringValue());
	return sent;
}


/* ---- SearchField: the recent searches -------------------------------- */

void
SearchField::setRecentSearches(const std::vector<std::string> &v)
{
	recentSearches_ = v;
}

void
SearchField::addRecentSearch(const char *s)
{
	if (!s || !s[0]) {
		return;
	}
	std::string text(s);
	std::vector<std::string> out;

	out.push_back(text);
	for (size_t i = 0; i < recentSearches_.size(); i++) {
		if (recentSearches_[i] != text
		    && (int) out.size() < maximumRecents_) {
			out.push_back(recentSearches_[i]);
		}
	}
	recentSearches_ = out;
}

} /* namespace argentum */
