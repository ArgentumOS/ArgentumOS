# The FNX test harness

`make test` boots the assembled Argentum OS under QEMU and asserts on what it
actually does: what the guest logs, what it draws on the framebuffer, and how
it answers input. A collaborator who has built the images can reproduce every
result in here.

    make test                 # the fast tier (a few minutes)
    make test-all             # fast + slow (boots one guest per slow case)
    make test TESTS=audio     # one case; globs work: TESTS='wm_*'
    make test-list            # the cases, their tiers and their timeouts

Exit status is `0` when everything selected passed, `1` when something failed,
and `2` when the harness cannot run here at all (a missing image, no QEMU) - in
which case the message names the make target that fixes it.

## Prerequisites

| what | how |
| --- | --- |
| QEMU | `qemu-system-x86_64` on the host, e.g. Debian's `qemu-system-x86`. `tools/qemu.sh` prefers a system QEMU (its module directory matches its binary, so host audio backends load) and falls back to a tools prefix via `FNX_QEMU_TOOLS`, or an explicit `FNX_QEMU_BIN`. `./tools/qemu.sh --version` says which one you got. |
| firmware | `.build/ovmf/OVMF.fd`, fetched by `make ovmf` (a `make test` prerequisite) |
| kernel ESP | `.build/esp.img` - `make` then `./tools/mkesp.sh` |
| root image | `.build/rootagfs.img` - `make rootagfs` |

A case that needs an artifact it does not have **skips** with that instruction
rather than failing, so a fresh clone gives a readable "run make rootagfs"
instead of a wall of red. Cases that need a program inside the image check for
it first (`ctx.require_guest_file("widget_zoo")`).

Nothing here rebuilds the images: a test run tests what is on disk, and the
runner warns when `.build/rootagfs.img` is older than the newest source file
that feeds it. Use `make rootagfs` before trusting a green run after a change.

