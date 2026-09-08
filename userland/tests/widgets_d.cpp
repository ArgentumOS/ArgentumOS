/* widgets_d.cpp — Argentum S2.2d acceptance (docs/design/
 * argentum-s22-control-first-leaves.md): TextField edit engine, on
 * the S2.2 milestone's interactive slice board (label + text field +
 * button):
 *   label "Name:"   (10,10,60x24)pt
 *   field           (10,44,220x26)pt   starts "abc", caret at the end
 *   clear button    (10,90,90x26)pt    action clears the field
 *
 * The probe injects (second X connection, before run()):
 *   click the field's right edge   -> focus, caret 3
 *   type "XY"                      -> "abcXY" (5)
 *   BackSpace                      -> "abcX"  (4)
 *   Left, Left                     -> caret 2
 *   type "Z"                       -> "abZcX" (3)
 *   Home                           -> caret 0
 *   Shift+Right, Shift+Right       -> selection [0,2]
 *   BackSpace (over the selection) -> "ZcX"   (0)
 *   type "Q"                       -> "QZcX"  (1)
 *   click Clear                    -> value "" (0)  [focus moves]
 * A LogField subclass logs every edit (value + caret + selection) as
 * S22D-EDIT, so the engine's step sequence is asserted. BoardWin logs
 * S22D-DRAW <value> <caret> <selStart>-<selEnd> per redraw; the last
 * draw (after the final synthetic Expose) is pixel-probed.
 */
#include <argentum/argentum.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <cstdio>
#include <cstring>
#include <unistd.h>

static const int WIN_X = 100;
static const int WIN_Y = 80;
static const unsigned WIN_W = 480;
static const unsigned WIN_H = 360;

static int
px(double pt)
{
	return (int) (pt * 4.0 / 3.0 + 0.5);
}

class LogField : public argentum::TextField {
public:
	void valueChanged() override
	{
		std::fprintf(stderr, "S22D-EDIT: \"%s\" caret=%u sel=%u-%u\n",
			     value(), caretIndex(), selectionStart(),
			     selectionEnd());
		std::fflush(stderr);
	}
};

class ContentView : public argentum::View {
public:
	argentum::Label nameLabel;
	LogField field;
	argentum::Button clearBtn;

	void draw(argentum::GraphicsContext &g) override
	{
		argentum::Rect f = frame();
		double ppt = argentum::Application::shared().pxPerPt();
		unsigned int w = (unsigned int) (f.size.w * ppt + 0.5);
		unsigned int h = (unsigned int) (f.size.h * ppt + 0.5);

		g.fillRect(0, 0, w, h, 0x223344);
	}
};

class BoardWin : public argentum::Window {
public:
	argentum::View *field() const
	{
		return field_;
	}

	void setField(argentum::View *v)
	{
		field_ = v;
	}

