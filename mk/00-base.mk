# fnx/Makefile
#
# Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
# Distributed under the terms of the Fiwix License.
#

TOPDIR := $(shell if [ "$$PWD" != "" ] ; then echo $$PWD ; else pwd ; fi)
INCLUDE = $(TOPDIR)/include
TMPFILE := $(shell mktemp)

# A COMPILER FLAG WITH THE ENVIRONMENT'S NAME, AND THE COLLISION WAS REAL (found 2026-09-20, W11b).
# `LANG` is the LOCALE variable everywhere else in the world, but here it is `-std=c89` — and because a
# variable that came from the ENVIRONMENT is re-exported by GNU make with whatever value the makefile
# gives it, EVERY CHILD PROCESS OF THIS BUILD has been running with `LANG="-std=c89"`: an invalid locale.
# A locale-sensitive probe read root-locale data under `make` and English from a shell, which is how this
# was found. The flag keeps its name for the recipes that use it; the ENVIRONMENT copy is withdrawn here.
LANG = -std=c89
unexport LANG

# The 32-bit i386 build was REMOVED in the FNX pivot: this tree builds
# only the 64-bit long-mode kernel (PE32+ UEFI application via buildfnx).

LD = $(CROSS_COMPILE)ld

export LD INCLUDE

# ---------------------------------------------------------------------------
# Development harness (QEMU / UEFI). See docs/port-longmode-uefi.txt.
#
#   make run        boot the 32-bit kernel via QEMU's built-in Multiboot
#                   loader (no GRUB needed). Without a root filesystem the
#                   kernel stops at the expected 'root device not defined'
#                   panic - that is the smoke test.
#   make ovmf       fetch OVMF firmware without root (into .build/ovmf).
#   make run-uefi   boot OVMF (UEFI) firmware with the FNX EFI stub
#                   (PE32+ kernel from make build64) on the AGFS root image
#                   (.build/rootagfs.img) — AGFS is the default root device.
#   make run-ext2    same, but booting the legacy ext2 root (.build/root.img).
#   make compile64  compile every C source with 64-bit flags into .build/64
#                   (no link) - the type-sweep verifier for the long-mode
#                   port.
#
# Display selection: with DISPLAY or WAYLAND_DISPLAY set and stdout on a
# terminal, run/run-uefi open a GTK window with the serial console on stdio
# (so the kernel's COM1 output appears in the same terminal). Without a
# display they fall back to curses; when stdout is piped (headless/CI) they
# use -nographic.
#
# A legacy virtio-net NIC is attached by default (see QEMU_NET below), so
# `ping 10.0.2.2` works inside the guest after the init-time DHCP handshake.
#
# Overrides: QEMU_EXTRA (extra qemu args), FNX_QEMU_TOOLS / QEMU_TOOLS (tools prefix),
#            QEMU_NET (NIC args; set QEMU_NET= to boot without the NIC).
# The on-disk tool prefix may still be the legacy fiwix-qemu-tools name.
QEMU_TOOLS ?= $(shell if [ -d "$(HOME)/.local/share/fnx-qemu-tools" ]; then echo "$(HOME)/.local/share/fnx-qemu-tools"; else echo "$(HOME)/.local/share/fiwix-qemu-tools"; fi)
# Attach the legacy virtio-net NIC by default (real-NIC support, target #6):
# the init-time DHCP handshake leases 10.0.2.15 from SLIRP, so
# `ping 10.0.2.2` works out of the box. hostfwd=6000 lets a host X client
# reach the guest's Xfb :0 at 127.0.0.1:6000 (M1 TCP bring-up).
# Set QEMU_NET= to boot without it.
QEMU_NET ?= -device virtio-net-pci,disable-modern=on,netdev=n1 -netdev user,id=n1,hostfwd=tcp:127.0.0.1:6000-:6000
# The root disk must sit on an AHCI controller: the kernel registers block
# major 8 (/dev/sda) only for AHCI, and root=/dev/sda is baked into the
# kernel cmdline. The ESP stays on the PIIX IDE (index 0) so OVMF can boot
# it. Set QEMU_DRIVES= to override.
QEMU_DRIVES ?= -drive file=.build/esp.img,format=raw,if=ide,index=0 -drive file=$(ROOTIMG),format=raw,if=none,id=disk -device ich9-ahci,id=ahci -device ide-hd,drive=disk,bus=ahci.0