## Layout

    tests/run.py            the entry point (make test calls this)
    tests/harness/          machinery shared by every case
      paths.py              repo/image/firmware locations, FNX_TEST_* knobs,
                            prerequisite checks, image staleness
      qemu.py               Session: launch, marker-synced log waits, serial
                            commands, screendump, halt, guaranteed teardown
      monitor.py            the QEMU monitor: pointer, keys, screendumps
      image.py              Shot: pixels, luma, light/dark fractions, ink,
                            row spans, shot-to-shot diffs
      case.py               BaseCase / Context / Check / Skip
      runner.py             case discovery, tiers, timeouts, reporting
    tests/cases/*.py        one file per case; the file name is the case name

Artifacts from the last run - the guest log per case and its screenshots - are
left in `.build/tests/<case>/` for debugging a failure.

`tests/` is the **host-side** harness. `userland/tests/` is a different thing:
guest-side probe programs that are built into the image and installed at
`/System/Shared/tests/<name>`. A host case usually drives one of those probes
and asserts on what it prints.

## Environment knobs

| variable | default | meaning |
| --- | --- | --- |
| `FNX_TEST_MEM` | `256M` | guest RAM for cases that do not ask for a size |
| `FNX_TEST_ROOTIMG` | `.build/rootagfs.img` | boot a different root image |
| `FNX_TEST_AUDIODEV` | `none` | host audio backend. `none` is silent but real (the card is attached either way); `pipewire`/`pa`/`alsa` make a run audible |
| `FNX_TEST_MACHINE` | `pc,usb=off` | the QEMU machine type |
| `FNX_TEST_TIER` | `fast` | default tier for the runner |
| `FNX_TEST_MEM_LIST` | `256M 1G 2G 4G 8G` | sizes the `boot_matrix` case boots |
| `FNX_TEST_ALLOW_OTHER_QEMU` | unset | boot even though another QEMU is running |

The harness refuses to start while another QEMU is running, because a leaked
guest holds a write lock on `.build/esp.img` and the next boot fails with
"Failed to get write lock", which reads like a harness bug. `make qemu-kill`
clears it.

## Writing a case

One file in `tests/cases/`; the file name is the case name, so `audio.py` is
`make test TESTS=audio`. Subclass `BaseCase`, implement `run(ctx)`, and call
`self.check(name, ok, detail)` for each thing you want to prove:

```python
"""What this case proves, in one paragraph, for whoever reads the failure."""

import re

from harness import BaseCase

DOCK_LINE = r"KESTREL: dock (\w+) (\d+)x(\d+) at (\d+),(\d+) tiles=(\d+) icon=(\d+)"


class Case(BaseCase):
    title = "one line for `make test-list`"
    tier = "fast"        # "fast" runs in `make test`; "slow" needs `make test-all`
    timeout = 300        # wall-clock budget for the whole case, in seconds
    needs_boot = True    # False = host-only; the runner then skips QEMU prereqs

    def run(self, ctx):
        ctx.require_guest_file("my_probe")          # or SKIP with instructions
        session = ctx.boot_to_desktop(secs=150)     # waits for KESTREL-READY
        log = session.log_text()

        self.check("session-up", "KESTREL-READY" in log, "the desktop is running")

        dock = re.search(DOCK_LINE, log)
        if not dock:
            self.check("dock-logged", False, "the WM did not report its dock")
            return
        dw, dh, dx, dy = (int(dock.group(i)) for i in (2, 3, 4, 5))

        shot = session.shot("desktop")
        self.check("dock-at-edge", dx + dw == shot.w,
                   "the dock reaches the edge (x %d..%d of %d)" % (dx, dx + dw, shot.w))

        session.serial("my_probe")                  # a command on the serial console
        self.check("probe-ran", session.wait_for(r"MY-PROBE-OK", 60), "the probe ran")

        self.check("no-fatal-faults", session.count(r"KERNEL EXCEPTION") == 0,
                   "no kernel exception in the guest log")
```

`ctx` gives you `boot(**kw)` / `boot_to_desktop()`, `require_guest_file()`,
`require_exists()`, `host(argv)` for a host command, and `artifact(name)` for a
file to keep. Every session a case starts is killed when the case ends,
including when it times out.

### The four rules

These are not style preferences. Each one is a bug that cost a day:

1. **Wait for a marker the guest prints, never a fixed sleep.** `KESTREL-READY`
   is a marker; `"# "` is not (a boot banner line contains it, which is how a
   gate once "synced" against the wrong thing). `session.wait_for(pattern, secs)`.
2. **Derive geometry from the log, never assume a screen size.** The default
   resolution and the dock's edge have both changed. Parse the line the
   component printed (`KESTREL: dock ...`) and compute from that.
3. **Assert colours the configuration produced, not magic triples.** The
   wallpaper logs its ramp (`base=0x… top=0x… bot=0x…`); check a sampled pixel
   is inside that range. A hardcoded `(34, 136, 238)` fails the moment someone
   changes the theme.
4. **Compare like with like.** Before/after shots of the whole screen count the
   window that just appeared; use `Shot.diff_box(other, box)` to ask about the
   region you mean.

### Driving the pointer

`session.monitor()` returns a `Monitor` with `park()`, `goto(x, y)`, `click()`,
`key(name)` and `screendump(path)`. It already encodes what the guest needs:
moves in `<=127` chunks, one axis at a time, and a 1px nudge bracketing each
button change (the guest drops ps/2 chunks sent faster than it drains them, and
loses a press that arrives with no motion). The emulated pointer starts at
`(0, 0)` and the guest never reports where it is, so `Monitor.pos` tracks it -
always `park()` before an absolute move, and drive a whole interaction from one
monitor socket.

## Tiers

`fast` cases run in `make test`: one boot each, a few minutes in total, and they
are the ones worth running before every commit. `slow` cases are in
`make test-all`: several boots, or input-driven interactions that need settling
time. Keep a fast case fast; put the exhaustive matrix (every RAM size, every
filesystem) in a slow case.

## What is deliberately not here

No pytest, no CI config: the cases need a QEMU boot and a root image, so they
are integration tests by construction, and a `make test` entry point is friendlier
to whoever is bisecting a boot failure than a Python test framework would be.
The runner speaks the same `PASS name: detail` / `TESTS-OK n/n` vocabulary the
older ad-hoc gates used.
