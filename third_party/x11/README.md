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

Updated during M0 bring-up (the real dependency graph): the server core
xkb layer includes libxkbfile's XKM headers and calls XkmReadFile, so
libxkbfile (and therefore libX11 + libxcb + xcb-proto) IS required —
xcb has been a hard requirement of libX11 since 1.6/1.7, so no
no-xcb libX11 route exists. Added submodules: xcbproto-1.17.0,
libxcb-1.17.0, libX11-1.8.13. libsha1/ is a FNX-vendored minimal SHA-1
(sha1_begin/hash/end API the server's os/xsha1.c 'libsha1' backend
expects; verified against FIPS 180-1 vectors) — the server needs SHA-1
and musl provides no libc/libmd/OpenSSL sha1.
