# Test coverage

The suite's target is **comprehensive coverage of the system**, measured in
*checks*, not in boots: one boot can drive many probes and assert many things,
so the fast tier stays a couple of minutes while the number of assertions grows.
This file is the map: what is covered, by which case, what the first
probe-driven run found, and what is still a gap. Update it whenever a case
lands - an unlisted gap is a gap nobody is tracking.

Run `make test-list` for tiers and timeouts; `tests/README.md` has the authoring
contract. A check whose behaviour is known to be broken is marked
`xfail="reason"`: it reports XFAIL and does not fail the run, and it reports
XPASS (which *does* fail) once the behaviour is fixed, so the marker gets
removed rather than forgotten.

## Covered

| area | case | what is actually asserted |
| --- | --- | --- |
| Boot to a working desktop | `smoke_desktop` | root mounted, session up, guest mode vs screendump size, wallpaper ramp on screen, menu bar and dock drawn, clock text, zero fatal faults, zero X errors, clean shutdown |
| Guest RAM / memory map | `boot_matrix` (slow) | desktop + zero fatal faults at every supported size (256M…8G) |
| Filesystem (AGFS root) | `fs_agfs` | the mount table (`mount`), a create/write/read round trip, `cp` + `cmp` agreeing, `chmod` + `acl get`, a config domain reading through |
| procfs / devfs / devpts | `procfs_devfs` | the procfs tree under `/System/Processes` (version, meminfo, self/status, the tree itself), the devfs bus directories, the pty multiplexer, the zero device, and the devpts mount being listed without error |
| Audio | `audio` | the HDA driver claims the card, an OSS device exists, `/System/Tools/tone` writes samples, no host backend errors |
| Window manager input | `wm_dock` (slow) | dock geometry at the screen edge, a tile click launches its app, the frame stays clear of the dock column, the running dot appears, a second click raises without relaunching |
| Repository hygiene | `host_fshlint` | the FSH path linter is clean over the staged userland (no QEMU) |

## What the first probe-driven run found

Ground truth from a real boot, because the drafts were written from a source
survey and got the layout wrong. This is the part that pays for the harness:

1. **There is no `/proc` and no `/dev`.** The FSH layout mounts procfs at
   `/System/Processes`, devfs at `/System/Devices`, devpts at
   `/System/Devices/PTS/pts`, the ESP at `/System/ESP`. `mount` prints
   `/dev/root on / type agfs`, and `/System/Processes/mounts` agrees. Any test
   (or tool) written against conventional paths fails for that reason alone.
2. **Kernel NULL-dereference panic listing the devpts mount** - **FOUND,
   ROOT-CAUSED AND FIXED.** `ls /System/Devices/PTS/pts` →
   `KERNEL EXCEPTION vector 0x0e error=0x00 cr2=0x60` → the guest halted: a
   page fault at offset 0x60 (`fsop->followlink`) of a NULL `fsop`. The root
   cause was not in devpts: `devpts_read_inode()` chooses the root inode's
   fsop with `cond ? &devpts_dir_fsop : &def_chr_fsop`, clang if-converted
   that into a `cmov` that loaded the symbol's first 8 bytes instead of its
   address (`tools/patch_pic_data.py` explains the relocation trap), and the
   root inode came back with `fsop == NULL` - an unreachable mount. Marking
   both fsop declarations `visibility("hidden")` makes clang emit direct
   PC-relative access, and the tool now *fails the build* on any such form it
   cannot rewrite. Checks: `procfs_devfs/devpts-listing-does-not-panic` and
   `procfs_devfs/devpts-mount-reachable`.
3. **The devfs role aliases are missing from `/System/Devices`.** The kernel
   builds them (`fs/devfs/super.c`, `devfs_aliases()`: `console`, `tty`,
   `ptmx`, `pts`, `kbd`, `mouse`, `psaux`, `fb0`, `dsp`, and the `Memory/*`
   names), but a listing of `/System/Devices` shows only the bus directories
   (`Audio Disk Display Memory PS2 PTS Serial TTY`). So anything opening
   `/System/Devices/fb0` or `…/dsp` fails. Not yet a check - it needs a case
   that asserts the alias set.
