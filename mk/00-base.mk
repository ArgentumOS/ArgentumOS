# fnx/Makefile
#
# Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
# Distributed under the terms of the Fiwix License.
#

TOPDIR := $(shell if [ "$$PWD" != "" ] ; then echo $$PWD ; else pwd ; fi)
INCLUDE = $(TOPDIR)/include
TMPFILE := $(shell mktemp)

LANG = -std=c89

# The 32-bit i386 build was REMOVED in the FNX pivot: this tree builds
# only the 64-bit long-mode kernel (PE32+ UEFI application via buildfnx).

LD = $(CROSS_COMPILE)ld

export LD INCLUDE

# Default target: the 64-bit UEFI kernel.
all: buildfnx
	@echo "make: .build/64/fnx.efi ready (FNX, 64-bit only)"

clean:
	rm -rf .build/64 .build/64real
	rm -f *.o fnx System.map.gz

# ---------------------------------------------------------------------------
# Development harness (QEMU / UEFI). See docs/port-longmode-uefi.txt.
#
#   make run        boot the 32-bit kernel via QEMU's built-in Multiboot
#                   loader (no GRUB needed). Without a root filesystem the
#                   kernel stops at the expected 'root device not defined'
#                   panic - that is the smoke test.
#   make ovmf       fetch OVMF firmware without root (into .build/ovmf).
#   make run-uefi   boot OVMF (UEFI) firmware with the FNX EFI stub
#                   (PE32+ kernel from make build64) on the XBFS root image
#                   (.build/rootxbfs.img) — XBFS is the default root device.
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

# One system compiler: the kernel builds with clang since M3
# (docs/llvm-clang-toolchain-plan.md M3); the pre-clang CC64R flag set is
# kept verbatim. PATCH_PIC stays on: -fPIC extern-data access emits
# R_X86_64_REX_GOTPCRELX under clang exactly as under gcc-14, and the PE
# link cannot relax it, so the mov->lea rewrite is required for both
# (see the REAL pattern rule for the measured #GP without it).
CLANG19 = /usr/lib/llvm-19/bin/clang
LLVM_OBJCOPY = /usr/lib/llvm-19/bin/llvm-objcopy

# A USB mouse is attached by default so the X desktop (Xfb) has a
# working pointer: QEMU 10's PS/2 mouse delivery is unreliable headless,
# and FNX's usb-mouse driver synthesizes PS/2 packets into /dev/psaux
# for the X server. Set QEMU_USB= to disable.
QEMU_USB ?= -device usb-ehci -device usb-mouse
# ---------------------------------------------------------------------------

CC64 = $(CLANG19) -m64 -march=x86-64 $(LANG) -D__KERNEL__ $(CONFFLAGS) -I$(INCLUDE) -O2 \
       -fno-pie -fno-common -ffreestanding -mno-red-zone -mno-sse -mno-sse2 \
       -fno-asynchronous-unwind-tables -Wall -Wstrict-prototypes

run: .build/ovmf/OVMF.fd rootxbfs build64
	$(MAKE) run-qemu

run-uefi: run

# Legacy ext2 root (mkext2.py, rev-0, 1KB blocks): the pre-XBFS default,
# kept as an optional boot path (M4f made XBFS the root fs).
run-ext2: .build/ovmf/OVMF.fd rootdisk64 build64
	$(MAKE) run-qemu ROOTIMG=.build/root.img

# --- Interactive Xfb desktop: boots Xfb :0 on the framebuffer (the QEMU
# --- window) with demo windows + a console shell, from the real FSH root.
# --- The image is the standard FSH rootfs plus a session.conf asking init
# --- for the X11 desktop (init.c session_is_xfb -> start_xfb). Run from a
# --- terminal with DISPLAY set so the GOP fb is shown in a GTK window:
# ---     make run-xfb
XFBROOT ?= .build/xfbdesk-root
XFBIMG  ?= .build/rootxbfs-xfbdesk.img
XFB_DEMO_BIN = .build/x11/xdraw .build/x11/xkey

.build/x11/xdraw: userland/demos/xdraw.c
	$(MUSL64_CC) -I .build/x11-prefix/include -I .build/x11-prefix/include/X11 \
		userland/demos/xdraw.c -L .build/x11-prefix/lib -lX11 -lxcb -lXdmcp -lXau -o $@