# Audio: attach a sound card so the OSS /dev/dsp stack has hardware to drive.
# The default is audible: tools/qemu.sh prefers the system QEMU (apt), whose
# module directory matches its binary, so the host backends load - pipewire
# here, through /run/user/$UID/pipewire-0.  The tools QEMU in
# ~/.local/share/fiwix-qemu-tools cannot load any host backend (its modules
# are a different build), so a sound card attached to it is silent; that
# prefix is only the fallback when no system QEMU exists.
# Override per run:
#   make run-uefi QEMU_AUDIODEV=pa     (or alsa/oss/sdl, likewise)
#   make run-uefi QEMU_AUDIODEV=none   discard playback
#   make run-uefi QEMU_AUDIODEV=wav,path=.build/qemu-audio.wav   capture to a file
QEMU_AUDIODEV ?= pipewire
QEMU_AUDIO ?= -device intel-hda -device hda-output,audiodev=snd -audiodev $(QEMU_AUDIODEV),id=snd

# One system compiler: the kernel builds with clang since M3
# (docs/llvm-clang-toolchain-plan.md M3); the pre-clang CC64R flag set is
# kept verbatim. PATCH_PIC stays on: -fPIC extern-data access emits
# R_X86_64_REX_GOTPCRELX under clang exactly as under gcc-14, and the PE
# link cannot relax it, so the mov->lea rewrite is required for both
# (see the REAL pattern rule for the measured #GP without it).
CLANG19 = /usr/lib/llvm-19/bin/clang
LLVM_OBJCOPY = /usr/lib/llvm-19/bin/llvm-objcopy

# Input is real USB HID (docs/design/native-input-plan.md): a USB
# keyboard + mouse on an xHCI controller, with the PS/2 controller
# absent ('i8042=off') so the host pointer routes to the USB device
# instead of the emulated PS/2 port. The drivers decode their own HID
# reports into the native input devices (/System/Devices/mouse,
# /System/Devices/kbd) - nothing synthesizes PS/2 any more. xHCI (not
# EHCI) is required to hold two full-speed devices at once. Set
# QEMU_USB= and QEMU_MACHINE= to disable.
QEMU_USB ?= -device qemu-xhci -device usb-kbd -device usb-mouse
QEMU_MACHINE ?= -machine pc,i8042=off
# Guest RAM. 128M is the ceiling the KERNEL currently boots at: 192M and
# 256M both hang right after `[M4-B] calling the real FNX kernel
# start_kernel()` (measured; the console stops there, in the paging /
# allocator setup). So this is a knob, not a workaround - raising it needs
# kernel work first.
#
# 128M is also why the display mode ships as the firmware's 1280x800 rather
# than 1920x1080: the frame buffers are a function of the screen (Xfb's
# shadow and the scanout map are ~8.3MB each at 1080p, and every window adds
# a backing the toolkit mirrors into an MIT-SHM segment), and a 1080p
# desktop with a real client on it exhausts 128M. That failure shows up as
# `shm_map_page(): map_page() returned 0!` plus a page fault - it looks like
# the S4.3 shm-churn crash but is genuine exhaustion.
# Guest RAM. The kernel reserves a low window for its boot structures (page
# pool, page tables, per-page structs, process and fd tables - ~19MB at 128M,
# ~26MB at 512M), so the 128M guest is too tight for a real client. 256M is
# the shipped default and carries a 1920x1080 desktop with an application on
# it (the kernel handles 512M/1G too; 2G hits the direct-map item).
QEMU_MEM ?= 256M
# ---------------------------------------------------------------------------

CC64 = $(CLANG19) -m64 -march=x86-64 $(LANG) -D__KERNEL__ $(CONFFLAGS) -I$(INCLUDE) -O2 \
       -fno-pie -fno-common -ffreestanding -mno-red-zone -mno-sse -mno-sse2 \
       -fno-asynchronous-unwind-tables -Wall -Wstrict-prototypes