4. **`userland/tests/test_toybox.sh` is stale.** It drives `/bin/sh`, `/bin/*`
   style paths from before the FSH migration, so it stops after its second
   step. It is the committed smoke test for the applet set; it needs rewriting
   against `/System/Tools`.
5. **`sec_test` reports 21 of 24 invariants passing.** Three of the security
   audit's regression checks fail on a current build. That run has not been
   triaged into which three, and the temporary case that ran it was removed
   pending that (see the gaps list) - it is the most valuable open finding.
6. The AGFS metadata probes (`agfsdir`, `agfsattr`, `agfsxattr`) are now built
   into the image but their expectations are not established: one of them
   printed 18 FAIL lines and another printed nothing at all without arguments.
   Ground-truth them before turning them into checks.

## Gaps, in the order I intend to close them

1. **Triage `sec_test`'s three failures**, then land the case (the battery, the
   shm size/leak probes and `test_mmap` - all already built into the image).
2. **The devfs alias set** - a case that asserts the role aliases the kernel
   builds. (The **devpts listing panic** that sat next to it is fixed; see the
   findings above.)
3. **Network protocols** - only the boot path (DHCP, `eth0`) and the root-only
   `AF_PACKET` refusal are covered. Nothing asserts UDP/TCP/ICMP
   (`net/ipv4.c`), ARP (`net/ext_net.c`), the AF_UNIX sockaddr rules, or that
   `ping` answers. Highest-value gap: a NIC can probe and still not move a byte.
4. **FAT/exFAT write paths** - `fs/fatfs/write.c` and `exwrite.c` have no guest
   round trip; the ESP is mounted FAT at boot, so it is cheap. ext2/minix read
   paths and `EROFS` on iso9660 belong here too.
5. **ACL enforcement end to end** - `fs_agfs` only proves the tool answers. The
   ACL is the single permissions model, so a case must create a file, set an
   ACL for a second user, and assert the *decision* changes, including
   default-ACL inheritance at create.
6. **Configuration precedence** - `system` → `user` → `shared` resolution, and
   the failure path for a malformed record.
7. **Signals and process semantics** - delivery, the SIGBUS-vs-SIGSEGV
   distinction the fault policy relies on, `rt_sigaction` flags.
8. **Syscall surface** - xattr (188–199), inotify (253–255, 294), epoll
   (213, 232, 233, 281, 291), futex (202), SysV IPC beyond shm: a probe that
   calls each cluster and reports non-`-ENOSYS` catches a table regression.
9. **Block I/O read-back** - drivers probe and register, but nothing reads a
   known sector back inside the guest.
10. **USB storage hotplug** - attach/detach and a read-back.
11. **Crash consistency** (slow) - XBFS/AGFS kill cycles: write, kill, remount,
    assert.
12. **UI toolkit acceptance** - the ~15 committed Argentum probes
    (`viewtree_*`, `widgets_*`, `structure_*`, `textview_*`) draw but print no
    markers, so each needs its own screenshot assertion; the historical
    `.build/s2*` gates encode those expectations.
13. **WM chrome** (slow) - frame resize/zoom/toolbar (`.build/s43_assert.py`),
    menus and dropdown picks (S4.2b), focus swap, drags.
14. **rlimits** - `RLIMIT_AS`/`RLIMIT_DATA` are unenforced
    (`docs/reference/security-audit-record.md` §8); a case should assert which
    limits bite so the gap is visible rather than assumed.
15. **Timers / scheduler** - no case asserts timekeeping, `nanosleep` or `yield`.
16. **Hardening invariants** - NX, SMEP, SMAP, ASLR and W^X are all *absent*
    today; assert the absence so an implementation has something to turn green.
17. **SMP** - no boot has ever used more than one CPU (`docs/design/smp-plan.md`).
    Not a coverage gap until SMP is a feature.

## Deliberately not covered

* **Machine-specific behaviour** - a real GPU, NIC or physical disk; the harness
  boots QEMU by construction.
* **Wall-clock performance** - the gate corpus has profiling runs
  (`ARGENTUM_DRAW_MS`, frame timing), but a timing assertion is a flake generator.
* **A full X client suite** - the X server is exercised through the toolkit and
  the WM, not by porting `x11perf`/`xterm`.
