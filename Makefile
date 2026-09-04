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
CCEXE=gcc

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
#                   (PE32+ kernel from make build64) on the BFS root image
#                   (.build/rootbfs.img) — BFS is the default root device.
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

# A USB mouse is attached by default so the GUI desktop has a working
# pointer: QEMU 10's PS/2 mouse delivery is unreliable headless, and
# FNX's usb-mouse driver synthesizes PS/2 packets into /dev/psaux for
# the compositor. Set QEMU_USB= to disable.
QEMU_USB ?= -device usb-ehci -device usb-mouse
# ---------------------------------------------------------------------------

CC64 = gcc -m64 -march=x86-64 $(LANG) -D__KERNEL__ $(CONFFLAGS) -I$(INCLUDE) -O2 \
       -fno-pie -fno-common -ffreestanding -mno-red-zone -mno-sse -mno-sse2 \
       -fno-asynchronous-unwind-tables -Wall -Wstrict-prototypes

run: .build/ovmf/OVMF.fd rootbfs build64
	$(MAKE) run-qemu

run-uefi: run

# Legacy ext2 root (mkext2.py, rev-0, 1KB blocks): the pre-BFS default,
# kept as an optional boot path (M4f made BFS the root fs).
run-ext2: .build/ovmf/OVMF.fd rootdisk64 build64
	$(MAKE) run-qemu ROOTIMG=.build/root.img

# --- Interactive Xfb desktop: boots Xfb :0 on the framebuffer (the QEMU
# --- window) with demo windows + a console shell. Run from a terminal with
# --- DISPLAY set so the GOP fb is shown in a GTK window:
# ---     make run-xfb
XFBROOT ?= .build/xfbdesk-root
XFBIMG  ?= .build/rootbfs-xfbdesk.img
xfbdesk-root:
	rm -rf $(XFBROOT)
	cp -a .build/xfbtest-root $(XFBROOT)
	cp .build/x11/xfb/Xfb $(XFBROOT)/bin/Xfb
	cp tools/xfbdesk-init $(XFBROOT)/sbin/init
	chmod +x $(XFBROOT)/sbin/init
xfbdesk: xfbdesk-root
	python3 tools/mkbfs.py $(XFBROOT) $(XFBIMG) 64
	python3 tools/bfscheck.py $(XFBIMG) $(XFBROOT)
run-xfb: .build/ovmf/OVMF.fd xfbdesk build64
	$(MAKE) run-qemu ROOTIMG=$(XFBIMG)

# Boot the BFS root image (.build/rootbfs.img) as /dev/sda. The kernel's
# cmdline carries no rootfstype=, so mount_root() probes the disk
# filesystems (minix -> ext2 -> iso9660 -> bfs) and finds bfs; the same
# kernel boots both the ext2 and the BFS root.
ROOTIMG ?= .build/rootbfs.img
run-bfs: run

run-qemu:
	@./tools/mkesp.sh
	@if [ -n "$${DISPLAY}$${WAYLAND_DISPLAY}" ] && [ -t 1 ]; then \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -display gtk -serial stdio -m 128M $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_EXTRA); \
	elif [ -t 1 ]; then \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -display curses -m 128M $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_EXTRA); \
	else \
		FNX_QEMU_BIOS=ovmf ./tools/qemu.sh -nographic -m 128M $(QEMU_NET) $(QEMU_DRIVES) $(QEMU_USB) $(QEMU_EXTRA); \
	fi

# --- FNX native x86_64 userland (port phase B): static ELF64 binaries
# --- built with tools/musl-gcc64.sh into .build/rootfs64, packed into an
# --- ext2 root image (.build/root.img) attached as the second IDE disk.