	void draw() override
	{
		Window::draw();
		if (field_) {
			argentum::TextField *tf =
				static_cast<argentum::TextField *>(field_);

			std::fprintf(stderr,
				     "S22D-DRAW: \"%s\" caret=%u sel=%u-%u\n",
				     tf->value(), tf->caretIndex(),
				     tf->selectionStart(),
				     tf->selectionEnd());
			std::fflush(stderr);
		}
	}

private:
	argentum::View *field_ = nullptr;
};

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		std::fprintf(stderr, "S22D: init failed\n");
		return 1;
	}

	ContentView content;
	content.setFrame({ {0, 0}, {360, 270} });
	content.nameLabel.setText("Name:");
	content.nameLabel.setFrame({ {10, 10}, {60, 24} });
	content.field.setFrame({ {10, 44}, {220, 26} });
	content.field.setValue("abc");
	content.clearBtn.setFrame({ {10, 90}, {90, 26} });
	content.clearBtn.setTitle("Clear");
	content.clearBtn.setAction([&content](argentum::Control *) {
		content.field.setValue("");
	});
	content.addSubview(&content.nameLabel);
	content.addSubview(&content.field);
	content.addSubview(&content.clearBtn);

	/* a11y read-back before run (field = "abc") */
	std::fprintf(stderr,
		     "S22D-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.nameLabel.accessibilityRole()),
		     content.nameLabel.accessibilityLabel(),
		     content.nameLabel.accessibilityEnabled(),
		     content.nameLabel.accessibilityValue());
	std::fprintf(stderr,
		     "S22D-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.field.accessibilityRole()),
		     content.field.accessibilityLabel(),
		     content.field.accessibilityEnabled(),
		     content.field.accessibilityValue());
	std::fprintf(stderr,
		     "S22D-A11Y: %s \"%s\" enabled=%d value=%s\n",
		     argentum::accessibilityRoleName(
			     content.clearBtn.accessibilityRole()),
		     content.clearBtn.accessibilityLabel(),
		     content.clearBtn.accessibilityEnabled(),
		     content.clearBtn.accessibilityValue());
	std::fflush(stderr);

	BoardWin w;
	if (!w.init("Argentum S2.2d textfield", WIN_X, WIN_Y, WIN_W,
		    WIN_H)) {
		std::fprintf(stderr, "S22D: window init failed\n");
		return 1;
	}
	w.setContentView(&content);
	w.setField(&content.field);
	w.show();			/* show() syncs the connection */

	Display *d2 = XOpenDisplay(nullptr);
	if (!d2) {
		std::fprintf(stderr, "S22D: no injection display\n");
		return 1;
	}
	::Window xwin = (::Window) w.xid();
	::Window root = DefaultRootWindow(d2);

	auto motion = [&](int x, int y) {
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xmotion.type = MotionNotify;
		ev.xmotion.display = d2;
		ev.xmotion.window = xwin;
		ev.xmotion.root = root;
		ev.xmotion.x = x;
		ev.xmotion.y = y;
		XSendEvent(d2, xwin, False, PointerMotionMask, &ev);
		XSync(d2, False);
		usleep(50000);
	};
	auto click = [&](int x, int y) {
		motion(x, y);
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xbutton.type = ButtonPress;
		ev.xbutton.display = d2;
		ev.xbutton.window = xwin;
		ev.xbutton.root = root;
		ev.xbutton.x = x;
		ev.xbutton.y = y;
		ev.xbutton.button = 1;
		ev.xbutton.state = 0;
		XSendEvent(d2, xwin, False, ButtonPressMask, &ev);
		ev.xbutton.type = ButtonRelease;
		ev.xbutton.state = Button1Mask;
		XSendEvent(d2, xwin, False, ButtonPressMask, &ev);
		XSync(d2, False);
		usleep(50000);
	};
	auto key = [&](KeySym ks, unsigned int mods, bool down) {
		KeyCode kc = XKeysymToKeycode(d2, ks);
		XEvent ev;
		std::memset(&ev, 0, sizeof(ev));
		ev.xkey.type = down ? KeyPress : KeyRelease;
		ev.xkey.display = d2;
		ev.xkey.window = xwin;
		ev.xkey.root = root;
		ev.xkey.keycode = kc;
		ev.xkey.state = mods;
		XSendEvent(d2, xwin, False, KeyPressMask, &ev);
		XSync(d2, false);
		usleep(50000);
	};
	auto typeChar = [&](const char *c, unsigned int mods) {
		KeySym ks = (KeySym) (unsigned char) c[0];
		key(ks, mods, true);
		key(ks, mods, false);
	};
	unsigned int SHIFT = ShiftMask;

	/* field at pt(10,44,220x26) -> px(13,59,293x35); right-edge click */
	int fldRx = px(10) + px(220) - 4;	/* ~296, near right edge */
	int fldCy = px(44) + px(26) / 2;	/* ~76 */
	click(fldRx, fldCy);			/* focus, caret 3 */

	typeChar("x", 0);
	typeChar("y", 0);			/* abcXY */
	key(XK_BackSpace, 0, true);		/* abcX */
	key(XK_BackSpace, 0, false);
	key(XK_Left, 0, true);			/* caret 2 */
	key(XK_Left, 0, false);
	key(XK_Left, 0, true);
	key(XK_Left, 0, false);
	typeChar("z", 0);			/* abZcX */
	key(XK_Home, 0, true);			/* caret 0 */
	key(XK_Home, 0, false);
	key(XK_Right, SHIFT, true);		/* sel [0,1] */
	key(XK_Right, SHIFT, false);
	key(XK_Right, SHIFT, true);		/* sel [0,2] */
	key(XK_Right, SHIFT, false);
	key(XK_BackSpace, 0, true);		/* delete sel -> ZcX */
	key(XK_BackSpace, 0, false);
	typeChar("q", 0);			/* QZcX */

	/* click Clear -> value "" (button action) */
	int clrCx = px(10) + px(90) / 2;
	int clrCy = px(90) + px(26) / 2;

	click(clrCx, clrCy);
	motion(px(320), px(180));	/* leave Clear: hover ends */

	/* final redraw of the cleared board */
	XEvent ev;
	std::memset(&ev, 0, sizeof(ev));
	ev.xexpose.type = Expose;
	ev.xexpose.display = d2;
	ev.xexpose.window = xwin;
	ev.xexpose.x = 0;
	ev.xexpose.y = 0;
	ev.xexpose.width = WIN_W;
	ev.xexpose.height = WIN_H;
	XSendEvent(d2, xwin, False, ExposureMask, &ev);
	XSync(d2, False);
	XCloseDisplay(d2);
	std::fprintf(stderr, "S22D-READY\n");
	std::fflush(stderr);

	app.run();
	return 0;
}
