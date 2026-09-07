/* shrike/application.cpp — the Shrike Application object (S0.1 skeleton).
 *
 * S0.1: identity + version only. The X11 session (Application::init(),
 * the event loop, .conf load) arrives in S0.2–S0.5; this file keeps the
 * shared library linkable and the singleton alive so S0.2 has a place
 * to hang the Display connection.
 */
#include <shrike/shrike.h>

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

Application::Application()
{
}

Application::~Application()
{
}

} /* namespace shrike */
