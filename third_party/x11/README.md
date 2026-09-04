# third_party/x11 — X server + dependency sources for the FNX X11 port

See docs/x11-xvfb-fb-plan.md (M0: vendor + build the modern Xvfb core).

| component | version | source |
|---|---|---|
| xserver    | xorg-server-21.1.24 | git submodule (gitlab.freedesktop.org/xorg/xserver, branch server-21.1-branch) |
| pixman     | pixman-0.46.4        | git submodule |
| xorgproto  | xorgproto-2025.1     | git submodule |
| libxkbfile | libxkbfile-1.2.0     | git submodule |
| libXfont2  | 2.0.7                | x.org release tarball (extracted tree; repo left freedesktop gitlab, no anonymous clone) |
| libfontenc | 1.1.8                | x.org release tarball (extracted tree) |
| xtrans     | 1.5.2                | x.org release tarball (extracted tree; repo left freedesktop gitlab) |

Builds target the musl64 static toolchain into .build/x11-prefix (see
Makefile x11 targets). The extracted tarballs are committed trees;
versions pinned here.
