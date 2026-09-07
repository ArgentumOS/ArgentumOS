/* shrike_hello.cpp — Shrike S0.1 acceptance (docs/design/
 * shrike-milestone-split.md S0.1): the skeleton libshrike.so.1 is
 * staged in /System/Libraries and a C++ app links it DYNAMICALLY,
 * calling through the shared library (not a static copy). Prints
 * SHRIKE-HELLO: libshrike <version> (from Application::shared()).
 *
 * The dynamic-link proof: shrike_hello NEEDs libshrike.so.1 (readelf),
 * and the loader resolves it from /System/Libraries at exec — same
 * mechanism libconfig.so.1 / libc++.so.1 already use. A static or
 * inlined version string would not exercise that path.
 */
#include <shrike/shrike.h>

#include <cstdio>

int
main()
{
	shrike::Application &app = shrike::Application::shared();

	printf("SHRIKE-HELLO: libshrike %s (Application via shared lib)\n",
	       app.version());
	return 0;
}
