/* shrike/window.cpp — the Shrike Window object.
 *
 * S0.2: creates + maps a real X11 window and fills it with a solid
 * color through core protocol (XPutImage of a depth-24 XRGB image — no
 * XRender/Xft client lib). The event loop arrives in S0.3.
 */
#include <shrike/shrike.h>
#include <shrike/shrike_p.h>

#include <cstdlib>
#include <cstring>

namespace shrike {

Window::Window()
{
	impl_ = new Impl();
}

Window::~Window()
{
	if (impl_->dpy && impl_->xwin) {
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
	return true;
}

void
Window::show()
{
	if (!impl_->dpy || !impl_->xwin) {
		return;
	}
	XMapWindow(impl_->dpy, impl_->xwin);
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
		img->data = data;
		XDestroyImage(img);
	} else {
		std::free(data);
	}
	XSync(dpy, False);
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

} /* namespace shrike */
