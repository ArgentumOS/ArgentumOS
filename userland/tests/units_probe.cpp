/* units_probe.cpp — Argentum S1.1 acceptance (docs/design/
 * argentum-milestone-split.md): the points→pixels session factor.
 *
 * px/pt = PPI/72, resolved once at session start from the display's
 * physical size (plan §3 Units): the system.display domain
 * (display.width_mm/height_mm) when set; else the X server's mm when
 * the display domain is absent; else the 96 dpi fallback (4/3 px/pt).
 *
 * The probe opens the real session (like the demo) and prints the
 * session factor. The S1.1 gate runs it twice:
 *   run 1 — stock system.display.conf (width_mm/height_mm unset) →
 *           UNITS: pxPerPt=1.333333 (96 dpi fallback)
 *   then  config write -s system.display display.width_mm 169.333
 *                   display.height_mm 105.833   (a 192-dpi panel at the
 *           1280x800 Xfb mode → pxPerPt = 8/3 = 2x the fallback)
 *   run 2 — UNITS: pxPerPt=2.666667
 * The override forces exactly 2x: 192/72 = 8/3 vs 96/72 = 4/3.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <unistd.h>

int
main()
{
	argentum::Application &app = argentum::Application::shared();

	int tries;
	for (tries = 0; tries < 120 && !app.init(); tries++) {
		usleep(1000000);
	}
	if (!app.isRunning()) {
		printf("UNITS: init failed\n");
		return 1;
	}
	printf("UNITS: pxPerPt=%.6f\n", app.pxPerPt());
	printf("UNITS: ptToPx(72)=%.6f pxToPt(96)=%.6f\n",
	       app.ptToPx(72.0), app.pxToPt(96.0));
	fflush(stdout);
	return 0;
}
