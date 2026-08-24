# fiwix/Makefile
#
# Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
# Distributed under the terms of the Fiwix License.
#

TOPDIR := $(shell if [ "$$PWD" != "" ] ; then echo $$PWD ; else pwd ; fi)
INCLUDE = $(TOPDIR)/include
TMPFILE := $(shell mktemp)

LANG = -std=c89

# The 32-bit i386 build was REMOVED in the Fiwix64 pivot: this tree builds
# only the 64-bit long-mode kernel (PE32+ UEFI application via build64real).
CCEXE=gcc

LD = $(CROSS_COMPILE)ld

export LD INCLUDE

# Default target: the 64-bit UEFI kernel.
all: build64real
	@echo "make: .build/64/fiwix64.efi ready (Fiwix64, 64-bit only)"

clean:
	rm -rf .build/64 .build/64real
	rm -f *.o fiwix System.map.gz

# ---------------------------------------------------------------------------
# Development harness (QEMU / UEFI). See docs/port-longmode-uefi.txt.
#
#   make run        boot the 32-bit kernel via QEMU's built-in Multiboot
#                   loader (no GRUB needed). Without a root filesystem the
#                   kernel stops at the expected 'root device not defined'
#                   panic - that is the smoke test.
#   make ovmf       fetch OVMF firmware without root (into .build/ovmf).
#   make run-uefi   boot OVMF (UEFI) firmware with the Fiwix64 EFI stub
#                   (PE32+ kernel from make build64).
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
# Overrides: QEMU_EXTRA (extra qemu args), FIWIX_QEMU_TOOLS / QEMU_TOOLS (tools prefix).
QEMU_TOOLS ?= $(HOME)/.local/share/fiwix-qemu-tools
# ---------------------------------------------------------------------------

CC64 = gcc -m64 -march=x86-64 $(LANG) -D__KERNEL__ $(CONFFLAGS) -I$(INCLUDE) -O2 \
       -fno-pie -fno-common -ffreestanding -mno-red-zone -mno-sse -mno-sse2 \
       -fno-asynchronous-unwind-tables -Wall -Wstrict-prototypes

run: .build/ovmf/OVMF.fd rootdisk64 build64
	@./tools/mkesp.sh
	@if [ -n "$${DISPLAY}$${WAYLAND_DISPLAY}" ] && [ -t 1 ]; then \
		FIWIX_QEMU_BIOS=ovmf ./tools/qemu.sh -display gtk -serial stdio -m 128M -drive file=.build/esp.img,format=raw -drive file=.build/root.img,format=raw $(QEMU_EXTRA); \
	elif [ -t 1 ]; then \
		FIWIX_QEMU_BIOS=ovmf ./tools/qemu.sh -display curses -m 128M -drive file=.build/esp.img,format=raw -drive file=.build/root.img,format=raw $(QEMU_EXTRA); \
	else \
		FIWIX_QEMU_BIOS=ovmf ./tools/qemu.sh -nographic -m 128M -drive file=.build/esp.img,format=raw -drive file=.build/root.img,format=raw $(QEMU_EXTRA); \
	fi

run-uefi: run

# --- Fiwix64 native x86_64 userland (port phase B): static ELF64 binaries
# --- built with tools/musl-gcc64.sh into .build/rootfs64, packed into an
# --- ext2 root image (.build/root.img) attached as the second IDE disk.

# ---------------------------------------------------------------------------
# Native x86_64 userland (port phase B): same tree, LP64 ABI. Built with
# tools/musl-gcc64.sh into .build/rootfs64. The binaries are static ELF64
# and can be smoke-tested on the host before the kernel can exec them.
MUSL64_PREFIX = .build/musl64
MUSL64_SPECS  = $(MUSL64_PREFIX)/lib/musl-gcc.specs
MUSL64_CC     = gcc -static -specs $(MUSL64_SPECS)
ROOTFS64      = .build/rootfs64
DASH64_BIN    = third_party/dash/src/dash64
TOYBOX64_BIN  = third_party/toybox/toybox64

.PHONY: userland64 musl64 dash64 toybox64

musl64: $(MUSL64_SPECS)
$(MUSL64_SPECS):
	cd third_party/musl && \
		make clean >/dev/null 2>&1 || true && \
		CC="gcc" ./configure --target=x86_64 --prefix=$(CURDIR)/$(MUSL64_PREFIX) && \
		sed -i 's/^CROSS_COMPILE = .*/CROSS_COMPILE =/' config.mak && \
		$(MAKE) && $(MAKE) install