run: .build/ovmf/OVMF.fd rootagfs build64
	$(MAKE) run-qemu

run-uefi: run

# Legacy ext2 root (mkext2.py, rev-0, 1KB blocks): the pre-AGFS default,
# kept as an optional boot path (M4f made AGFS the root fs).
run-ext2: .build/ovmf/OVMF.fd rootdisk64 build64
	$(MAKE) run-qemu ROOTIMG=.build/root.img

# --- Interactive Xfb desktop: boots Xfb :0 on the framebuffer (the QEMU
# --- window) with demo windows + a console shell, from the real FSH root.
# --- The image is the standard FSH rootfs plus a session.conf steering
# --- init (init.c read_session -> start_xfb): `desktop = "xfb"` runs the
# --- xdraw+xkey demo desktop. (The `uitest` variant image, which ran the
# --- removed theme_chrome board, is gone.) Run from a terminal with DISPLAY
# --- set so the GOP fb is shown in a GTK window:
# ---     make run-xfb      (demo desktop)
XFBROOT ?= .build/xfbdesk-root
XFBIMG  ?= .build/rootagfs-xfbdesk.img
XFB_DEMO_BIN = .build/x11/xdraw .build/x11/xkey

.build/x11/xdraw: userland/demos/xdraw.c
	$(MUSL64_CC) -I .build/x11-prefix/include -I .build/x11-prefix/include/X11 \
		userland/demos/xdraw.c -L .build/x11-prefix/lib -lX11 -lxcb -lXdmcp -lXau -o $@
.build/x11/xkey: userland/demos/xkey.c
	$(MUSL64_CC) -I .build/x11-prefix/include -I .build/x11-prefix/include/X11 \
		userland/demos/xkey.c -L .build/x11-prefix/lib -lX11 -lxcb -lXdmcp -lXau -o $@

xfbdesk: xfbdesk-root
	python3 tools/mkagfs.py $(XFBROOT) $(XFBIMG) 64
	python3 tools/agfscheck.py $(XFBIMG) $(XFBROOT)
run-xfb: .build/ovmf/OVMF.fd xfbdesk build64
	$(MAKE) run-qemu ROOTIMG=$(XFBIMG)


ZOOIMG  ?= .build/rootagfs-zoo.img



# Boot the AGFS root image (.build/rootagfs.img) as /dev/sda. The kernel's
# cmdline carries no rootfstype=, so mount_root() probes the disk
# filesystems (minix -> ext2 -> iso9660 -> agfs) and finds agfs; the same
# kernel boots both the ext2 and the AGFS root.
ROOTIMG ?= .build/rootagfs.img
run-agfs: run

# Stop lingering QEMU guests. Every guest opens .build/esp.img, so one left
# behind (a guest can reach "Safe to Power Off" and stay alive) makes the next
# `make run-*` / `make zoo` fail with "Failed to get write lock". Safe when no
# guest is meant to be up.
qemu-kill:
	@n=$$(pgrep -f '[q]emu-system' | wc -l); \
	if [ "$$n" = "0" ]; then \
		echo "qemu-kill: no guests running"; \
	else \
		pkill -9 -f '[q]emu-system'; sleep 1; \
		echo "qemu-kill: stopped $$n guest(s)"; \
	fi

run-qemu:
	@./tools/mkesp.sh
	@if pgrep -f '[q]emu-system' >/dev/null 2>&1; then \
		echo "run-qemu: a QEMU guest is already running, and every guest opens"; \
		echo "  .build/esp.img, so this run would fail with \"Failed to get write"; \
		echo "  lock\". A guest that reached 'Safe to Power Off' can linger and keep"; \
		echo "  holding it. Finish/close it, or clean up with: make qemu-kill"; \
		exit 1; \
	fi
	@if [ -n "$${DISPLAY}$${WAYLAND_DISPLAY}" ] && [ -t 1 ]; then \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh $(QEMU_MACHINE) -display gtk -serial stdio -m $(QEMU_MEM) $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_AUDIO) $(QEMU_EXTRA); \
	elif [ -t 1 ]; then \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh $(QEMU_MACHINE) -display curses -m $(QEMU_MEM) $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_AUDIO) $(QEMU_EXTRA); \
	else \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh $(QEMU_MACHINE) -nographic -m $(QEMU_MEM) $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_AUDIO) $(QEMU_EXTRA); \
	fi

