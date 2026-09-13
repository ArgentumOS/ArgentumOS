"""Clicking a dock tile launches its app; a second click raises it instead, and
a window drag ends when the button comes up - even mid fast motion.  It also
holds that a window is NOT on screen until it is fully drawn.

What it proves: the dock sits at the screen edge, a click on its first tile
starts the pinned app, the app's frame lands inside the work area (clear of the
dock, which owns a column of the desktop), the tile then shows a running dot,
clicking again raises the running app rather than starting a second copy, and a
flick drag leaves the frame where the release happened (a deferred drag end
kept the window on the pointer after the button was up).

This is also the case that exercises the pointer machinery in
tests/harness/monitor.py, so it is the one that says whether input driving
works at all: the guest drops ps/2 chunks sent too fast, and it loses a press
that comes with no motion.
"""

import re
import time

from harness import BaseCase

APP = "Widget Zoo"		# S5.2d: the app is a BUNDLE now
CALC = "Calculator"
DOCK_LINE = (r"KESTREL: dock (\w+) (\d+)x(\d+) at (\d+),(\d+) "
             r"tiles=(\d+) icon=(\d+)")
MANAGE_LINE = (r"KESTREL: manage 0x[0-9a-f]+ '%s' frame=0x[0-9a-f]+ "
               r"at (\d+),(\d+) (\d+)x(\d+)")
XERR = r"X Error|BadWindow|BadMatch|BadValue|BadDrawable"

# The dock's internal layout, mirroring kestrel.cpp (DOCK_PAD / TILE_GAP).
# The log gives the dock's box, its tile count and the icon size; the padding
# above the first tile and the gap between tiles are the WM's layout contract
# with this case.
DOCK_PAD = 8
TILE_GAP = 8

# The frame's client inset, also from kestrel.cpp: the frame window carries a
# 1px outline (its X border), then FRAME_PX of lip and the BAND_H title band
# before the client's own origin.
FRAME_PX = 4
BAND_H = 20