.build/x11/xkey: userland/demos/xkey.c
	$(MUSL64_CC) -I .build/x11-prefix/include -I .build/x11-prefix/include/X11 \
		userland/demos/xkey.c -L .build/x11-prefix/lib -lX11 -lxcb -lXdmcp -lXau -o $@

xfbdesk-root: $(XFB_DEMO_BIN) $(XFB_BIN)
	rm -rf $(XFBROOT)
	cp -a $(ROOTFS64) $(XFBROOT)
	cp $(XFB_BIN) "$(XFBROOT)/System/Shared/X11/bin/Xfb"
	cp .build/x11/xdraw "$(XFBROOT)/System/Shared/X11/bin/xdraw"
	cp .build/x11/xkey "$(XFBROOT)/System/Shared/X11/bin/xkey"
	printf 'desktop = "xfb"\n' > "$(XFBROOT)/System/Configuration/session.conf"
xfbdesk: xfbdesk-root
	python3 tools/mkxbfs.py $(XFBROOT) $(XFBIMG) 64
	python3 tools/xbfscheck.py $(XFBIMG) $(XFBROOT)
run-xfb: .build/ovmf/OVMF.fd xfbdesk build64
	$(MAKE) run-qemu ROOTIMG=$(XFBIMG)

# Boot the XBFS root image (.build/rootxbfs.img) as /dev/sda. The kernel's
# cmdline carries no rootfstype=, so mount_root() probes the disk
# filesystems (minix -> ext2 -> iso9660 -> xbfs) and finds xbfs; the same
# kernel boots both the ext2 and the XBFS root.
ROOTIMG ?= .build/rootxbfs.img
run-xbfs: run

run-qemu:
	@./tools/mkesp.sh
	@if [ -n "$${DISPLAY}$${WAYLAND_DISPLAY}" ] && [ -t 1 ]; then \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -display gtk -serial stdio -m 128M $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_EXTRA); \
	elif [ -t 1 ]; then \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -display curses -m 128M $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_EXTRA); \
	else \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -nographic -m 128M $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_EXTRA); \
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
# First-party shared libraries (docs/shared-libraries-plan.md): FNX code
# built as .so's into .build/fnxlib and staged into /System/Libraries.
FNXLIB        = .build/fnxlib
FNXLIB_CONFIG = $(FNXLIB)/libconfig.so.1

$(FNXLIB_CONFIG): userland/libconfig.c userland/libconfig.h
	@mkdir -p $(FNXLIB)
	$(MUSL64_CC) -fPIC -shared -Iinclude -Iuserland -Wl,-soname,libconfig.so.1 \
		-o $@ userland/libconfig.c
	ln -sf libconfig.so.1 $(FNXLIB)/libconfig.so
# C++: LLVM libc++/libc++abi/libunwind via tools/musl-clang++64.sh
# (docs/cpp-toolchain-plan.md; runtimes built by the llvm-cxx target).
MUSL64_CXX    = $(CURDIR)/tools/musl-clang++64.sh
LLVM_CXX_SRC    = .build/llvm-src
LLVM_CXX_CFG    = .build/llvm-cxx/Makefile
LLVM_CXX_PREFIX = .build/llvm-cxx-prefix
LLVM_CXX_STAMP  = .build/llvm-cxx/.installed
ROOTFS64      = .build/rootfs64
DASH64_BIN    = third_party/dash/src/dash64
TOYBOX64_BIN  = third_party/toybox/toybox64

.PHONY: userland64 musl64 dash64 toybox64 llvm-cxx compiler-rt m0clang fshlint toolchain-gate

# FSH porting linter gate (proposal 6.1/Q1): zero-allow on System/Tools.
fshlint:
	python3 tools/fshlint.py $(ROOTFS64)

# LLVM M4 gate (docs/llvm-clang-toolchain-plan.md M4): the one system
# compiler is clang - no other compiler name may appear in the build
# definition. Wired into buildfnx and userland64 below; run it standalone
# as `make toolchain-gate`.
toolchain-gate:
	@python3 tools/toolchain-gate.py
