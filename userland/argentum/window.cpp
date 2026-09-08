/* argentum/window.cpp — the Argentum Window object.
 *
 * S0.2: creates + maps a real X11 window and fills it with a solid
 * color through core protocol (XPutImage of a depth-24 XRGB image — no
 * XRender/Xft client lib).
 * S0.3: selects the input events and registers in the Application's
 * window map so run() can dispatch to the responder virtuals.
 * S2.1a: an optional content view roots the view tree; the base draw()
 * composites it into an offscreen BitmapImage (per-view translate +
 * clip, local-px drawRect) and flushes.
 */
#include <argentum/argentum.h>
#include <argentum/argentum_p.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace argentum {

/* S2.1a: draw one view and its subtree into g. The context must
 * already be positioned so (0,0) is `v`'s top-left (the caller
 * translates); we push a frame, translate to this view, clip to its
 * px bounds, call draw(), recurse into subviews in draw order.
 * Frames are pt — px = frame × session pxPerPt (rounded). */
static void
render_view(View *v, GraphicsContext &g, double pxPerPt)
{
	if (!v || v->isHidden()) {
		return;
	}
	Rect fr = v->frame();

	g.save();
	g.translate((int) std::lround(fr.origin.x * pxPerPt),
		    (int) std::lround(fr.origin.y * pxPerPt));
	unsigned int pw = (unsigned int) std::lround(fr.size.w * pxPerPt);
	unsigned int ph = (unsigned int) std::lround(fr.size.h * pxPerPt);

	g.clipToRect(0, 0, pw, ph);
	v->draw(g);
	for (View *c : v->subviews()) {
		render_view(c, g, pxPerPt);
	}
	g.restore();
}

void
Window::setContentView(View *view)
{
	impl_->contentView = view;
	if (view && impl_->mapped) {
		view->setNeedsDisplay();
		draw();
	}
}

View *
Window::contentView() const
{
	return impl_->contentView;
}

void
Window::handleResize(unsigned int widthPx, unsigned int heightPx)
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	impl_->width = widthPx;
	impl_->height = heightPx;
	if (impl_->contentView) {
		/* reset the tree root to the full window; setFrame's
		 * size change triggers resizeSubviewsWithOldBounds,
		 * which relayouts every subview by its autoresizing
		 * mask (S2.1d springs/struts) */
		double ppt = Application::shared().pxPerPt();
		Rect full = { {0, 0},
			      {widthPx / ppt, heightPx / ppt} };

		impl_->contentView->setFrame(full);
	}
}

/* S2.1c: hit-test + responder dispatch for content-view windows.
 * The X event point is window-relative PX; frames are PT, so convert.
 * Descends to the deepest visible view under the point (reverse draw
 * order, hidden skipped) and delivers the mouse event with
 * VIEW-LOCAL PT coordinates; unhandled events bubble up the responder
 * chain (View::mouseDown default forwards to nextResponder with the
 * local point translated by the frame origin). Empty space hits the
 * content view itself. */
/* Shared S2.1c hit-test helper: convert the window-relative px point
 * to content-local pt, descend to the deepest visible view, and fill
 * *hl with the HIT-LOCAL pt. Returns the hit view (nullptr outside
 * the content tree). */
static View *
hit_in_tree(View *cv, double ppt, const MouseEvent &pxEvent, Point *hl)
{
	Point wp = { pxEvent.x / ppt, pxEvent.y / ppt };
	Point cl = { wp.x - cv->frame().origin.x,
		     wp.y - cv->frame().origin.y };

	if (!rectContains(cv->bounds(), cl)) {
		return nullptr;		/* outside the content tree */
	}
	View *hit = cv->hitTest(cl);
	if (!hit) {
		return nullptr;
	}
	/* translate the point down the path cv -> hit into hit-local pt */
	std::vector<View *> path;

	for (View *v = hit; v && v != cv; v = v->superview()) {
		path.push_back(v);
	}
	hl->x = cl.x;
	hl->y = cl.y;
	for (auto it = path.rbegin(); it != path.rend(); ++it) {
		hl->x -= (*it)->frame().origin.x;
		hl->y -= (*it)->frame().origin.y;
	}
	return hit;
}

