/* shrike/shrike.h — Shrike toolkit public API (S0.1 skeleton).
 *
 * Shrike is FNX's from-scratch C++ GUI toolkit (docs/design/shrike-plan.md):
 * Cocoa-resemblant semantics under C++17, libc++, exceptions + RTTI.
 *
 * S0.1 (docs/design/shrike-milestone-split.md) ships the class
 * declarations + the library skeleton only: the namespace, version
 * reporting, and the Application/Window shells compile into the shared
 * libshrike.so.1 staged in /System/Libraries. The real X11 session,
 * event loop and window mapping land in S0.2/S0.3; text + .conf in
 * S0.4/S0.5. Nothing here touches X11 yet.
 */
#ifndef FNX_SHRIKE_SHRIKE_H
#define FNX_SHRIKE_SHRIKE_H

#define SHRIKE_VERSION_MAJOR 0
#define SHRIKE_VERSION_MINOR 1
#define SHRIKE_VERSION_PATCH 0

#define SHRIKE_VERSION "0.1.0"

namespace shrike {

/* Mirrors NSApplication. The single app object owns the session (X
 * connection, event loop, .conf); created on first shared() and alive
 * for the process. S0.1: identity + version only — init()/run() arrive
 * with the X11 session in S0.2/S0.3. */
class Application {
public:
	static Application &shared();

	/* toolkit version, e.g. "0.1.0" (SHRIKE_VERSION) */
	const char *version() const;

	/* no copying: one app per process */
	Application(const Application &) = delete;
	Application &operator=(const Application &) = delete;

private:
	Application();			/* constructed by shared() */
	~Application();
};

/* Mirrors NSWindow. S0.1: declared but inert — no X11 window yet
 * (S0.2/S0.3 attach the Xlib window + event dispatch). */
class Window {
public:
	Window();
	~Window();

	Window(const Window &) = delete;
	Window &operator=(const Window &) = delete;
};

} /* namespace shrike */

#endif /* FNX_SHRIKE_SHRIKE_H */
