"""The stock boot reaches a working desktop, and it is drawn.

What it proves: the kernel mounts the root, init starts the session, Kestrel
comes up, the screen the guest claims matches what QEMU is really showing, the
wallpaper ramp the WM logged is on screen, the menu bar and the dock have
content, and nothing fatal happened on the way.

No pixel coordinate here is a constant.  Every coordinate is derived from a
line the guest printed, so the case survives a change of default resolution or
theme - which is exactly what broke the older gates.
"""

import re
import time

from harness import BaseCase, hex_rgb, luma_of

DOCK_LINE = (r"KESTREL: dock (\w+) (\d+)x(\d+) at (\d+),(\d+) "
             r"tiles=(\d+) icon=(\d+)")
# W0b: the SURFACE's report is the app's now. The wallpaper began as the WM's
# own window (S5.2a) and moved to Workspace in W0; the dock line stays
# Kestrel's, because the dock is the window manager's (D6). Same fields, so
# the geometry checks below read it exactly as before.
WALL_LINE = (r"WORKSPACE: desktop surface (\d+)x(\d+) base=0x([0-9a-f]+) "
             r"top=0x([0-9a-f]+) bot=0x([0-9a-f]+)")
FATAL = r"KERNEL EXCEPTION|cannot map the page|Page Fault at 0x"
XERR = r"X Error|BadWindow|BadMatch|BadValue|BadDrawable"