dash64: $(DASH64_BIN)
$(DASH64_BIN): $(MUSL64_SPECS)
	cd third_party/dash && ./autogen.sh && \
		CC="$(CURDIR)/tools/musl-gcc64.sh" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(DASH64_BIN)

toybox64: $(TOYBOX64_BIN)
$(TOYBOX64_BIN): $(MUSL64_SPECS) tools/mktoybox.sh tools/musl-gcc64.sh
	TOYBOX_CC="$(CURDIR)/tools/musl-gcc64.sh" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $(TOYBOX64_BIN)

userland64: $(MUSL64_SPECS) $(DASH64_BIN) $(TOYBOX64_BIN)
	@mkdir -p $(ROOTFS64)/sbin $(ROOTFS64)/bin $(ROOTFS64)/dev
	$(MAKE) -C third_party/toybox CC="$(CURDIR)/tools/musl-gcc64.sh" install PREFIX="$(CURDIR)/$(ROOTFS64)"
	$(MUSL64_CC) userland/init.c -o $(ROOTFS64)/sbin/init
	cp $(DASH64_BIN) $(ROOTFS64)/bin/sh
	cp userland/test_toybox.sh $(ROOTFS64)/test_toybox.sh
	# device nodes: mkext2.py converts each placeholder file under dev/ into
	# a char device inode using the DEVICES table (see tools/mkinitrd.py).
	@touch $(ROOTFS64)/dev/console $(ROOTFS64)/dev/ttyS0 $(ROOTFS64)/dev/null $(ROOTFS64)/dev/zero \
		$(ROOTFS64)/dev/full $(ROOTFS64)/dev/random $(ROOTFS64)/dev/urandom \
		$(ROOTFS64)/dev/mem $(ROOTFS64)/dev/kmem $(ROOTFS64)/dev/port \
		$(ROOTFS64)/dev/tty $(ROOTFS64)/dev/tty0
	# /etc: passwd/group so id, ls -l and chown-by-name work
	@mkdir -p $(ROOTFS64)/etc
	@printf 'root:x:0:0:root:/root:/bin/sh\n' > $(ROOTFS64)/etc/passwd
	@printf 'root:x:0:\n' > $(ROOTFS64)/etc/group
	@echo "userland64: native x86_64 rootfs staged in $(ROOTFS64)"

# Build the native x86_64 ext2 root filesystem image (.build/root.img) from
# the ELF64 userland tree, so the kernel can boot /sbin/init straight off
# hdb (no initrd). Attached as a second IDE disk (hdb), it becomes the boot
# root via the kernel cmdline 'root=/dev/hdb rootfstype=ext2'.
rootdisk64: userland64
	python3 tools/mkext2.py $(ROOTFS64) .build/root.img 8
	@echo "rootdisk64: .build/root.img ready (ext2, 8MB, native x86_64 userland)"

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

# Fiwix64: PE32+ UEFI application (EFI stub boot). See docs/port-longmode-uefi.txt.
# M4-B: the 64-bit kernel is now the REAL Fiwix kernel + kernel64 primitives;
# 'build64' builds that. The standalone stub/demo kernel (M1-M4-A demos) is
# kept as 'build64demo'.
build64: build64real

build64demo: .build/64/fiwix64demo.efi

.build/64/fiwix64demo.efi: kernel64/efi_stub.c kernel64/main64.c kernel64/paging64.c kernel64/mm64.c kernel64/idt64.c kernel64/gdt64.c kernel64/irq64.c kernel64/sched64.c kernel64/switch64.S include/fiwix/efi.h kernel64/serial64.h
	@mkdir -p .build/64
	$(CC64K) -c -o .build/64/efi_stub.o kernel64/efi_stub.c
	$(CC64K) -c -o .build/64/main64.o kernel64/main64.c
	$(CC64K) -c -o .build/64/paging64.o kernel64/paging64.c
	$(CC64K) -c -o .build/64/mm64.o kernel64/mm64.c
	$(CC64K) -c -o .build/64/idt64.o kernel64/idt64.c
	$(CC64K) -c -o .build/64/gdt64.o kernel64/gdt64.c
	$(CC64K) -c -o .build/64/irq64.o kernel64/irq64.c
	$(CC64K) -c -o .build/64/sched64.o kernel64/sched64.c
	gcc -c -o .build/64/probe64.o kernel64/probe64.c
	gcc -c -o .build/64/switch64.o kernel64/switch64.S
	$(LD) -m i386pep --entry efi_main --image-base 0x1000000 -o $@ \
		.build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/probe64.o .build/64/switch64.o
	objcopy --remove-section .comment --subsystem 10 $@
	@echo "build64: $@ ready (PE32+ EFI application)"