# ---------------------------------------------------------------------------
# Native x86_64 userland (port phase B): same tree, LP64 ABI. Built with
# tools/musl-gcc64.sh into .build/rootfs64. The binaries are static ELF64
# and can be smoke-tested on the host before the kernel can exec them.
MUSL64_PREFIX = .build/musl64
MUSL64_SPECS  = $(MUSL64_PREFIX)/lib/musl-gcc.specs
MUSL64_CC     = gcc -static -specs $(MUSL64_SPECS)
# C++: LLVM libc++/libc++abi/libunwind via tools/musl-g++64.sh
# (docs/cpp-toolchain-plan.md; runtimes built by the llvm-cxx target).
MUSL64_CXX    = $(CURDIR)/tools/musl-g++64.sh
LLVM_CXX_SRC    = .build/llvm-src
LLVM_CXX_CFG    = .build/llvm-cxx/Makefile
LLVM_CXX_PREFIX = .build/llvm-cxx-prefix
LLVM_CXX_STAMP  = .build/llvm-cxx/.installed
ROOTFS64      = .build/rootfs64
DASH64_BIN    = third_party/dash/src/dash64
TOYBOX64_BIN  = third_party/toybox/toybox64

.PHONY: userland64 musl64 dash64 toybox64 llvm-cxx

musl64: $(MUSL64_SPECS)
$(MUSL64_SPECS):
	cd third_party/musl && \
		make clean >/dev/null 2>&1 || true && \
		CC="gcc" ./configure --target=x86_64 --prefix=$(CURDIR)/$(MUSL64_PREFIX) && \
		sed -i 's/^CROSS_COMPILE = .*/CROSS_COMPILE =/' config.mak && \
		$(MAKE) && $(MAKE) install

# LLVM C++ runtimes (docs/cpp-toolchain-plan.md P0+P1): pinned fetch via
# tools/fetch-llvm.sh, then a cmake build of static libc++/libc++abi/
# libunwind against musl (specs-based; see the plan for the measured
# gotchas: no CMAKE_SYSROOT, -I tools/kernel-headers for linux/futex.h).
llvm-cxx: $(LLVM_CXX_STAMP)

$(LLVM_CXX_SRC)/libcxx/CMakeLists.txt: tools/fetch-llvm.sh
	./tools/fetch-llvm.sh

$(LLVM_CXX_CFG): $(LLVM_CXX_SRC)/libcxx/CMakeLists.txt
	rm -rf .build/llvm-cxx $(LLVM_CXX_PREFIX)
	cmake -G "Unix Makefiles" -S $(LLVM_CXX_SRC)/runtimes -B .build/llvm-cxx \
	  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
	  -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
	  -DCMAKE_C_FLAGS="-static -I$(CURDIR)/tools/kernel-headers -specs $(CURDIR)/$(MUSL64_SPECS)" \
	  -DCMAKE_CXX_FLAGS="-static -I$(CURDIR)/tools/kernel-headers -specs $(CURDIR)/$(MUSL64_SPECS)" \
	  -DCMAKE_EXE_LINKER_FLAGS=-static \
	  -DCMAKE_INSTALL_PREFIX=$(CURDIR)/$(LLVM_CXX_PREFIX) \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXXABI_ENABLE_SHARED=OFF \
	  -DLIBUNWIND_ENABLE_SHARED=OFF \
	  -DLIBCXX_ENABLE_STATIC=ON -DLIBCXXABI_ENABLE_STATIC=ON \
	  -DLIBUNWIND_ENABLE_STATIC=ON \
	  -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=ON \
	  -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
	  -DLIBUNWIND_INCLUDE_TESTS=OFF \
	  -DLIBCXX_HAS_MUSL_LIBC=ON

$(LLVM_CXX_STAMP): $(LLVM_CXX_CFG)
	cmake --build .build/llvm-cxx -j$$(nproc)
	cmake --install .build/llvm-cxx
	touch $(LLVM_CXX_STAMP)

dash64: $(DASH64_BIN)
$(DASH64_BIN): $(MUSL64_SPECS)
	cd third_party/dash && ./autogen.sh && \
		CC="$(CURDIR)/tools/musl-gcc64.sh" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(DASH64_BIN)

toybox64: $(TOYBOX64_BIN)
$(TOYBOX64_BIN): $(MUSL64_SPECS) tools/mktoybox.sh tools/musl-gcc64.sh
	TOYBOX_CC="$(CURDIR)/tools/musl-gcc64.sh" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $(TOYBOX64_BIN)