void
Window::dispatchMouseToContent(const MouseEvent &pxEvent, bool down)
{
	if (!impl_->contentView) {
		return;
	}
	double ppt = Application::shared().pxPerPt();
	View *cv = impl_->contentView;
	Point hl;

	if (down) {
		/* press: hit-test, deliver, remember the target so the
		 * matching release reaches the SAME view even if the
		 * pointer moved off it (drag-out semantics) */
		View *hit = hit_in_tree(cv, ppt, pxEvent, &hl);
		if (!hit) {
			return;
		}
		impl_->pressed = hit;
		/* minimal focus (S2.2c): clicking an ENABLED control makes
		 * it the window's first responder; disabled controls do
		 * not take focus; clicking empty space clears */
		if (hit->acceptsFirstResponder()) {
			Control *c = dynamic_cast<Control *>(hit);

			if (!c || c->isEnabled()) {
				setFirstResponder(hit);
			}
		} else if (hit == cv) {
			setFirstResponder(nullptr);
		}
		MouseEvent e = pxEvent;

		e.x = hl.x;
		e.y = hl.y;
		hit->mouseDown(e);
		return;
	}
	/* release: the view that got the press owns the release */
	View *target = impl_->pressed ? impl_->pressed : nullptr;
	View *hit = hit_in_tree(cv, ppt, pxEvent, &hl);

	if (!target) {
		target = hit;
	}
	impl_->pressed = nullptr;
	if (!target) {
		return;
	}
	/* local pt for the TARGET (may differ from the pointer's hit) */
	Point tl = { 0, 0 };
	View *anc = target;

	if (target != cv) {
		Point wpl = { pxEvent.x / ppt, pxEvent.y / ppt };
		Point cl = { wpl.x - cv->frame().origin.x,
			     wpl.y - cv->frame().origin.y };
		std::vector<View *> path;

		for (View *v = target; v && v != cv; v = v->superview()) {
			path.push_back(v);
		}
		tl = cl;
		for (auto it = path.rbegin(); it != path.rend(); ++it) {
			tl.x -= (*it)->frame().origin.x;
			tl.y -= (*it)->frame().origin.y;
		}
	}
	(void) anc;
	MouseEvent e = pxEvent;

	e.x = tl.x;
	e.y = tl.y;
	target->mouseUp(e);
}

void
Window::setFirstResponder(View *view)
{
	if (impl_->firstResponder == view) {
		return;
	}
	if (impl_->firstResponder) {
		impl_->firstResponder->resignFirstResponder();
	}
	impl_->firstResponder = view;
	if (view) {
		view->becomeFirstResponder();
	}
}

View *
Window::firstResponder() const
{
	return impl_->firstResponder;
}

void
Window::dispatchMotionToContent(const MouseEvent &pxEvent)
{
	if (!impl_->contentView) {
		return;
	}
	double ppt = Application::shared().pxPerPt();
	View *cv = impl_->contentView;
	Point hl;
	View *hit = hit_in_tree(cv, ppt, pxEvent, &hl);

	/* S2.3a drag delivery: while a ButtonPress is held, pointer
	 * motion goes to the PRESSED view (so a Slider tracks the knob
	 * across the window); enter/exit tracking is suspended for the
	 * drag. */
	if (impl_->pressed) {
		View *target = impl_->pressed;
		Point wpl = { pxEvent.x / ppt, pxEvent.y / ppt };
		Point cl = { wpl.x - cv->frame().origin.x,
			     wpl.y - cv->frame().origin.y };
		std::vector<View *> path;
		Point tl = cl;

		if (target != cv) {
			for (View *v = target; v && v != cv;
			     v = v->superview()) {
				path.push_back(v);
			}
			tl = cl;
			for (auto it = path.rbegin(); it != path.rend();
			     ++it) {
				tl.x -= (*it)->frame().origin.x;
				tl.y -= (*it)->frame().origin.y;
			}
		}
		MouseEvent mv = pxEvent;

		mv.x = tl.x;
		mv.y = tl.y;
		target->mouseMoved(mv);
		return;
	}

	if (hit != impl_->motionTarget) {
		if (impl_->motionTarget) {
			MouseEvent out = pxEvent;

			impl_->motionTarget->mouseExited(out);
		}
		impl_->motionTarget = hit;
		if (hit) {
			MouseEvent in = pxEvent;

			in.x = hl.x;
			in.y = hl.y;
			hit->mouseEntered(in);
		}
	}
	if (hit) {
		MouseEvent mv = pxEvent;

		mv.x = hl.x;
		mv.y = hl.y;
		hit->mouseMoved(mv);
	}
}

void
Window::dispatchKeyToContent(const KeyEvent &keyEvent, bool down)
{
	if (!impl_->contentView) {
		return;
	}
	/* keys go to the first responder (S2.2c) when one is set, else
	 * the content view; unhandled events bubble up the chain */
	View *target = impl_->firstResponder ? impl_->firstResponder
					     : impl_->contentView;

	if (down) {
		target->keyDown(keyEvent);
	} else {
		target->keyUp(keyEvent);
	}
}

void
Window::draw()
{
	/* S2.1a: with a content view, composite the tree; without one
	 * this base implementation paints nothing (S1-era subclasses
	 * override draw() and never reach here). */
	if (!impl_->contentView || !impl_->dpy || !impl_->xwin) {
		return;
	}
	BitmapImage bmp(impl_->width, impl_->height);
	GraphicsContext g(bmp);

	/* deterministic backdrop before the tree composites */
	g.fillRect(0, 0, impl_->width, impl_->height, 0x000000);
	render_view(impl_->contentView, g,
		    Application::shared().pxPerPt());
	g.flush(*this, 0, 0);
}