# ---------------------------------------------------------------------------
# Native x86_64 userland (LLVM M0-M2, docs/llvm-clang-toolchain-plan.md):
# DYNAMIC (non-PIE) ELF64 against /System/Libraries/ld-musl-x86_64.so.1
# (docs/shared-libraries-plan.md); MUSL64_CC_STATIC is the explicit static
# exception (recovery shell/updater + unconverted third-party carve-outs).
# The one system compiler is clang since M1 (kernel M3): every binary in
# the system build goes through the tools/musl-clang64*.sh wrappers, and
# musl itself is built by clang since M2. MUSL64_LIBC is the "musl is
# installed" stamp the userland targets order against.
MUSL64_PREFIX = .build/musl64
MUSL64_LIBC   = $(MUSL64_PREFIX)/lib/libc.so
# musl's configure CC is the host clang and config.mak's LIBCC points at
# the compiler-rt builtins archive in place of libgcc.a (clang has no
# libgcc; musl's configure would otherwise detect the host's libgcc).
# musl's build is self-contained (own headers, freestanding), so no
# sysroot or -isystem is needed.
MUSL64_BUILD_CC = /usr/lib/llvm-19/bin/clang
MUSL64_CC     = $(CURDIR)/tools/musl-clang64.sh
MUSL64_CC_STATIC = $(CURDIR)/tools/musl-clang64-static.sh
COMPILER_RT_CFG     = .build/compiler-rt/CMakeCache.txt
COMPILER_RT_BUILTINS = .build/compiler-rt/lib/linux/libclang_rt.builtins-x86_64.a
# M0-era names still used by the m0clang target (same wrappers).
MUSL64_CLANG     = $(CURDIR)/tools/musl-clang64.sh
MUSL64_CLANG_STATIC = $(CURDIR)/tools/musl-clang64-static.sh
# X11 third-party dependency prefix (built by tools/x11-shared-build.sh;
# shared .so + static .a coexist since the M2 conversion).
X11PREFIX     = .build/x11-prefix
# lcms2 third-party prefix (built by tools/lcms2-build.sh, which builds it TWICE: this guest
# copy, and a host copy that the host CoreGraphics library and its probes link).
LCMS2_PREFIX  = .build/lcms2-prefix
# libcurl third-party prefix (built by tools/curl-build.sh, GUEST ONLY - the Foundation's HTTP
# transport: docs/design/foundation-transport-plan.md, W7 slice 2b). One build rather than lcms2's
# two because libcurl has no host consumer; see the script's header for why.
CURL_PREFIX   = .build/curl-prefix
# LibreSSL third-party prefix (fetched by tools/fetch-libressl.sh, built by tools/libressl-build.sh,
# GUEST ONLY): the ONE system SSL library - docs/design/libressl-plan.md, pin 4.3.2. L0 built it; L1
# stages it; L2 binds libcurl to it for https.
LIBRESSL_PREFIX = .build/libressl-prefix
# ICU4C third-party prefix (built by tools/icu-build.sh) - the DATA backend for
# the Foundation's data-driven families (docs/design/foundation-plan.md §10,
# slice F13). Same shape as X11PREFIX: a prefix on the build side, staged into
# /System/Libraries on the guest side.
ICUPREFIX     = .build/icu-prefix
# First-party shared libraries (docs/shared-libraries-plan.md): FNX code
# built as .so's into .build/fnxlib and staged into /System/Libraries.
FNXLIB        = .build/fnxlib
FNXLIB_CONFIG = $(FNXLIB)/libconfig.so.1

$(FNXLIB_CONFIG): userland/libconfig.c userland/libconfig_plist.c \
	userland/libconfig_internal.h userland/plist.c userland/libconfig.h
	@mkdir -p $(FNXLIB)
	# libconfig is two translation units over the shared plist CORE (P3b:
	# docs/design/plist-config-plan.md), and plist.c is compiled in rather than
	# linked as a third library: one source, no forked implementation.
	$(MUSL64_CC) -fPIC -shared -Iinclude -Iuserland -Wl,-soname,libconfig.so.1 \
		-o $@ userland/libconfig.c userland/libconfig_plist.c \
		userland/plist.c
	ln -sf libconfig.so.1 $(FNXLIB)/libconfig.so