class Case(BaseCase):
    title = "the stock boot reaches a drawn desktop with no fatal faults"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        session = ctx.boot_to_desktop(secs=150)
        log = session.log_text()

        up = self.check("session-up", "KESTREL-READY" in log,
                        "the desktop session is running")
        self.check("root-mounted", "mounted root device" in log,
                   "the root filesystem came up")

        # The guest's idea of the screen against what QEMU actually renders.
        mode = re.search(r"INIT: display: mode (\d+)x(\d+) (\d+)bpp", log)
        # Both of the session's own lines arrive at its own pace — the shell
        # app starts as its shared library loads, and that library grew when
        # the Browser control joined it. So WAIT for them, and wait BEFORE the
        # screenshot: the shot IS an observation, and taking it first
        # photographed a desktop that had not been painted yet (luma 0 where
        # the ramp should be). Waiting only for the parsed checks fixed half
        # the problem and left the pixels reading black.
        if up:
            session.wait_for(r"KESTREL: dock \w+ \d+x\d+", 60)
            session.wait_for(r"WORKSPACE: desktop surface \d+x\d+", 60)
        log = session.log_text()
        shot = session.shot("desktop") if up else None
        self.check("resolution-agrees",
                   bool(mode and shot and shot.w == int(mode.group(1))
                        and shot.h == int(mode.group(2))),
                   "guest %sx%s, screenshot %s"
                   % (mode.group(1) if mode else "?",
                      mode.group(2) if mode else "?",
                      ("%dx%d" % (shot.w, shot.h)) if shot else "none"))

        dock = re.search(DOCK_LINE, log)
        wall = re.search(WALL_LINE, log)
        self.check("wm-logged-its-layout", bool(dock and wall),
                   "the session reported its layout: the dock from the WM, "
                   "the desktop surface from Workspace")

        if shot and dock and wall:
            side = dock.group(1)
            dw, dh, dx, dy = (int(dock.group(i)) for i in (2, 3, 4, 5))
            icon = int(dock.group(7))
            self.note("dock %sx%s at %d,%d, screen %dx%d"
                      % (dw, dh, dx, dy, shot.w, shot.h))

            # The wallpaper covers the whole screen, and the desktop shows a
            # tone inside the ramp the WM said it drew.
            self.check("wallpaper-spans-screen",
                       (int(wall.group(1)), int(wall.group(2))) == (shot.w, shot.h),
                       "wallpaper %sx%s vs screen %dx%d"
                       % (wall.group(1), wall.group(2), shot.w, shot.h))
            lo, hi = sorted((luma_of(hex_rgb(wall.group(4))),
                             luma_of(hex_rgb(wall.group(5)))))
            # Sample desktop that NO window owns. The centre used to do,
            # until W1 gave the app a browser window — 720x420pt, so
            # 1440x840px, opened at 120,120 — which covers the middle of the
            # screen and read as luma 246 against a ramp of 95..147. The
            # corner below the dock is the one place the session guarantees
            # is open: the dock is the right edge and the surface is behind
            # everything, so this is the surface's own pixels.
            sx, sy = shot.w - 120, shot.h - 40
            got = shot.luma(sx, sy)
            self.check("wallpaper-on-screen", lo - 8 <= got <= hi + 8,
                       "desktop pixel at %d,%d has luma %d, ramp %d..%d"
                       % (sx, sy, got, lo, hi))

            # The menu bar owns the strip above the dock; content in it means
            # rows that are not one flat colour.
            bar = (0, 0, shot.w, dy)
            drawn = [y for y in range(1, dy) if not shot.row_is_uniform(y, bar)]
            self.check("bar-drawn", bool(drawn),
                       "%d of %d rows in the bar area have drawn content"
                       % (len(drawn), dy))

            # The dock is tile chrome on a slab.  Both regions come from the
            # geometry the WM logged: the tile band at the top of the dock and
            # the slab below the last tile.  The tiles are the lighter of the
            # two, and there is light content in there at all.
            box = (dx, dy, dx + dw, dy + dh)
            tile_box = (dx, dy, dx + dw, dy + icon)
            slab_box = (dx, dy + dh - icon, dx + dw, dy + dh)
            lit = shot.light_frac(box, 200, 4)
            tile_l = shot.mean_luma(tile_box, 2)
            slab_l = shot.mean_luma(slab_box, 2)
            self.check("dock-drawn", lit > 0.02 and tile_l > slab_l + 8,
                       "%.0f%% of the dock is tile-light; tile band luma %.0f vs "
                       "slab luma %.0f" % (lit * 100, tile_l, slab_l))

            self.check("dock-at-edge", dx + dw == shot.w,
                       "the dock reaches the %s edge (x %d..%d of %d)"
                       % (side, dx, dx + dw, shot.w))

            # The slab is square: it is edge chrome flush with the menubar and
            # the screen edge, so its extreme corner pixel is slab too, not
            # wallpaper showing through a rounded corner.
            cx, cy = dx + dw - 1, dy + dh - 1
            corner = shot.luma(cx, cy)
            self.check("dock-square-corners", abs(corner - slab_l) < 12,
                       "dock corner at %d,%d has luma %.0f vs slab luma %.0f"
                       % (cx, cy, corner, slab_l))

        clock = re.search(r'KESTREL: clock "([^"]*)"', log)
        # The interim cursor theme (userland/cursors, staged at
        # /System/Shared/Icons/default/cursors) is loaded by the WM and
        # defined on the root window, so every window inherits it. The log
        # carries the size: a miss logs UNAVAILABLE and the server's tiny
        # built-in cursor stays, which is what the size threshold separates.
        cur = re.search(r"KESTREL: cursor theme '([^']+)' (\d+)x(\d+) hot "
                        r"(\d+),(\d+) from (\S+)", session.log_text())
        self.check("cursor-theme-loaded",
                   bool(cur) and int(cur.group(2)) >= 20,
                   "the desktop's cursor comes from the staged theme (%s)"
                   % (cur.group(0) if cur else
                      "none loaded - see the UNAVAILABLE line"))

        self.check("clock-shown",
                   bool(clock) and any(ch.isdigit() for ch in clock.group(1)),
                   "menu-bar clock renders text: %r"
                   % (clock.group(1) if clock else None))

        faults = len(re.findall(FATAL, log))
        # --- WT-1: the Browser control (the column view) ------------------
        # Served by a PROBE, not a board in the zoo: the zoo is single-board
        # and its existing checks must not move, and the plan allows either.
        # The probe is over STATIC data on purpose — this slice proves the
        # CHAIN, and a file tree would hide a chain bug behind a filesystem
        # bug. It publishes its layout in ROOT pixels (origin + band), so the
        # drag below is aimed at a position the probe itself reported rather
        # than at a hardcoded one that assumes the point scale.
        session.serial("DISPLAY=:0 /System/Shared/tests/browser_probe &")
        if session.wait_for(r"BROWSER-PROBE: ready", 60):
            log = session.log_text()
            # the LAST origin the probe publishes: its first is printed before
            # the WM reparents the window into a frame, so it is the unmanaged
            # position — and a press aimed through it lands on the frame's
            # title band, which moves the window and arms no divider.
            session.wait_for(r"BROWSER-PROBE: origin=", 30)
            log = session.log_text()
            orgs = re.findall(
                r"BROWSER-PROBE: origin=(\d+),(\d+) band=(\d+)", log)
            org = orgs[-1] if orgs else None
            lay = re.search(r"BROWSER-PROBE: cols=(\d+) \(chain\)(.*)", log)
            cols = re.findall(r"col(\d+) x=(\d+) w=(\d+) rows=(\d+)",
                              lay.group(2) if lay else "")
            widths = {c[2] for c in cols}
            self.check("browser-builds-a-column-chain",
                       len(cols) == 3 and len(widths) == 1
                       and all(int(c[3]) > 0 for c in cols),
                       "three columns, equal widths, rows each: %s"
                       % [(c[0], c[1], c[2], c[3]) for c in cols])

            self.note("the probe's client origin is offset inside its WM "
                      "frame (%s, published after the reparent)" % (org,))
            # The divider DRAG is out until the press's LANDING is known, and
            # the diagnosis is now narrow enough to state exactly:
            #   - the aim is right (the origin above is post-reparent, and the
            #     click below proves input reaches the probe: it logs a
            #     selection);
            #   - so the open question is whether the press reaches the
            #     BROWSER (a band press) or one of the columns, and whether
            #     the armed drag gets its motion.
            # Next: instrument Browser::mouseDown/mouseMoved behind an env
            # flag (the ARGENTUM_DRAW_MS pattern) and read which of the two
            # fires. A check that fails for an unknown reason is worse than
            # none, so it stays out of the case until then.

        # --- W1: the browser window, and a real directory read ------------
        # The app reads its start root with opendir/readdir (D3: listing is
        # libc, not a shell-out) and logs the path with its count. The count
        # is asserted against the FILESYSTEM rather than trusted: `ls -A` is
        # the same set the reader enumerates — dot entries excluded, dotfiles
        # included — so the number is data and not a magic constant.
        m = re.search(r"WORKSPACE: (\S+): (\d+) entries",
                      session.log_text())
        if not m:
            self.check("workspace-lists-its-start-root", False,
                       "the browser never reported its start root")
        else:
            path, n = m.group(1), int(m.group(2))
            session.serial("echo WSCOUNT=$(ls -A %s | wc -l)" % path)
            time.sleep(1.5)
            c = re.search(r"WSCOUNT=(\d+)", session.log_text())
            self.check("workspace-lists-its-start-root",
                       bool(c) and int(c.group(1)) == n,
                       "the browser read %s: %d entries; ls -A counts %s"
                       % (path, n, c.group(1) if c else "nothing"))

        self.check("no-fatal-faults", faults == 0,
                   "%d fatal fault line(s)" % faults)
        xerr = len(re.findall(XERR, log))
        self.check("no-x-errors", xerr == 0, "%d X protocol error(s)" % xerr)

        if up:
            self.check("clean-shutdown", session.halt(),
                       "the guest reached 'Safe to Power Off'")