Window::Window()
{
	impl_ = new Impl();
}

Window::~Window()
{
	/* drop out of the event-dispatch map first */
	if (impl_->dpy && impl_->xwin) {
		Application::shared().impl_->windows.erase(
			(unsigned long) impl_->xwin);
		XDestroyWindow(impl_->dpy, impl_->xwin);
	}
	delete impl_;
}

bool
Window::init(const char *title, int x, int y,
	     unsigned int width, unsigned int height)
{
	Application &app = Application::shared();

	if (!app.impl_->running || !app.impl_->dpy) {
		return false;
	}
	::Window root = DefaultRootWindow(app.impl_->dpy);
	unsigned long black = BlackPixel(app.impl_->dpy, app.impl_->screen);

	impl_->dpy = app.impl_->dpy;
	impl_->x = x;
	impl_->y = y;
	impl_->width = width;
	impl_->height = height;
	impl_->xwin = XCreateSimpleWindow(impl_->dpy, root, x, y,
					  width, height, 0, black, black);
	if (!impl_->xwin) {
		return false;
	}
	if (title) {
		XStoreName(impl_->dpy, impl_->xwin, title);
	}
	/* S0.3: which events the loop dispatches (keyboard, mouse
	 * buttons, expose/redraw). StructureNotifyMask (S2.1d) also
	 * brings ConfigureNotify so window resizes reach handleResize. */
	XSelectInput(impl_->dpy, impl_->xwin,
		     KeyPressMask | KeyReleaseMask |
		     ButtonPressMask | ButtonReleaseMask |
		     ExposureMask | StructureNotifyMask |
		     PointerMotionMask);
	XSync(impl_->dpy, False);

	/* register for event dispatch (idempotent on re-init) */
	app.impl_->windows[(unsigned long) impl_->xwin] = this;
	return true;
}

void
Window::show()
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	XMapWindow(impl_->dpy, impl_->xwin);
	/* focus the window so key events reach it without a WM */
	XSetInputFocus(impl_->dpy, impl_->xwin, RevertToParent, CurrentTime);
	impl_->mapped = true;
	XSync(impl_->dpy, False);
}

void
Window::fill(std::uint32_t rgb)
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	/* Depth-24 XRGB core-protocol blit: build a 32-bpp ZPixmap image
	 * (XRGB8888, matching the Xfb /dev/fb0 layout byte-for-byte), fill
	 * every pixel with the requested color and XPutImage it. This is
	 * the S0.2 "no XRender/Xft" proof — plain core protocol. */
	Display *dpy = impl_->dpy;
	::Window xwin = impl_->xwin;
	int screen = DefaultScreen(dpy);
	int w = (int) impl_->width;
	int h = (int) impl_->height;
	Visual *vis = DefaultVisual(dpy, screen);
	unsigned int depth = (unsigned int) DefaultDepth(dpy, screen);

	/* rgb is 0xRRGGBB; pack as 0x00RRGGBB in the 32-bpp XRGB8888 layout
	 * X11 expects (the fb is little-endian and the 8:8:8 masks
	 * 0xff0000/0x00ff00/0x0000ff match Xfb). */
	std::uint32_t pixel = rgb & 0xffffff;

	char *data = (char *) std::calloc((size_t) w * h, 4);
	if (!data) {
		return;
	}
	/* fill the buffer with the 32-bpp pixel repeated */
	std::uint32_t *words = (std::uint32_t *) data;
	for (int i = 0; i < w * h; i++) {
		words[i] = pixel;
	}

	XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0, data,
				   (unsigned int) w, (unsigned int) h,
				   32, w * 4);
	if (img) {
		GC gc = XCreateGC(dpy, xwin, 0, nullptr);
		if (gc) {
			XPutImage(dpy, xwin, gc, img, 0, 0, 0, 0,
				  (unsigned int) w, (unsigned int) h);
			XFreeGC(dpy, gc);
		}
		/* XDestroyImage frees data (we handed it ownership) */
		XDestroyImage(img);
	} else {
		std::free(data);
	}
	XSync(dpy, False);
}

/* --- responder virtuals (S0.3): default = ignore ------------------ */

void
Window::keyDown(const KeyEvent &)
{
}

void
Window::keyUp(const KeyEvent &)
{
}

void
Window::mouseDown(const MouseEvent &)
{
}

void
Window::mouseUp(const MouseEvent &)
{
}

unsigned int
Window::width() const
{
	return impl_->width;
}

unsigned int
Window::height() const
{
	return impl_->height;
}

unsigned long
Window::xid() const
{
	return (unsigned long) impl_->xwin;
}

} /* namespace argentum */
