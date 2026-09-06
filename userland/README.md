# userland — first-party userland sources

Sources compiled into the FNX guest image (via the top Makefile's
`userland64` target and friends), plus the vendored X server tree.

- `tools/` — programs staged as `/System/Tools/*` (init, config, acl,
  fbdump, tone, pty_test, xbfsquery, xbfsqtest, ...).
- `tests/` — proof + test programs (hello*, cpp_smoke, test_mmap, the
  xbfs* test programs, test_toybox.sh), staged under
  `/System/Shared/tests` or run by harnesses.
- `demos/` — X11 demo clients (xdraw, xkey), staged by `make xfbdesk`.
- `scripts/` — runtime scripts staged into the image (dhcp_script.sh →
  /System/Shared/scripts/dhcp/default.script).
- `libconfig.c` — source of the first-party libconfig shared library
  (.build/fnxlib/libconfig.so.1). Its public header is `userland/libconfig.h`.
- `configuration/` — the machine-configuration `.conf` samples staged into
  /System/Configuration at image build.
- `xfb/` — the vendored X server (Xfb) sources + its own build.
