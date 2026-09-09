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

#include <sys/ipc.h>
#include <sys/shm.h>

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace argentum {

/* ---- MIT-SHM transport (docs/design/mit-shm-plan.md M2) ----------
 * One persistent SysV segment per window backing. The segment must be
 * 0666: FNX has no SO_PEERCRED, so the server's shm_access() falls
 * through to the "other" bits. Geometry must be width*4 rows (the
 * layout the server derives from a depth-24 32-bpp ShmPutImage);
 * XShmCreateImage pads to that when the pad is 32, but verify and
 * fall back to XPutImage otherwise. */

void
Window::shmTeardown()
{
	if (!impl_->shmImg) {
		return;		/* nothing attached */
	}
	XShmDetach(impl_->dpy, &impl_->shm);
	shmdt(impl_->shmImg->data);
	impl_->shmImg->data = nullptr;	/* XDestroyImage frees data */
	XDestroyImage(impl_->shmImg);
	impl_->shmImg = nullptr;
	shmctl(impl_->shm.shmid, IPC_RMID, nullptr);
	impl_->shmUp = false;
	impl_->shmW = impl_->shmH = 0;
}

/* Make the shm transport ready for the current backing geometry.
 * Returns true when flushBacking can XShmPutImage. */
bool
Window::shmEnsure()
{
	int screen = DefaultScreen(impl_->dpy);

	if (impl_->shmUp && impl_->shmW == (int) impl_->width &&
	    impl_->shmH == (int) impl_->height) {
		return true;
	}
	if (impl_->shmUp) {
		shmTeardown();
	}
	if (!XShmQueryExtension(impl_->dpy)) {
		return false;
	}
	unsigned int wpx = impl_->width;
	unsigned int hpx = impl_->height;

	/* Use the PERSISTENT XShmSegmentInfo (impl_->shm): libXext keeps
	 * it as the XImage's obdata (img->obdata = the shminfo pointer),
	 * so XShmPutImage reads the segment id from it at flush time. A
	 * local would dangle the moment this function returns. */
	memset(&impl_->shm, 0, sizeof(impl_->shm));
	impl_->shm.shmid = shmget(IPC_PRIVATE, (size_t) wpx * hpx * 4,
				  IPC_CREAT | 0666);
	if (impl_->shm.shmid < 0) {
		return false;
	}
	XImage *img = XShmCreateImage(
		impl_->dpy, DefaultVisual(impl_->dpy, screen),
		(unsigned int) DefaultDepth(impl_->dpy, screen), ZPixmap,
		nullptr, &impl_->shm, wpx, hpx);
	if (!img) {
		shmctl(impl_->shm.shmid, IPC_RMID, nullptr);
		return false;
	}
	impl_->shm.shmaddr = img->data =
		(char *) shmat(impl_->shm.shmid, nullptr, 0);
	impl_->shm.readOnly = False;
	if (impl_->shm.shmaddr == (char *) -1) {
		img->data = nullptr;
		XDestroyImage(img);
		shmctl(impl_->shm.shmid, IPC_RMID, nullptr);
		return false;
	}
	if (!XShmAttach(impl_->dpy, &impl_->shm) ||
	    img->bytes_per_line != (int) (wpx * 4)) {
		/* padded layout or attach refused: keep the core path */
		shmdt(impl_->shm.shmaddr);
		img->data = nullptr;
		XDestroyImage(img);
		shmctl(impl_->shm.shmid, IPC_RMID, nullptr);
		return false;
	}
	/* libXext routes XShm over xcb, ahead of Xlib's buffered stream:
	 * flush so the attach (and every earlier Xlib request) reaches
	 * the server before the first XShmPutImage references the seg. */
	XSync(impl_->dpy, False);
	impl_->shmImg = img;
	impl_->shmUp = true;
	impl_->shmW = (int) wpx;
	impl_->shmH = (int) hpx;
	printf("ARGENTUM-SHM: enabled %ux%u shmid=%d\n", wpx, hpx,
	       impl_->shm.shmid);
	fflush(stdout);
	return true;
}

/* S2.1a: draw one view and its subtree into g. The context must
 * already be positioned so (0,0) is `v`'s top-left (the caller
 * translates); we push a frame, translate to this view, clip to its
 * px bounds, call draw(), recurse into subviews in draw order.
 * Frames are pt — px = frame × session pxPerPt (rounded).
 *
 * S2.6 damage-limited compositing: (wx,wy) is this view's window-px
 * origin and (d0x,d0y,d1x,d1y) the damage rect (window px). A subtree
 * whose rect does not intersect the damage is skipped entirely (no
 * draw, no text shaping, no glyph raster); intersecting views are
 * additionally clipped to the intersection so the work is bounded by
 * the dirty region. */
static void
render_view(View *v, GraphicsContext &g, double pxPerPt,
	    int wx, int wy, int d0x, int d0y, int d1x, int d1y)
{
	if (!v || v->isHidden()) {
		return;
	}
	Rect fr = v->frame();
	int pw = (int) std::lround(fr.size.w * pxPerPt);
	int ph = (int) std::lround(fr.size.h * pxPerPt);

	/* skip whole subtrees outside the damage */
	if (wx >= d1x || wy >= d1y || wx + pw <= d0x || wy + ph <= d0y) {
		return;
	}
	g.save();
	g.translate((int) std::lround(fr.origin.x * pxPerPt),
		    (int) std::lround(fr.origin.y * pxPerPt));
	g.clipToRect(0, 0, (unsigned) pw, (unsigned) ph);
	/* intersect the damage with this view's bounds (local px) */
	int cx0 = d0x - wx;
	int cy0 = d0y - wy;
	int cx1 = d1x - wx;
	int cy1 = d1y - wy;

	if (cx0 < 0) {
		cx0 = 0;
	}
	if (cy0 < 0) {
		cy0 = 0;
	}
	if (cx1 > pw) {
		cx1 = pw;
	}
	if (cy1 > ph) {
		cy1 = ph;
	}
	if (cx1 > cx0 && cy1 > cy0) {
		g.clipToRect(cx0, cy0, (unsigned) (cx1 - cx0),
			     (unsigned) (cy1 - cy0));
	}
	v->draw(g);
	for (View *c : v->subviews()) {
		Rect cr = c->frame();

		render_view(c, g, pxPerPt,
			    wx + (int) std::lround(cr.origin.x * pxPerPt),
			    wy + (int) std::lround(cr.origin.y * pxPerPt),
			    d0x, d0y, d1x, d1y);
	}
	g.restore();
}

void
Window::setContentView(View *view)
{
	impl_->contentView = view;
	if (view) {
		/* the root view reports damage to this window */
		view->impl_->hostWindow = this;
	}
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

	/* S2.3c: remember the press in ROOT px for popup anchoring */
	if (down) {
		Application::shared().impl_->lastRootX =
			impl_->x + (int) pxEvent.x;
		Application::shared().impl_->lastRootY =
			impl_->y + (int) pxEvent.y;
	}

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
	/* local pt for the TARGET (may differ from the pointer's hit).
	 * Content-local first; when the target is a child, subtract
	 * each ancestor's frame origin down to it. */
	Point wpl = { pxEvent.x / ppt, pxEvent.y / ppt };
	Point tl = { wpl.x - cv->frame().origin.x,
		     wpl.y - cv->frame().origin.y };

	if (target != cv) {
		std::vector<View *> path;

		for (View *v = target; v && v != cv; v = v->superview()) {
			path.push_back(v);
		}
		for (auto it = path.rbegin(); it != path.rend(); ++it) {
			tl.x -= (*it)->frame().origin.x;
			tl.y -= (*it)->frame().origin.y;
		}
	}
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

/* S2.6 per-rect damage ------------------------------------------ */

void
Window::noteDamage(int xPx, int yPx, unsigned int wPx, unsigned int hPx)
{
	int x1 = xPx + (int) wPx;
	int y1 = yPx + (int) hPx;

	if (x1 <= xPx || y1 <= yPx) {
		return;
	}
	if (!impl_->damageScheduled && impl_->dmgX1 <= impl_->dmgX0 &&
	    impl_->dmgY1 <= impl_->dmgY0) {
		impl_->dmgX0 = xPx;
		impl_->dmgY0 = yPx;
		impl_->dmgX1 = x1;
		impl_->dmgY1 = y1;
	} else {
		if (xPx < impl_->dmgX0) {
			impl_->dmgX0 = xPx;
		}
		if (yPx < impl_->dmgY0) {
			impl_->dmgY0 = yPx;
		}
		if (x1 > impl_->dmgX1) {
			impl_->dmgX1 = x1;
		}
		if (y1 > impl_->dmgY1) {
			impl_->dmgY1 = y1;
		}
	}
}

void
Window::scheduleDamagePx(int x0, int y0, int x1, int y1)
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	if (x1 <= x0 || y1 <= y0) {
		return;
	}
	noteDamage(x0, y0, (unsigned) (x1 - x0), (unsigned) (y1 - y0));
	if (!impl_->damageScheduled) {
		impl_->damageScheduled = true;
		/* ask the server for one Expose over the whole pending
		 * rect; run() turns it into draw() */
		XClearArea(impl_->dpy, impl_->xwin, x0, y0,
			   (unsigned) (x1 - x0), (unsigned) (y1 - y0),
			   True);
	}
}

/* Put the pending damage rect of the backing store onto the window. */
void
Window::flushBacking()
{
	BitmapImage::Impl *b = impl_->back->impl_;

	if (!b->img || impl_->dmgX1 <= impl_->dmgX0 ||
	    impl_->dmgY1 <= impl_->dmgY0) {
		return;
	}
	int screen = DefaultScreen(impl_->dpy);
	Visual *vis = DefaultVisual(impl_->dpy, screen);
	unsigned int depth = (unsigned int) DefaultDepth(impl_->dpy, screen);
	int dx = impl_->dmgX0;
	int dy = impl_->dmgY0;
	unsigned int dw = (unsigned) (impl_->dmgX1 - dx);
	unsigned int dh = (unsigned) (impl_->dmgY1 - dy);

	/* MIT-SHM path: memcpy the damaged rect into the segment and
	 * XShmPutImage (no image bytes on the wire). Fall back to
	 * XPutImage when the server lacks XShm or the layout pads. */
	if (shmEnsure() &&
	    impl_->shmImg->bytes_per_line ==
		    (int) pixman_image_get_stride(b->img)) {
		char *src = (char *) pixman_image_get_data(b->img);
		char *dst = impl_->shmImg->data;
		int bpl = impl_->shmImg->bytes_per_line;
		int rowBytes = (int) dw * 4;
		unsigned int y;

		for (y = 0; y < dh; y++) {
			memcpy(dst + (size_t) (dy + y) * bpl +
			       (size_t) dx * 4,
			       src + (size_t) (dy + y) *
					     pixman_image_get_stride(b->img) +
				       (size_t) dx * 4,
			       (size_t) rowBytes);
		}
		GC gc = XCreateGC(impl_->dpy, impl_->xwin, 0, nullptr);

		if (gc) {
			XShmPutImage(impl_->dpy, impl_->xwin, gc,
				     impl_->shmImg, dx, dy, dx, dy, dw, dh,
				     False);
			XFreeGC(impl_->dpy, gc);
		}
		XSync(impl_->dpy, False);
		return;
	}
	XImage *ximg = XCreateImage(
		impl_->dpy, vis, depth, ZPixmap, 0,
		(char *) pixman_image_get_data(b->img),
		b->width, b->height, 32,
		pixman_image_get_stride(b->img));

	if (!ximg) {
		return;
	}
	GC gc = XCreateGC(impl_->dpy, impl_->xwin, 0, nullptr);

	if (gc) {
		XPutImage(impl_->dpy, impl_->xwin, gc, ximg,
			  dx, dy, dx, dy, dw, dh);
		XFreeGC(impl_->dpy, gc);
	}
	/* XDestroyImage frees ximg->data — pixman's buffer, which the
	 * BitmapImage still owns; detach first. */
	ximg->data = nullptr;
	XDestroyImage(ximg);
	XSync(impl_->dpy, False);
}

void
Window::draw()
{
	/* S2.1a: with a content view, composite the tree; without one
	 * this base implementation paints nothing (S1-era subclasses
	 * override draw() and never reach here). S2.6 keeps a backing
	 * store and flushes ONLY the damaged rect (a full-window put of
	 * a big backing through Xfb is the redraw bottleneck). */
	if (!impl_->contentView || !impl_->dpy || !impl_->xwin) {
		return;
	}
	if (!impl_->back || impl_->back->width() != impl_->width ||
	    impl_->back->height() != impl_->height) {
		delete impl_->back;
		impl_->back = new BitmapImage(impl_->width, impl_->height);
		/* fresh backing: the whole window is damaged */
		impl_->dmgX0 = impl_->dmgY0 = 0;
		impl_->dmgX1 = (int) impl_->width;
		impl_->dmgY1 = (int) impl_->height;
	} else if (impl_->dmgX1 <= impl_->dmgX0 ||
		   impl_->dmgY1 <= impl_->dmgY0) {
		/* a redraw pass with no pending damage = full redraw */
		impl_->dmgX0 = impl_->dmgY0 = 0;
		impl_->dmgX1 = (int) impl_->width;
		impl_->dmgY1 = (int) impl_->height;
	}
	struct timespec t0, t1, t2;
	bool timed = getenv("ARGENTUM_DRAW_MS") != nullptr;

	clock_gettime(CLOCK_MONOTONIC, &t0);
	BitmapImage *back = impl_->back;
	GraphicsContext g(*back);
	double ppt = Application::shared().pxPerPt();

	/* deterministic backdrop, bounded to the damage rect (the rest
	 * of the backing keeps the previous frame) */
	g.fillRect(impl_->dmgX0, impl_->dmgY0,
		   (unsigned) (impl_->dmgX1 - impl_->dmgX0),
		   (unsigned) (impl_->dmgY1 - impl_->dmgY0), 0x000000);
	View *cv = impl_->contentView;
	Rect cr = cv->frame();

	render_view(cv, g, ppt,
		    (int) std::lround(cr.origin.x * ppt),
		    (int) std::lround(cr.origin.y * ppt),
		    impl_->dmgX0, impl_->dmgY0, impl_->dmgX1,
		    impl_->dmgY1);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	flushBacking();
	clock_gettime(CLOCK_MONOTONIC, &t2);
	if (timed) {
		fprintf(stderr, "DRAW-MS: comp=%ld flush=%ld rect=%d,%d-%d,%d\n",
			(t1.tv_sec - t0.tv_sec) * 1000 +
				(t1.tv_nsec - t0.tv_nsec) / 1000000,
			(t2.tv_sec - t1.tv_sec) * 1000 +
				(t2.tv_nsec - t1.tv_nsec) / 1000000,
			impl_->dmgX0, impl_->dmgY0, impl_->dmgX1,
			impl_->dmgY1);
	}
	impl_->dmgX0 = impl_->dmgY0 = 0;
	impl_->dmgX1 = impl_->dmgY1 = 0;
	impl_->damageScheduled = false;
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
	shmTeardown();
	delete impl_->back;
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
Window::setNeedsDisplay()
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	/* XClearArea on the whole window: the server answers with an
	 * Expose, which run() turns into draw(). */
	XClearArea(impl_->dpy, impl_->xwin, 0, 0, 0, 0, True);
}

void
Window::unmap()
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	XUnmapWindow(impl_->dpy, impl_->xwin);
	impl_->mapped = false;
}

void
Window::moveRoot(int xPx, int yPx)
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	impl_->x = xPx;
	impl_->y = yPx;
	XMoveWindow(impl_->dpy, impl_->xwin, xPx, yPx);
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
