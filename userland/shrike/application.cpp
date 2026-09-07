/* shrike/application.cpp — the Shrike Application object.
 *
 * S0.2: owns the X11 session (Display). init() opens the connection
 * (XOpenDisplay); the event loop lands in S0.3, text/.conf in S0.4/S0.5.
 */
#include <shrike/shrike.h>
#include <shrike/shrike_p.h>

#include <cstdlib>

namespace shrike {

static Application *theApp = nullptr;

Application &
Application::shared()
{
	if (!theApp) {
		theApp = new Application();
	}
	return *theApp;
}

const char *
Application::version() const
{
	return SHRIKE_VERSION;
}

bool
Application::init(const char *displayName)
{
	if (impl_->running) {
		return true;
	}
	/* XOpenDisplay(NULL) honors $DISPLAY; on the Xfb desktop the init
	 * spawns the server with DISPLAY=:0 for its clients. */
	impl_->dpy = XOpenDisplay(displayName);
	if (!impl_->dpy) {
		return false;
	}
	impl_->screen = DefaultScreen(impl_->dpy);
	impl_->running = true;
	return true;
}

bool
Application::isRunning() const
{
	return impl_->running;
}

void
Application::terminate()
{
	if (impl_->dpy) {
		XCloseDisplay(impl_->dpy);
		impl_->dpy = nullptr;
	}
	impl_->running = false;
}

Application::Application()
{
	impl_ = new Impl();
}

Application::~Application()
{
	terminate();
	delete impl_;
}

} /* namespace shrike */