class Case(BaseCase):
    title = "the dock: a tile launches or raises, and a drag ends at the release"
    tier = "slow"
    timeout = 600

    def run(self, ctx):
        ctx.require_guest_file("WidgetZoo")
        ctx.require_guest_file("Calculator")
        session = ctx.boot()
        if not session.wait_for(r"KESTREL-READY", 150):
            self.check("session-up", False, "the desktop never came up")
            return
        self.check("session-up", True, "the desktop is running")

        dock = re.search(DOCK_LINE, session.log_text())
        self.check("dock-logged", bool(dock), "the WM reported its dock")
        if not dock:
            return

        side = dock.group(1)
        dw, dh, dx, dy = (int(dock.group(i)) for i in (2, 3, 4, 5))
        icon = int(dock.group(7))

        monitor = session.monitor()
        monitor.park()
        before = session.shot("before")
        self.check("dock-at-edge", dx + dw == before.w,
                   "the dock reaches the %s edge of a %d-wide screen (x %d..%d)"
                   % (side, before.w, dx, dx + dw))
        # Edge chrome: the dock spans the whole height left below the menubar
        # (it starts at the bar's bottom edge and reaches the screen's).  It is
        # inset only on the inside, where the work area keeps windows off it -
        # a vertical inset of its own left a gap above and below it.
        self.check("dock-full-height", dy + dh == before.h and dy > 0,
                   "the dock spans y %d..%d of %d (below the menubar)"
                   % (dy, dy + dh, before.h))

        # The first tile's centre, from the dock the WM logged - not from a
        # screen coordinate (a 1280-wide screen puts it elsewhere than 1920).
        tile_x = dx + dw // 2
        tile_y = dy + DOCK_PAD + icon // 2
        monitor.goto(tile_x, tile_y)
        monitor.click()

        launched = session.wait_for(r"KESTREL: dock launch '[^']*'", 45)
        self.check("tile-launches", launched,
                   "a click on the tile started the pinned app")
        managed = session.wait_for(r"KESTREL: manage 0x[0-9a-f]+ '%s'"
                                   % re.escape(APP), 45)
        self.check("app-managed", managed,
                   "the desktop manages the app's window")

        after = session.shot("launched")
        frame = re.search(MANAGE_LINE % re.escape(APP), session.log_text())
        if frame:
            fx, fy, fw, fh = (int(frame.group(i)) for i in (1, 2, 3, 4))
            inset = (fx + fw <= dx) if side == "right" else (fx >= dx + dw)
            self.check("frame-inset-from-dock", inset,
                       "frame %d,%d %dx%d stays clear of the dock at x=%d"
                       % (fx, fy, fw, fh, dx))
        else:
            self.check("frame-inset-from-dock", False,
                       "the desktop did not log the frame geometry")

        # The running dot: the tile itself changed once its app had a window.
        tile = (dx, tile_y - icon // 2, dx + dw, tile_y + icon // 2)
        changed = before.diff_box(after, tile)
        self.check("running-dot", changed > 0,
                   "%d px of the tile changed after its app was mapped" % changed)

        # The global menubar carries the ACTIVE app's menus.  The app draws
        # them itself, in its own bar-sized window (S4.2d), which the WM sizes
        # to the zone it logs:
        #
        #   KESTREL: menubar zone x=226 w=1510
        #
        # The WM's own half of the bar is now ONLY the desktop's system mark
        # (three lines in the accent colour): the active app's NAME is the
        # first menu of the APP's own bar, not something the desktop draws
        # (docs/design/argentum-hig.md §2).  So the desktop contributes no
        # ink right of that zone, and the app's menus must be right of it.
        # The app's bar window paints a moment after the WM maps it, so wait
        # for the ink instead of assuming it.
        def bar_ink_right(shot, x0=28):
            right = 0
            for x in range(x0, 900):
                if any(shot.luma(x, y) < 140 for y in range(6, 25)):
                    right = x
            return right

        was = bar_ink_right(before)		# the idle bar: Kestrel's own name
        zone = re.search(r"KESTREL: menubar zone x=(\d+) w=(\d+)",
                         session.log_text())
        self.check("menubar-zone-logged", bool(zone),
                   "the WM reported the app zone it gave the bar")
        zx = int(zone.group(1)) if zone else 0
        painted = None
        deadline = time.time() + 25
        while zx and time.time() < deadline:
            painted = session.shot("bar-wait")
            if bar_ink_right(painted, zx + 4) > 0:
                break
            time.sleep(1.0)
        ink_app = bar_ink_right(painted, zx + 4) if painted else 0
        self.check("app-menus-in-bar", zx > 0 and ink_app > zx + 20,
                   "the app's own bar window carries its menus: ink reaches "
                   "x=%d, right of the WM's mark zone (the zone starts at "
                   "x=%d; the desktop draws nothing right of it: %d)"
                   % (ink_app, zx, was))

        # The dropdown must HANG from the bar: its top edge is the bar's
        # bottom edge.  Report: "pulldown menus should appear with their tops
        # aligned to the bottom of the menubar, but they appear on top of it
        # instead" - the app's bar window sits ON the bar, so its origin is
        # the screen's top edge, and anchoring there covered the titles.  The
        # discriminator is the bar itself: while the menu hangs below it
        # nothing changes in the bar's rows (the app's bar has no hover or
        # pressed state), while an anchor at the window's origin covers them.
        run = []
        if painted is not None:
            for x in range(zx, 900):
                if any(painted.luma(x, y) < 140 for y in range(6, 25)):
                    run.append(x)
                elif run:
                    break
        self.check("bar-title-found", len(run) > 4,
                   "the app's first bar title is at x=%s"
                   % (("%d..%d" % (run[0], run[-1])) if run else "none"))
        # The whole of the first title, its words joined the way the layout
        # joins them (a gap of <= 14px is the same title).  `run` stops at
        # the first word, which is not enough to exclude "the pointer's
        # columns" below: the application menu's title is two words wide
        # ("Widget Zoo"), and its chip repaints under the click.
        ti0, ti1, x, gap = run[0], run[-1], run[-1] + 1, 0
        while x < 890 and gap <= 14:
            if any(painted.luma(x, y) < 140 for y in range(6, 25)):
                ti1, gap = x, 0
            else:
                gap += 1
            x += 1
        if len(run) > 4:
            tx = (run[0] + run[-1]) // 2
            monitor.park()
            ref = session.shot("bar-ref")
            monitor.goto(tx, 15)
            monitor.click()
            opened = session.shot("menu-open")
            below = opened.diff_box(ref, (tx - 60, 33, tx + 260, 130))
            # the bar's own rows, minus the clicked title's own extent (the
            # click arms its chip and the guest repaints exactly that)
            bar_changed = (opened.diff_box(ref, (28, 6, ti0 - 12, 27)) +
                           opened.diff_box(ref, (ti1 + 12, 6, 900, 27)))
            self.check("menus-drop-below-the-bar",
                       below > 800 and bar_changed < 400,
                       "the dropdown fills %d px below the bar (y 33..130) and "
                       "leaves the bar's own rows alone (%d px; an anchor at "
                       "the bar window's origin covers them)"
                       % (below, bar_changed))
            # The title whose menu is open must SAY so: it draws dark (the
            # theme's armed chip) for as long as its dropdown is up, and back
            # to chrome once the menu closes however it closes. Sampled just
            # left of the first glyph, inside the title's own hit zone, so the
            # pixel is background in both states and not a glyph.
            bgx = max(run[0] - 4, 2)
            idle_bg = ref.luma(bgx, 15)
            open_bg = opened.luma(bgx, 15)
            monitor.goto(1700, 900)		# dismiss: the popup takes it
            monitor.click()

        # --- the bar's hit-test, and Mac-like menu tracking ---------------
        # The title LAYOUT is in pixels (barTitleLayout measures with
        # pxPerPt) while a view responder's mouse event is in view-local
        # POINTS: comparing them put the hit zones left of the painted
        # titles and drifted further off the further right you clicked, so
        # clicking a title opened a DIFFERENT title's menu ("the wrong one
        # gets the click").  Then: on the Mac, once a menu is open, moving
        # the pointer onto another title opens that one and closes the
        # previous.  Both are asserted through what the app DOES: the zoo's
        # first menu's first item is "About Zoo" (ZOO-ACT: menu:about) and
        # its second menu's first item is "Reset Values" (menu:reset).
        runs = []
        if painted is not None:
            cur = []
            for x in range(zx, 900):
                if any(painted.luma(x, y) < 140 for y in range(6, 25)):
                    cur.append(x)
                elif cur:
                    if cur[-1] - cur[0] > 4:
                        runs.append([cur[0], cur[-1]])
                    cur = []
            if cur and cur[-1] - cur[0] > 4:
                runs.append([cur[0], cur[-1]])
        merged = []
        for r in runs:
            if merged and r[0] - merged[-1][1] <= 14:
                merged[-1][1] = r[1]		# words of one title
            else:
                merged.append(r)
        # The guideline (docs/design/argentum-hig.md §2) fixes this order:
        # the application menu first, then the standard menus the app has
        # items for, then the app's own — and the zoo omits File/Edit/
        # Windows/Help because it has nothing for them. So its bar is
        # [Widget Zoo] [View] [Widgets], and the menu whose first row is
        # Reset Values (menu:reset) is the THIRD title, not the second.
        self.check("bar-titles-found", len(merged) >= 3,
                   "the app's bar carries the application menu, View and "
                   "its own menu, in that order: %s" % (merged,))
        if len(merged) >= 3:
            ax = (merged[0][0] + merged[0][1]) // 2	# the application menu
            bx = (merged[2][0] + merged[2][1]) // 2	# the app's own menu
            rowy = 45					# first dropdown row

            about = session.count(r"ZOO-ACT: menu:about")
            reset = session.count(r"ZOO-ACT: menu:reset")
            monitor.park()
            monitor.goto(bx, 15)
            monitor.click()			# the app's own menu
            monitor.goto(bx, rowy)
            monitor.click()			# its first row
            self.check("bar-hit-test-is-right",
                       session.count(r"ZOO-ACT: menu:reset") > reset
                       and session.count(r"ZOO-ACT: menu:about") == about,
                       "clicking the app's own title dropped THAT title's "
                       "menu (menu:reset ran, menu:about did not)")

            # Mac-like menu tracking: open the application menu, then MOVE
            # the pointer onto the app's own menu WITHOUT clicking.  The popup holds the
            # pointer, so this motion reaches IT (not the bar) and the bar
            # answers which title it is over: the menu under the pointer
            # must already be the second one when the click lands.
            about = session.count(r"ZOO-ACT: menu:about")
            reset = session.count(r"ZOO-ACT: menu:reset")
            monitor.park()
            monitor.goto(ax, 15)
            monitor.click()			# the FIRST title
            monitor.goto(bx, 15)		# motion only: no button
            monitor.goto(bx, rowy)
            monitor.click()			# the swapped menu's first row
            self.check("menu-tracks-on-hover",
                       session.count(r"ZOO-ACT: menu:reset") > reset
                       and session.count(r"ZOO-ACT: menu:about") == about,
                       "moving onto the app's own title swapped the open "
                       "menu (menu:reset ran, not menu:about)")
            dismissed = session.shot("menu-dismissed")
            back_bg = dismissed.luma(bgx, 15)
            self.check("open-title-goes-dark",
                       idle_bg - open_bg > 20 and back_bg - open_bg > 20,
                       "the open menu's title is dark while its dropdown is up "
                       "(idle %d -> open %d -> closed %d at x=%d)"
                       % (idle_bg, open_bg, back_bg, bgx))
            # The chip must HUG the title EVENLY.  drawText() insets a run's
            # ink by a raster pad inside the box it is handed, so a chip drawn
            # from the box's left edge alone pads the left by (BAR_CHIP_PAD +
            # the pad) and the right by (BAR_CHIP_PAD - the pad): a visibly
            # lopsided highlight ("more on the left than on the right of the
            # text").  ti0/ti1 above are that whole extent.
            # The chip, on the OPEN bar: any column whose middle row is not the
            # bar's own tone (the chip's fill AND the inverted label differ)
            bg = ref.luma(zx + 1, 15)
            cl, cr = ti0, ti1
            while cl > 2 and abs(opened.luma(cl - 1, 15) - bg) > 20:
                cl -= 1
            while cr < 890 and abs(opened.luma(cr + 1, 15) - bg) > 20:
                cr += 1
            self.check("bar-chip-hugs-the-title-evenly",
                       abs((ti0 - cl) - (cr - ti1)) <= 2,
                       "the open title's chip pads the text evenly (%d px "
                       "left, %d px right; chip %d..%d around ink %d..%d)"
                       % (ti0 - cl, cr - ti1, cl, cr, ti0, ti1))

            # --- the bar's own rules, on pixels (2026-09) ------------------
            # 1. the highlight is a RECTANGLE filling the bar from its top
            #    border to its bottom border - not a rounded chip inset into
            #    the bar, which reads as a separate object rather than as the
            #    item being held down.
            def chip_cols(y):
                return [x for x in range(zx, 400)
                        if abs(opened.luma(x, y) - bg) > 20]

            top, bot = chip_cols(1), chip_cols(dy - 2)
            self.check("bar-highlight-fills-the-bar",
                       bool(top) and bool(bot) and cl >= top[0] and
                       cr <= top[-1] and cl >= bot[0] and cr <= bot[-1],
                       "the open title's highlight fills the bar top to "
                       "bottom (columns at row 1: %s; row %d: %s; the chip is "
                       "%d..%d)"
                       % (("%d..%d" % (top[0], top[-1])) if top else "-",
                          dy - 2,
                          ("%d..%d" % (bot[0], bot[-1])) if bot else "-",
                          cl, cr))
            # 2. the dropdown is flush with that highlight, and 3. its top row
            #    IS the bar's bottom border row: the popup is anchored one row
            #    above the bar's floor, so its content starts ON the bar's
            #    floor (y = the bar's height) rather than one row below it.
            prow = [x for x in range(0, 700)
                    if opened.px(x, dy + 4) != ref.px(x, dy + 4)]
            self.check("menu-is-flush-with-the-highlight",
                       bool(prow) and prow[0] == cl,
                       "the dropdown's left edge is the highlight's left edge "
                       "(%s vs %d)"
                       % (prow[0] if prow else "none", cl))
            first = None
            for y in range(dy - 8, dy + 8):
                r = [x for x in range(0, 700)
                     if opened.px(x, y) != ref.px(x, y)]
                if r and len(r) > 150:
                    first = y
                    break
            self.check("menu-overlaps-the-bar-border",
                       first == dy,
                       "the dropdown's top border IS the bar's bottom border "
                       "(its content starts on the bar's floor, y=%s; the bar "
                       "is %d px tall)" % (first, dy))

        # 3. the desktop's system menu carries no title: three lines in the
        #    theme's ACCENT colour (a filled tile, or any text after it, is
        #    what this replaced).  Bands are counted only when they are wide
        #    enough to be a line - the strip's rounded top-left corner also
        #    puts a few dark pixels in this box.
        bands, prev = 0, False
        for y in range(3, dy - 3):
            xs = [x for x in range(4, zx) if ref.luma(x, y) < 185]
            line = bool(xs) and (xs[-1] - xs[0]) >= 8
            if line and not prev:
                bands += 1
            prev = line
        rgb = ref.px(10, 11)
        self.check("system-menu-is-a-three-line-icon",
                   bands == 3 and (max(rgb) - min(rgb)) >= 20,
                   "the system menu is three accent-coloured lines (%d band(s), "
                   "rgb=%s at the middle line)" % (bands, rgb))

        monitor.goto(tile_x, tile_y)
        monitor.click()
        raised = session.wait_for(r"KESTREL: dock raise '[^']*'", 45)
        self.check("second-click-raises", raised,
                   "the second click raised the running app")
        launches = session.count(r"KESTREL: dock launch '[^']*'")
        self.check("no-second-launch", launches == 1,
                   "%d launch(es) for two clicks" % launches)

        # --- the drag must END at the release ---------------------------
        # A drag used to be ended by a TIMER: the release counted only once
        # the pointer had been quiet for a while (a full second when the
        # release landed mid fast motion), and motion with the button up
        # moved the window AND re-armed that clock.  So a flick followed by
        # any hand movement kept the window on the pointer for as long as
        # the hand kept moving - "it sticks after I let go".  The end is
        # the release itself now.  Drag by a known delta, release, then
        # move the pointer FAR without the button: the frame must stay
        # where the release left it.
        if frame:
            flick = [(40, 0), (40, 0), (40, 0), (30, 0), (0, -30)]
            net = [0, 0]

            monitor.goto(fx + fw // 2, fy + BAND_H // 2)
            monitor.nudge()		# a bare button change is dropped
            monitor.send("mouse_button 1")
            for fdx, fdy in flick:
                monitor.move(fdx, fdy)
                net[0] += fdx
                net[1] -= fdy		# monitor dy is positive UP
            monitor.send("mouse_button 0")	# the release, mid-motion
            monitor.move(200, 0)		# the hand keeps going: 200px
            time.sleep(1.5)			# a deferred end would land here

            end = None
            for line in session.log_text().splitlines():
                hit = re.search(r"KESTREL: move 0x[0-9a-f]+ '%s' to (\d+),(\d+)"
                                % re.escape(APP), line)
                if hit:
                    end = (int(hit.group(1)), int(hit.group(2)))
            want = (fx + net[0], fy + net[1])
            self.check(
                "drag-ends-at-release",
                end is not None and abs(end[0] - want[0]) < 40
                and abs(end[1] - want[1]) < 40,
                "the frame ended at %s; the release was at %d,%d (a window "
                "still following the pointer would land near %d,%d)"
                % (("%d,%d" % end) if end else "never",
                   want[0], want[1], want[0] + 200, want[1]))

        # --- the app's bar goes with the app and comes back with it ------
        # The report this guards: after closing and restarting the widget zoo
        # the bar showed no menus, though they were there and reacted to
        # clicks.  The titles were Kestrel's to draw then, in the strip whose
        # repaints never reached fb0.  Now they are the app's own window, which
        # the WM places and maps only while that app is focused: a closed app's
        # titles go with it, a relaunched app's must come back.
        #
        # The close box, not killall: toybox's killall reads /proc, which this
        # boot's mounts do not expose (it answers "killall: no /proc").  The
        # box goes through WM_DELETE_WINDOW, which is the same "the app is
        # gone" event the WM sees - DestroyNotify on its windows - and the one
        # a user reaches.
        fpos = None
        for line in session.log_text().splitlines():
            hit = re.search(r"KESTREL: move 0x[0-9a-f]+ '%s' to (\d+),(\d+)"
                            % re.escape(APP), line)
            if hit:
                fpos = (int(hit.group(1)), int(hit.group(2)))
        self.check("frame-position-known", fpos is not None,
                   "the frame's position is known (for its close box)")
        if fpos:
            monitor.park()
            monitor.goto(fpos[0] + 12, fpos[1] + 10)	# the close box
            monitor.click()
            closed = session.wait_for(
                r"KESTREL: close-request 0x[0-9a-f]+ '%s'" % re.escape(APP), 20)
            gone = session.wait_for(
                r"KESTREL: unmanage 0x[0-9a-f]+ '%s'" % re.escape(APP), 20)
            self.check("app-closes", closed and gone,
                       "the close box closed the app (close-request=%s, "
                       "unmanage=%s)" % (closed, gone))
            monitor.park()
            killed = session.shot("closed")
            # both directions: the app's OWN bar window is gone (nothing
            # right of the zone) and the WM's half is Kestrel's again - a
            # closed app used to keep its name in the bar until the next
            # clock tick repainted the strip.
            gone_own = bar_ink_right(killed, zx + 4)
            ink_gone = bar_ink_right(killed)
            self.check("bar-clears-with-the-app",
                       gone_own == 0 and ink_gone < was + 40,
                       "with the app closed its own bar is gone (no ink right "
                       "of x=%d) and the bar's text falls back to x=%d (was "
                       "%d with no app)" % (zx, ink_gone, was))

        # relaunch it from its tile and the bar must come back
        want = session.count(r"KESTREL: dock launch '[^']*'") + 1
        zones = session.count(r"KESTREL: menubar zone x=") + 1
        monitor.goto(tile_x, tile_y)
        monitor.click()
        deadline = time.time() + 45
        while (time.time() < deadline and
               (session.count(r"KESTREL: dock launch '[^']*'") < want or
                session.count(r"KESTREL: menubar zone x=") < zones)):
            time.sleep(0.5)
        ink_back = 0
        deadline = time.time() + 25
        while time.time() < deadline:
            again = session.shot("relaunched")
            ink_back = bar_ink_right(again, zx + 4)
            if ink_back > zx + 20:
                break
            time.sleep(1.0)
        self.check("menus-return-after-restart", ink_back > zx + 20,
                   "after closing and relaunching the app its own bar window "
                   "is back: ink reaches x=%d, right of the zone at x=%d"
                   % (ink_back, zx))

        # --- S5.2d: a tile names a BUNDLE -------------------------------
        # The tiles carry no path any more: each names /Applications/<name>.app,
        # and the WM reads that bundle's manifest — a .conf file, read with
        # libconfig, the same grammar as every other config file — validates
        # it, and execs the payload it names.  The second tile is the
        # Calculator, a real app, so this leg exercises the app and not just
        # its launch: its own key grid is clicked through 12+3= and the
        # arithmetic is asserted from the app's log.
        calc_y = dy + DOCK_PAD + icon + TILE_GAP + icon // 2
        monitor.goto(tile_x, calc_y)
        monitor.click()
        self.check("calculator-tile-launches",
                   session.wait_for(r"KESTREL: dock launch 'Calculator'", 45),
                   "the second tile launched its bundle")
        self.check("calculator-managed",
                   session.wait_for(r"KESTREL: manage 0x[0-9a-f]+ '%s'"
                                    % re.escape(CALC), 45),
                   "the desktop manages the Calculator's window")

        bundles = re.findall(r"KESTREL: bundle '([^']+)' ok name=\"([^\"]+)\" "
                             r"executable=(\S+)", session.log_text())
        # (the zoo is launched twice in this run - the first click and the
        # relaunch after its close box - so the resolved set is what matters)
        resolved = sorted(set(b[0] for b in bundles))
        self.check("bundle-manifests-validated",
                   resolved == ["Calculator.app", "Widget Zoo.app"],
                   "both tiles resolved a bundle whose manifest validated: %s"
                   % "; ".join("%s name=%s exe=%s" % b
                               for b in sorted(set(bundles))))

        # The Calculator's own bar window must reach the bar's app zone, the
        # same measure the zoo leg uses - the zone is the WM's own half, so it
        # is re-read (a different app name means a different zone width).
        zones = re.findall(r"KESTREL: menubar zone x=(\d+) w=(\d+)",
                           session.log_text())
        czx = int(zones[-1][0]) if zones else zx
        ink_calc = 0
        deadline = time.time() + 25
        while time.time() < deadline:
            shot = session.shot("calc-bar")
            ink_calc = bar_ink_right(shot, czx + 4)
            if ink_calc > czx + 20:
                break
            time.sleep(1.0)
        self.check("calculator-bar-in-zone", ink_calc > czx + 20,
                   "the Calculator's own bar window carries its menu: ink "
                   "reaches x=%d, right of the zone at x=%d"
                   % (ink_calc, czx))

        # 1 2 + 3 = / the grid's own geometry: the app logs the grid in window
        # px and the WM logs the frame, so the click targets are derived, not
        # guessed (the frame's client origin is its 1px outline + lip + band).
        grid = re.search(r"CALC: grid (\d+)x(\d+) at (\d+),(\d+) cell (\d+)x(\d+) "
                         r"gap (\d+)", session.log_text())
        calc_frame = re.search(MANAGE_LINE % re.escape(CALC), session.log_text())
        self.check("calculator-grid-logged", bool(grid and calc_frame),
                   "the app logged its key grid and the WM logged its frame")
        result = None
        if grid and calc_frame:
            (_gc, _gr, gx0, gy0, cw, ch, gap) = (int(grid.group(i))
                                                 for i in range(1, 8))
            ox = int(calc_frame.group(1)) + 1 + FRAME_PX
            oy = int(calc_frame.group(2)) + 1 + BAND_H
            # the key grid, row 0 at the top: row 3 is 1 2 3 +, row 4 ends =
            for row, col in ((3, 0), (3, 1), (3, 3), (3, 2), (4, 3)):
                monitor.goto(ox + gx0 + col * (cw + gap) + cw // 2,
                             oy + gy0 + row * (ch + gap) + ch // 2)
                monitor.click()
            deadline = time.time() + 20
            while time.time() < deadline:
                hit = re.search(r"CALC-ACT: result (\S+)", session.log_text())
                if hit:
                    result = hit.group(1)
                    break
                time.sleep(0.5)
        self.check("calculator-arithmetic", result == "15",
                   "clicking 1 2 + 3 = on the key grid produced %s (the app's "
                   "own log of the result)" % (result or "no result"))

        # --- security (audit 2026-09): a hostile GEOMETRY ----------------
        # Any X client can resize another client's window (X11 checks no
        # window ownership on ConfigureWindow) and X sizes are CARD16, so a
        # 65535x65535 ConfigureNotify is untrusted input aimed at a surface
        # allocation where `w*h*4` in 32-bit `unsigned` wraps: a tiny
        # surface, then rows copied past its end (the victim may be the
        # WM).  The toolkit clamps at the wire entry now; this drives the
        # attack and asserts the clamp fired, the victim survived (the WM
        # still raises its window) and the session is unharmed.
        client = re.search(r"KESTREL: manage (0x[0-9a-f]+) '%s'"
                           % re.escape(APP), session.log_text())
        self.check("client-id-known", bool(client),
                   "the WM logged the client window it manages")
        session.run("DISPLAY=:0 /System/Shared/tests/rogue_resize %s '%s'"
                    % (client.group(1) if client else "0", APP), secs=45)
        # the size that arrives may already be capped by the server or the
        # WM (measured: 65535 arrives as 16384), so the property is the
        # ORDER — an over-bound size must be clamped and reported, not
        # allocated — not one literal number
        clamped = session.wait_for(
            r"ARGENTUM: window (width|height) (1[6-9]\d{3}|[2-9]\d{4,}) "
            r"clamped to 8192", 20)
        self.check("rogue-resize-clamped", clamped,
                   "a hostile 65535x65535 resize of the app's window was "
                   "clamped at the toolkit's wire entry (the server caps it "
                   "at 16384 first; the toolkit caps THAT at 8192 and says "
                   "so)")
        monitor.goto(tile_x, tile_y)
        monitor.click()
        self.check("victim-survives-hostile-resize",
                   session.wait_for(r"KESTREL: dock raise '%s'" % re.escape(APP),
                                    30),
                   "the app and the WM are both still alive after the hostile "
                   "resize (the dock raised its window again)")

        # --- a window is not on screen until it is DRAWN ----------------
        # krel_slow sleeps 6s inside its FIRST paint.  A first paint is not
        # a normal paint: it fills the text, mask and glyph caches, so it
        # costs an order of magnitude more than the repaints after it
        # (measured ~450ms cold against ~35ms warm for the zoo's board).
        # Mapping first therefore showed a blank window for that whole
        # time.  Composing BEFORE the map means that while it works there
        # is not even a MapRequest, so the screen must be untouched until
        # the window is already drawn - and then it must appear.
        quiet = session.shot("undrawn-before")
        session.serial("DISPLAY=:0 /System/Shared/tests/krel_slow &")
        if session.wait_for(r"KREL-SLOW-PAINTING", 60):
            time.sleep(2)		# inside the 6s first paint
            during = session.shot("undrawn-painting")
            painted = during.diff_box(quiet, (0, 40, 1920, 1080))
            self.check("window-hidden-while-it-paints", painted <= 4,
                       "nothing of the window is on screen while it paints "
                       "its first frame (%d px changed below the bar)"
                       % painted)
        else:
            self.check("window-hidden-while-it-paints", False,
                       "the slow probe never reported its first paint")
        session.wait_for(r"KREL-SLOW-DRAWN", 60)
        session.wait_for(r"KREL-SLOW-READY", 60)
        time.sleep(1)
        drawn = session.shot("undrawn-drawn")
        appeared = drawn.diff_box(quiet, (0, 40, 1920, 1080))
        self.check("window-appears-already-drawn", appeared > 1000,
                   "the window is on screen once it is drawn (%d px changed "
                   "below the bar)" % appeared)
        session.serial("killall krel_slow 2>@null")
        time.sleep(1)

        self.check("no-x-errors", session.count(XERR) == 0,
                   "no X protocol error")
