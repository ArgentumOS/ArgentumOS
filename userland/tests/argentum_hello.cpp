/* argentum_hello.cpp — Argentum S0.1 acceptance (docs/design/
 * argentum-milestone-split.md S0.1): the skeleton libargentum.so.1 is
 * staged in /System/Libraries and a C++ app links it DYNAMICALLY,
 * calling through the shared library (not a static copy). Prints
 * ARGENTUM-HELLO: libargentum <version> (from Application::shared()).
 *
 * The dynamic-link proof: argentum_hello NEEDs libargentum.so.1 (readelf),
 * and the loader resolves it from /System/Libraries at exec — same
 * mechanism libconfig.so.1 / libc++.so.1 already use. A static or
 * inlined version string would not exercise that path.
 */
#include <argentum/argentum.h>

#include <cstdio>

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	printf("ARGENTUM-HELLO: libargentum %s (Application via shared lib)\n",
	       app.version());
	return 0;
}