# (The Argentum UIKit toolkit — libargentum.so.1, the ARGENTUM_SRCS list and its
# shared-library rule — was removed 2026-09-17 when the toolkit was parked:
# docs/design/argentum-uikit-plan.md (DEFERRED), branch park/argentum-uikit.)

# C++: LLVM libc++/libc++abi/libunwind via tools/musl-clang++64.sh
# (docs/cpp-toolchain-plan.md; runtimes built by the llvm-cxx target).
MUSL64_CXX    = $(CURDIR)/tools/musl-clang++64.sh
# Objective-C (docs/design/objc-toolchain-plan.md P2): the C++ wrapper plus the
# gnustep-2.0 ABI, blocks, the runtime's headers and -lobjc.
MUSL64_OBJC   = $(CURDIR)/tools/musl-clang-objc64.sh
# The Foundation (docs/design/foundation-plan.md): F0's root class, built as a
# shared library beside libconfig. The MRR and ARC files are compiled with their
# OWN flags (ARC is a per-file choice, never a wrapper default) and -fPIC, because
# a shared object cannot take the ABI's PC-relative ivar-offset relocations.
FOUNDATION_SRC = userland/Foundation
FOUNDATION_LIB = $(FNXLIB)/libfoundation.so.1
# CoreGraphics (docs/design/coregraphics-plan.md, milestones C1 and C2): the geometry,
# the affine arithmetic, and — from C2 — the context whose fills are rasterized by the
# VENDORED PIXMAN. That is now this library's only dependency: the include path and the
# link come from the X11 prefix the rest of the userland already builds against, and
# mk/20-userland.mk stages libpixman-1.so into the image. The engine is not a new one —
# userland/xfb/fb/fbtrap.c feeds pixman the same trapezoids.
#
# A GENERATED SOURCE LIST, like the Foundation's block in mk/20-userland.mk, and for
# the reason that block exists: this library gains a translation unit per header, and
# hand-listed sources are how these files were damaged once. THE OBJECT PREFIX IS
# `coregraphics-` AND IS NOBODY ELSE'S: a probe and a library source that share a
# basename must not share an object path, which is a collision the Foundation's
# history records (userland/tests/foundation_nsvalue.m once overwrote
# userland/foundation/nsvalue.m's object).
CG_SRC  = userland/CoreGraphics
CG_LIB  = $(FNXLIB)/libcoregraphics.so.1
CG_SRCS = $(notdir $(wildcard $(CG_SRC)/*.c))
# THE OBJECTIVE-C HALF: one file, and it exists because the system-defined spaces are NAMED and a
# name is an NSString. Everything else in this library is C, and the C half does not know this
# file exists beyond the two functions it calls through CGColorSpace_internal.h.
CG_MSRCS = $(notdir $(wildcard $(CG_SRC)/*.m))
CG_OBJS = $(addprefix $(FNXLIB)/coregraphics-,$(CG_SRCS:.c=.o) $(CG_MSRCS:.m=.o))
# THREE DIRECTORIES NOW. PIXMAN'S HEADER IS NOT SELF-CONTAINED — `pixman.h` includes
# `<pixman-version.h>`, which sits beside it rather than on a bare include path — and the
# colour engine arrives with the ICC half of C4: lcms2's header is in a prefix of its own,
# built by tools/lcms2-build.sh, and listed FIRST because the version is newer than anything a
# system include path might offer.
CG_CFLAGS = -I$(LCMS2_PREFIX)/include -I$(LIBJPEG_PREFIX)/include -I$(X11PREFIX)/include -I$(X11PREFIX)/include/pixman-1
# NO RPATH FOR THE GUEST, deliberately: the loader resolves `liblcms2.so.2` out of
# /System/Libraries, where mk/20-userland.mk stages it (musl's syslibdir is that directory), so
# a build-tree path in the binary would be wrong on the guest rather than merely unnecessary.
# AND libfoundation, BECAUSE OF ONE FILE: the system-defined colour spaces are NAMED, comparing a
# name is a message send, and that file is Objective-C. `libfoundation.so.1` is staged beside this
# library in /System/Libraries, and musl's syslibdir IS that directory, so the guest loader finds
# it with no rpath — the same rule the lcms2 line above follows.
# A PNG DECODER NEEDS LIBPNG, AND THE TREE ALREADY HAS IT: `third_party/x11/libpng` is vendored and
# built into the SAME prefix as pixman — FreeType reads sbix colour glyphs with it — and
# `libpng16.so.16` is already staged into the guest for the X stack. So `CGImageCreateWithPNGDataProvider`
# added a LINK FLAG and no new dependency, which is a measured finding rather than an assumption: the
# library and its header were found in $(X11PREFIX) before the seam was written.
# LIBJPEG IS A DIFFERENT CASE FROM LIBPNG ABOVE, and the difference is why both lines carry their own
# comment: libpng rode the X stack, while libjpeg-turbo is VENDORED FOR THIS LIBRARY and built by
# tools/libjpeg-build.sh into a prefix of its own — so it needs an include path (on the line above) and
# this link line, and mk/20-userland.mk stages its SONAME into /System/Libraries for the guest.
LIBJPEG_PREFIX = .build/libjpeg-prefix
CG_LDFLAGS = -L$(X11PREFIX)/lib -lpixman-1 -lpng16 -L$(LCMS2_PREFIX)/lib -llcms2 -L$(LIBJPEG_PREFIX)/lib -ljpeg -L$(FNXLIB) -lfoundation

define CG_rule
$(FNXLIB)/coregraphics-$(1:.c=.o): $(CG_SRC)/$(1)
	@mkdir -p $(FNXLIB)
	$$(MUSL64_CC) -fPIC -Iinclude -Iuserland $$(CG_CFLAGS) -c $$< -o $$@
endef
$(foreach f,$(CG_SRCS),$(eval $(call CG_rule,$(f))))

# THE OBJECTIVE-C RULE, WHICH DIFFERS FROM THE C ONE IN EXACTLY TWO WAYS: the compiler is the
# tree's Objective-C wrapper (00-base already defines it, because Foundation uses it for its whole
# library), and the ICU include path is on it because <Foundation/Foundation.h> leads there.
# NO -fobjc-arc, matching the library's ARC policy (mk/20-userland.mk states it): the library is
# manual, the probes are ARC, and this file owns nothing under either.
define CG_objc_rule
$(FNXLIB)/coregraphics-$(1:.m=.o): $(CG_SRC)/$(1)
	@mkdir -p $(FNXLIB)
	$$(MUSL64_OBJC) -fPIC -Iinclude -Iuserland -I$$(ICUPREFIX)/include $$(CG_CFLAGS) -c $$< -o $$@
endef
$(foreach f,$(CG_MSRCS),$(eval $(call CG_objc_rule,$(f))))

# No -lm: musl folds the math functions into libc, and a shared object is linked
# with unresolved symbols allowed anyway. The HOST probe needs -lm and the host's
# own pixman, and those are on mk/60-host.mk's line rather than here.
$(CG_LIB): $(CG_OBJS)
	@mkdir -p $(FNXLIB)
	$(MUSL64_CC) -fPIC -shared -Wl,-soname,libcoregraphics.so.1 $(CG_OBJS) $(CG_LDFLAGS) -o $@
	ln -sf libcoregraphics.so.1 $(FNXLIB)/libcoregraphics.so

# The AppKit (docs/design/coregraphics-plan.md, milestone C8): the bridge between this tree's
# CoreGraphics and the Objective-C AppKit that cocoa-parity-plan.md builds on top. ONE CLASS SO FAR
# — NSGraphicsContext, the seam — and it is its own shared object rather than a file inside
# libfoundation because Apple's AppKit is its own framework and coregraphics-plan §3 row 1 draws the
# boundary there.
#
# A GENERATED SOURCE LIST AND A UNIQUE OBJECT PREFIX, copying CoreGraphics' block above rather than
# Foundation's hand-listed one: that comment records why (a library that gains a translation unit per
# header, and a probe and a library source that must not share an object path). NOTHING IS
# HAND-LISTED HERE, so adding NSColor.m or NSImage.m later needs no edit in this file.
APPKIT_SRC   = userland/AppKit
APPKIT_LIB   = $(FNXLIB)/libappkit.so.1
APPKIT_MSRCS = $(notdir $(wildcard $(APPKIT_SRC)/*.m))
APPKIT_OBJS  = $(addprefix $(FNXLIB)/appkit-,$(APPKIT_MSRCS:.m=.o))
# THE LIBRARY IS PURE OBJECTIVE-C, so it has no CFLAGS line of its own: `<AppKit/…>` and
# `<CoreGraphics/…>` both resolve through `-Iuserland`, and Foundation's headers through the ICU
# include path the rule already passes. `-lfoundation` is what pulls the runtime in transitively —
# the same arrangement the CoreGraphics objective-C file relies on, and the reason no `-lobjc`
# appears on the GUEST link line (the host link needs it explicitly; see mk/60-host.mk).
APPKIT_LDFLAGS = -L$(FNXLIB) -lcoregraphics -lfoundation

define APPKIT_objc_rule
$(FNXLIB)/appkit-$(1:.m=.o): $(APPKIT_SRC)/$(1)
	@mkdir -p $(FNXLIB)
	$$(MUSL64_OBJC) -fPIC -Iinclude -Iuserland -I$$(ICUPREFIX)/include -c $$< -o $$@
endef
$(foreach f,$(APPKIT_MSRCS),$(eval $(call APPKIT_objc_rule,$(f))))

$(APPKIT_LIB): $(APPKIT_OBJS)
	@mkdir -p $(FNXLIB)
	$(MUSL64_OBJC) -fPIC -shared -Wl,-soname,libappkit.so.1 $(APPKIT_OBJS) $(APPKIT_LDFLAGS) -o $@
	ln -sf libappkit.so.1 $(FNXLIB)/libappkit.so
LLVM_CXX_SRC    = .build/llvm-src
LLVM_CXX_CFG    = .build/llvm-cxx/Makefile
LLVM_CXX_PREFIX = .build/llvm-cxx-prefix
LLVM_CXX_STAMP  = .build/llvm-cxx/.installed
ROOTFS64      = .build/rootfs64
DASH64_BIN    = third_party/dash/src/dash64
TOYBOX64_BIN  = third_party/toybox/toybox64

.PHONY: userland64 musl64 dash64 toybox64 llvm-cxx compiler-rt m0clang fshlint toolchain-gate foundation-gate foundation-sweep

# The clean-room wall's mechanical half (docs/design/foundation-plan.md §2): no
# GNUstep/ObjFW/Apple-Foundation header import by first-party code, and never the
# runtime's legacy <objc/Object.h>.
foundation-gate: foundation-sweep
	@python3 tools/foundation-gate.py

# The ledger's mechanical half (docs/design/foundation-plan.md §11.2, source 2,
# and §11.3.1): the whole documented Foundation surface — every class, protocol,
# macro, enum, function, variable, type alias and struct — held against this
# tree, offline. It fails when a `shipped` row stops being declared, when an
# `open` row STARTS being declared (the stale-absence trap that hid two red
# probes for months), and when a `struck` row — API Apple deprecates, which
# §11.5 makes ours to leave — turns up in our headers. Refresh the surface with
# tools/foundation-sweep.py --refresh (the only networked mode; the data is a
# dated measurement, so record the date where the ledger is discussed).
foundation-sweep:
	@python3 tools/foundation-sweep.py --check

# FSH porting linter gate (proposal 6.1/Q1): zero-allow on System/Tools.
fshlint:
	python3 tools/fshlint.py $(ROOTFS64)

# LLVM M4 gate (docs/llvm-clang-toolchain-plan.md M4): the one system
# compiler is clang - no other compiler name may appear in the build
# definition. Wired into buildfnx and userland64 below; run it standalone
# as `make toolchain-gate`.
toolchain-gate:
	@python3 tools/toolchain-gate.py