# --- LVGL (third_party/lvgl) static lib for the compositor GUI spike. The
# config lives in include/lv_conf.h (copied from lv_conf_template.h).
LVGL_SRC      = third_party/lvgl/src
LVGL64        = .build/lvgl64/liblvgl.a
LVGL64_OBJ    = .build/lvgl64/src
LVGL64_SRCS   = $(shell find $(LVGL_SRC) -name '*.c')
LVGL64_OBJS   = $(patsubst $(LVGL_SRC)/%.c,$(LVGL64_OBJ)/%.o,$(LVGL64_SRCS))
LVGL64_CFLAGS = -O2 -Iinclude -Ithird_party/lvgl -Ithird_party/lvgl/src \
		-DLV_CONF_INCLUDE_SIMPLE

.PHONY: lvgl64
lvgl64: $(LVGL64)
$(LVGL64): $(LVGL64_OBJS)
	ar rcs $@ $^
$(LVGL64_OBJ)/%.o: $(LVGL_SRC)/%.c include/lv_conf.h
	@mkdir -p $(dir $@)
	$(MUSL64_CC) $(LVGL64_CFLAGS) -c $< -o $@

# --- Xfb: FNX's native X server (fork of Xvfb; the pristine upstream is
# not vendored - regenerate it on demand, see userland/xfb/README.md).
# xfb64 is phony and always delegates: the inner per-file rules own the
# incremental rebuild (a file-prerequisite here would go stale forever,
# since .build/x11/xfb/Xfb has no source deps at this level).
XFB_SRC = userland/xfb
XFB_OUT = .build/x11/xfb
XFB_BIN = $(XFB_OUT)/Xfb

.PHONY: xfb64
xfb64:
	$(MAKE) -C $(XFB_SRC) OUT="$(CURDIR)/$(XFB_OUT)" \
		CC="$(CURDIR)/tools/musl-gcc64.sh" -j8