# M4-B: 64-bit build of the REAL Fiwix kernel (kernel/mm/fs/lib/drivers C
# sources) linked with the kernel64 primitives (paging64/gdt64/idt64/irq64)
# and the EFI stub. The real sources are compiled -m64 with
# -fvisibility=hidden so extern globals (kstat, current, ...) resolve
# PC-relative instead of via the GOT (which ld -m i386pep mishandles).
# gcc 14.x with -fPIC + -fvisibility=hidden emits `mov sym(%rip),%rax`
# instead of `lea sym(%rip),%rax` for extern DATA addresses;
# patch_pic_data.py fixes the one-byte opcode (0x8b -> 0x8d) in the objects.
CC64R = $(CC64K) -fvisibility=hidden
REALDIR = .build/64real
REALSRCS = $(shell find kernel mm fs lib drivers net -name '*.c' 2>/dev/null | grep -v 'font-lat9-')
REALOBJS = $(patsubst %.c,$(REALDIR)/%.o,$(REALSRCS))

# gcc 14.x with -fPIC -fvisibility=hidden emits `mov sym(%rip),%rax` instead
# of `lea sym(%rip),%rax` for extern DATA addresses; patch_pic_data.py fixes
# the one-byte opcode in the objects (0x8b -> 0x8d).
PATCH_PIC = tools/patch_pic_data.py

build64real: .build/64/fiwix64.efi

.build/64/fiwix64.efi: $(REALOBJS) kernel64/efi_stub.c kernel64/main64.c kernel64/paging64.c kernel64/mm64.c kernel64/idt64.c kernel64/gdt64.c kernel64/irq64.c kernel64/sched64.c kernel64/probe64.c kernel64/user64.c kernel64/kreal64.c kernel64/asm64.c kernel64/sections64.c kernel64/initrd64.c kernel64/switch64.S kernel64/init_trampoline64.S include/fiwix/efi.h kernel64/serial64.h
	@mkdir -p .build/64
	$(CC64R) -c -o .build/64/efi_stub.o kernel64/efi_stub.c
	$(CC64R) -c -o .build/64/main64.o kernel64/main64.c
	$(CC64R) -c -o .build/64/paging64.o kernel64/paging64.c
	$(CC64R) -c -o .build/64/mm64.o kernel64/mm64.c
	$(CC64R) -c -o .build/64/idt64.o kernel64/idt64.c
	$(CC64R) -c -o .build/64/gdt64.o kernel64/gdt64.c
	$(CC64R) -c -o .build/64/irq64.o kernel64/irq64.c
	$(CC64R) -c -o .build/64/sched64.o kernel64/sched64.c
	$(CC64R) -c -o .build/64/probe64.o kernel64/probe64.c
	$(CC64R) -c -o .build/64/user64.o kernel64/user64.c
	$(CC64R) -c -o .build/64/kreal64.o kernel64/kreal64.c
	$(CC64R) -c -o .build/64/asm64.o kernel64/asm64.c
	$(CC64R) -c -o .build/64/sections64.o kernel64/sections64.c
	$(CC64R) -c -o .build/64/initrd64.o kernel64/initrd64.c
	gcc -c $(M6DEBUG) -o .build/64/switch64.o kernel64/switch64.S
	gcc -c $(M6DEBUG) -o .build/64/init_trampoline64.o kernel64/init_trampoline64.S
	python3 $(PATCH_PIC) .build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/probe64.o .build/64/user64.o .build/64/kreal64.o .build/64/asm64.o .build/64/sections64.o .build/64/initrd64.o > /dev/null
	$(LD) -m i386pep --entry efi_main --image-base 0x1000000 -o $@ \
		$(REALOBJS) \
		.build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/probe64.o .build/64/user64.o .build/64/kreal64.o .build/64/asm64.o .build/64/sections64.o .build/64/initrd64.o .build/64/switch64.o .build/64/init_trampoline64.o
	objcopy --remove-section .comment --subsystem 10 $@
	@echo "build64real: $@ ready (PE32+ EFI application, REAL kernel + kernel64 primitives)"

$(REALDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC64R) -c -o $@ $<
	python3 $(PATCH_PIC) $@