userland64: $(MUSL64_SPECS) $(DASH64_BIN) $(TOYBOX64_BIN) $(LLVM_CXX_STAMP) $(LVGL64) $(XFB_BIN)
	@mkdir -p $(ROOTFS64)/sbin $(ROOTFS64)/bin $(ROOTFS64)/dev
	# X server (Xfb) + its runtime helper/data locations on the FNX root
	cp $(XFB_BIN) $(ROOTFS64)/bin/Xfb
	cp .build/x11-prefix/bin/xkbcomp $(ROOTFS64)/bin/xkbcomp
	@mkdir -p $(ROOTFS64)/usr/share/X11/xkb/compiled
	$(MAKE) -C third_party/toybox CC="$(CURDIR)/tools/musl-gcc64.sh" install PREFIX="$(CURDIR)/$(ROOTFS64)"
	$(MUSL64_CC) userland/init.c -o $(ROOTFS64)/sbin/init
	$(MUSL64_CXX) userland/cpp_smoke.cpp -o $(ROOTFS64)/bin/cpp_smoke
	$(MUSL64_CC) userland/acl.c -o $(ROOTFS64)/bin/acl
	$(MUSL64_CC) -Iinclude userland/config.c userland/libconfig.c -o $(ROOTFS64)/bin/config
	$(MUSL64_CC) -Iinclude userland/compositor.c -o $(ROOTFS64)/bin/compositor
	$(MUSL64_CC) -Iinclude userland/gui_smoke.c userland/libgui.c -o $(ROOTFS64)/bin/gui_smoke
	$(MUSL64_CC) -Iinclude userland/gui_demo.c userland/libgui.c -o $(ROOTFS64)/bin/gui_demo
	$(MUSL64_CC) $(LVGL64_CFLAGS) -Iuserland userland/lv_demo.c userland/lvapp.c userland/libgui.c $(LVGL64) -o $(ROOTFS64)/bin/lv_demo
	$(MUSL64_CC) -Iinclude tools/shm_leak_test.c -o $(ROOTFS64)/bin/shm_leak_test
	$(MUSL64_CC) -Iinclude tools/shm_resize_test.c userland/libgui.c -o $(ROOTFS64)/bin/shm_resize_test
	$(MUSL64_CC) -Iinclude tools/shm_cap_test.c -o $(ROOTFS64)/bin/shm_cap_test
	# config's three scope directories (docs/config-design.md; the FSH
	# spells them /System, /Shared, Users/$USER). The guest has one
	# user (root).
	@mkdir -p $(ROOTFS64)/System/Configuration $(ROOTFS64)/Shared/Configuration \
		$(ROOTFS64)/Users/root/Configuration
	@cp userland/configuration/system.config.xfb.conf $(ROOTFS64)/System/Configuration/system.config.xfb.conf
	$(MUSL64_CC) userland/pty_test.c -o $(ROOTFS64)/bin/pty_test
	$(MUSL64_CC) userland/bfsquery.c -o $(ROOTFS64)/bin/bfsquery
	$(MUSL64_CC) userland/bfsqtest.c -o $(ROOTFS64)/bin/bfsqtest
	$(MUSL64_CC) userland/tone.c -o $(ROOTFS64)/bin/tone -lm
	$(MUSL64_CC) userland/fbdump.c -o $(ROOTFS64)/bin/fbdump
	cp $(DASH64_BIN) $(ROOTFS64)/bin/sh
	cp userland/test_toybox.sh $(ROOTFS64)/test_toybox.sh
	# DHCP event script for toybox's dhcp client (default location)
	@mkdir -p $(ROOTFS64)/usr/share/dhcp
	@cp userland/dhcp_script.sh $(ROOTFS64)/usr/share/dhcp/default.script
	@chmod +x $(ROOTFS64)/usr/share/dhcp/default.script
	# device nodes: mkext2.py converts each placeholder file under dev/ into
	# a char device inode using the DEVICES table (see tools/mkinitrd.py).
	@touch $(ROOTFS64)/dev/console $(ROOTFS64)/dev/ttyS0 $(ROOTFS64)/dev/null $(ROOTFS64)/dev/zero \
		$(ROOTFS64)/dev/full $(ROOTFS64)/dev/random $(ROOTFS64)/dev/urandom \
		$(ROOTFS64)/dev/mem $(ROOTFS64)/dev/kmem $(ROOTFS64)/dev/port \
		$(ROOTFS64)/dev/tty $(ROOTFS64)/dev/tty0 $(ROOTFS64)/dev/ptmx \
		$(ROOTFS64)/dev/sda $(ROOTFS64)/dev/psaux $(ROOTFS64)/dev/nvme0n1 \
		$(ROOTFS64)/dev/ttyS1
	# mount points for the virtual filesystems init mounts (procfs, devpts)
	@mkdir -p $(ROOTFS64)/proc $(ROOTFS64)/tmp $(ROOTFS64)/dev/pts $(ROOTFS64)/mnt
	# /etc: passwd/group so id, ls -l and chown-by-name work
	@mkdir -p $(ROOTFS64)/etc
	@printf 'root:x:0:0:root:/root:/bin/sh\n' > $(ROOTFS64)/etc/passwd
	@printf 'root:x:0:\n' > $(ROOTFS64)/etc/group
	# /etc/hosts: (none) is UTS_NODENAME; resolving it locally keeps
	# gethostbyname/dnsdomainname out of the DNS resolver (which has a
	# backgrounded-socket deadlock under load)
	@printf '127.0.0.1 localhost\n127.0.0.1 (none)\n' > $(ROOTFS64)/etc/hosts
	@echo "userland64: native x86_64 rootfs staged in $(ROOTFS64)"

# Build the native x86_64 ext2 root filesystem image (.build/root.img) from
# the ELF64 userland tree, so the kernel can boot /sbin/init straight off
# hdb (no initrd). Attached as a second IDE disk (hdb), it becomes the boot
# root via the kernel cmdline 'root=/dev/hdb rootfstype=ext2'.
rootdisk64: userland64
	python3 tools/mkext2.py $(ROOTFS64) .build/root.img 8
	@echo "rootdisk64: .build/root.img ready (ext2, 8MB, native x86_64 userland)"

# BFS root image (OpenBFS M4f): the same userland tree packed into a BeOS
# BFS image by tools/mkbfs.py (multi-node dir trees, indirect +
# double-indirect streams, symlinks). BFS is the DEFAULT root device
# (make run / run-uefi); 64MB leaves headroom for the X11 userland (Xfb
# is a ~16MB static binary).
rootbfs: userland64
	python3 tools/mkbfs.py $(ROOTFS64) .build/rootbfs.img 64
	python3 tools/bfscheck.py .build/rootbfs.img $(ROOTFS64)
	@echo "rootbfs: .build/rootbfs.img ready (BFS, 64MB, native x86_64 userland)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh

compile64:
	@rm -rf .build/64
	@for n in kernel kernel/syscalls mm fs drivers/char drivers/block drivers/pci drivers/video net lib ; do \
		for f in $$(find $$n -name '*.c') ; do \
			o=".build/64/$${f%.c}.o" ; \
			mkdir -p "$$(dirname "$$o")" ; \
			$(CC64) -c -o "$$o" "$$f" || exit 1 ; \
		done ; \
	done
	@echo "compile64: all C sources compile clean as -m64 (no link)"

# 64-bit kernel build flags (long mode). -march must be x86-64, not i386.
# The EFI stub MUST be position-independent (-fPIC): UEFI loads the PE32+
# image at an arbitrary address, and PE base relocations cannot fix up the
# absolute 32-bit immediates that -fno-pie would emit for string literals.
# -fno-semantic-interposition keeps references to our own globals direct
# (RIP-relative) so no GOT is needed in the PE.
CC64K = gcc -m64 -march=x86-64 $(LANG) -D__KERNEL__ -I$(INCLUDE) -O2 \
	-fPIC -fno-semantic-interposition -fno-common -ffreestanding \
	-mno-red-zone -mno-sse -mno-sse2 -fno-asynchronous-unwind-tables \
	-fno-stack-protector -Wall -Wstrict-prototypes \
	-DCONFIG_FS_MINIX $(M6DEBUG)

# FNX: PE32+ UEFI application (EFI stub boot). See docs/port-longmode-uefi.txt.
# M4-B: the 64-bit kernel is now the REAL FNX kernel + kernel64 primitives;
# 'build64' builds that. The standalone stub/demo kernel (M1-M4-A demos) is
# kept as 'buildfnxdemo'.
build64: buildfnx

buildfnxdemo: .build/64/fnxdemo.efi

.build/64/fnxdemo.efi: kernel64/efi_stub.c kernel64/main64.c kernel64/paging64.c kernel64/mm64.c kernel64/idt64.c kernel64/gdt64.c kernel64/irq64.c kernel64/msix64.c kernel64/sched64.c kernel64/switch64.S include/fnx/efi.h kernel64/serial64.h
	@mkdir -p .build/64
	$(CC64K) -c -o .build/64/efi_stub.o kernel64/efi_stub.c
	$(CC64K) -c -o .build/64/main64.o kernel64/main64.c
	$(CC64K) -c -o .build/64/paging64.o kernel64/paging64.c
	$(CC64K) -c -o .build/64/mm64.o kernel64/mm64.c
	$(CC64K) -c -o .build/64/idt64.o kernel64/idt64.c
	$(CC64K) -c -o .build/64/gdt64.o kernel64/gdt64.c
	$(CC64K) -c -o .build/64/irq64.o kernel64/irq64.c
	$(CC64K) -c -o .build/64/msix64.o kernel64/msix64.c
	$(CC64K) -c -o .build/64/sched64.o kernel64/sched64.c
	$(CC64K) -c -o .build/64/probe64.o kernel64/probe64.c
	gcc -c -o .build/64/switch64.o kernel64/switch64.S
	$(LD) -m i386pep --entry efi_main --image-base 0x1000000 -o $@ \
		.build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/probe64.o .build/64/switch64.o
	objcopy --remove-section .comment --subsystem 10 $@
	@echo "build64: $@ ready (PE32+ EFI application)"

# M4-B: 64-bit build of the REAL FNX kernel (kernel/mm/fs/lib/drivers C
# sources) linked with the kernel64 primitives (paging64/gdt64/idt64/irq64)
# and the EFI stub. The real sources are compiled -m64 with
# -fvisibility=hidden so extern globals (kstat, current, ...) resolve
# PC-relative instead of via the GOT (which ld -m i386pep mishandles).
# gcc 14.x with -fPIC + -fvisibility=hidden emits `mov sym(%rip),%rax`
# instead of `lea sym(%rip),%rax` for extern DATA addresses;
# patch_pic_data.py fixes the one-byte opcode (0x8b -> 0x8d) in the objects.
CC64R = $(CC64K) -fvisibility=hidden -MMD -MP
REALDIR = .build/64real
OBJDIR64 = .build/64
REALSRCS = $(shell find kernel mm fs lib drivers net -name '*.c' 2>/dev/null | grep -v 'font-lat9-')
REALOBJS = $(patsubst %.c,$(REALDIR)/%.o,$(REALSRCS))
# dependency files (.d) generated by -MMD -MP: one per object, so a change
# to any header (e.g. include/fnx/process.h) rebuilds exactly the objects
# that include it. Previously a header edit silently left mixed struct
# sizes across .o files (the struct-proc boot crash) unless the whole
# .build/64real tree was removed.
REALDEPS = $(patsubst %.o,%.d,$(REALOBJS))

# gcc 14.x with -fPIC -fvisibility=hidden emits `mov sym(%rip),%rax` instead
# of `lea sym(%rip),%rax` for extern DATA addresses; patch_pic_data.py fixes
# the one-byte opcode in the objects (0x8b -> 0x8d).
PATCH_PIC = tools/patch_pic_data.py

K64SRCS = kernel64/efi_stub.c kernel64/main64.c kernel64/paging64.c kernel64/mm64.c kernel64/idt64.c kernel64/gdt64.c kernel64/irq64.c kernel64/msix64.c kernel64/sched64.c kernel64/probe64.c kernel64/user64.c kernel64/kreal64.c kernel64/asm64.c kernel64/sections64.c kernel64/initrd64.c kernel64/switch64.S kernel64/init_trampoline64.S
K64OBJS = $(patsubst kernel64/%.c,$(OBJDIR64)/%.o,$(filter %.c,$(K64SRCS))) \
          $(patsubst kernel64/%.S,$(OBJDIR64)/%.o,$(filter %.S,$(K64SRCS)))
# PATCH_PIC only applies to the C-compiled objects (the .S ones are asm)
K64PICOBJS = $(patsubst kernel64/%.c,$(OBJDIR64)/%.o,$(filter %.c,$(K64SRCS)))
K64DEPS = $(patsubst %.o,%.d,$(K64PICOBJS))

buildfnx: .build/64/fnx.efi

.build/64/fnx.efi: $(REALOBJS) $(K64OBJS) include/fnx/efi.h kernel64/serial64.h
	@mkdir -p .build/64
	python3 $(PATCH_PIC) $(K64PICOBJS) > /dev/null
	$(LD) -m i386pep --entry efi_main --image-base 0x1000000 -o $@ \
		$(REALOBJS) \
		$(K64OBJS)
	objcopy --remove-section .comment --subsystem 10 $@
	@echo "buildfnx: $@ ready (PE32+ EFI application, REAL kernel + kernel64 primitives)"

$(REALDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC64R) -c -o $@ $<
	python3 $(PATCH_PIC) $@

# pull in the generated header dependencies (-MMD -MP wrote the .d files)
-include $(REALDEPS) $(K64DEPS)

# kernel64 primitives + EFI stub: pattern rule with header deps too.
# Uses CC64R (with -fvisibility=hidden -MMD -MP) exactly like the original
# inline compiles in the .efi rule.
# PATCH_PIC is applied once in the .efi rule (not here), matching the
# original single-pass build.
$(OBJDIR64)/%.o: kernel64/%.c
	@mkdir -p $(OBJDIR64)
	$(CC64R) -c -o $@ $<

$(OBJDIR64)/%.o: kernel64/%.S
	@mkdir -p $(OBJDIR64)
	gcc -c $(M6DEBUG) -o $@ $<

