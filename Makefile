# fiwix/Makefile
#
# Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
# Distributed under the terms of the Fiwix License.
#

TOPDIR := $(shell if [ "$$PWD" != "" ] ; then echo $$PWD ; else pwd ; fi)
INCLUDE = $(TOPDIR)/include
TMPFILE := $(shell mktemp)

ARCH = -m32
CPU = -march=i386
LANG = -std=c89

# CCEXE can be overridden at the command line. For example: make CCEXE="tcc"
# To use tcc see docs/tcc.txt
CCEXE=gcc

CC = $(CROSS_COMPILE)$(CCEXE) $(ARCH) $(CPU) $(LANG) -D__KERNEL__ $(CONFFLAGS) #-D__DEBUG__
CFLAGS = -I$(INCLUDE) -O2 -fno-pie -fno-common -ffreestanding -Wall -Wstrict-prototypes #-Wextra -Wno-unused-parameter

ifeq ($(CCEXE),gcc)
LD = $(CROSS_COMPILE)ld
CPP = $(CROSS_COMPILE)cpp -P -I$(INCLUDE) -U__x86_64__
LIBGCC := -L$(shell dirname `$(CC) -print-libgcc-file-name`) -lgcc
LDFLAGS = -m elf_i386
endif

ifeq ($(CCEXE),tcc)
LD = $(CROSS_COMPILE)$(CCEXE) $(ARCH)
LDFLAGS = -static -nostdlib -nostdinc
# If you define CONFIG_VM_SPLIT22 this should be 0x80100000: make CCEXE="tcc" TEXTADDR="0x80100000"
TEXTADDR = 0xC0100000
endif


DIRS =	kernel \
	kernel/syscalls \
	mm \
	fs \
	drivers/char \
	drivers/block \
	drivers/pci \
	drivers/video \
	net \
	lib

OBJS = 	kernel/*.o \
	kernel/syscalls/*.o \
	mm/*.o \
	fs/*.o \
	fs/devpts/*.o \
	fs/ext2/*.o \
	fs/iso9660/*.o \
	fs/minix/*.o \
	fs/pipefs/*.o \
	fs/procfs/*.o \
	fs/sockfs/*.o \
	drivers/char/*.o \
	drivers/block/*.o \
	drivers/pci/*.o \
	drivers/video/*.o \
	net/*.o \
	lib/*.o

export CC LD CFLAGS LDFLAGS INCLUDE

all:
	@echo "#define UTS_VERSION \"`date -u`\"" > include/fiwix/version.h
	@for n in $(DIRS) ; do (cd $$n ; $(MAKE)) || exit ; done
ifeq ($(CCEXE),gcc)
	$(CPP) $(CONFFLAGS) fiwix.ld > $(TMPFILE)
	$(LD) -N -T $(TMPFILE) $(LDFLAGS) $(OBJS) $(LIBGCC) -o fiwix
	rm -f $(TMPFILE)
	nm fiwix | sort | gzip -9c > System.map.gz
endif
ifeq ($(CCEXE),tcc)
	$(LD) -Wl,-Ttext=$(TEXTADDR) $(LDFLAGS) $(OBJS) -o fiwix
endif

clean:
	@for n in $(DIRS) ; do (cd $$n ; $(MAKE) clean) ; done
	rm -f *.o fiwix System.map.gz
	rm -rf .build/64

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

run: fiwix
	@if [ -n "$${DISPLAY}$${WAYLAND_DISPLAY}" ] && [ -t 1 ]; then \
		./tools/qemu.sh -display gtk -serial stdio -m 128M -kernel fiwix -append "console=/dev/ttyS0" $(QEMU_EXTRA); \
	elif [ -t 1 ]; then \
		./tools/qemu.sh -display curses -m 128M -kernel fiwix -append "console=/dev/ttyS0" $(QEMU_EXTRA); \
	else \
		./tools/qemu.sh -nographic -m 128M -kernel fiwix -append "console=/dev/ttyS0" $(QEMU_EXTRA); \
	fi

run-uefi: .build/ovmf/OVMF.fd rootdisk build64
	@./tools/mkesp.sh
	@if [ -n "$${DISPLAY}$${WAYLAND_DISPLAY}" ] && [ -t 1 ]; then \
		FIWIX_QEMU_BIOS=ovmf ./tools/qemu.sh -display gtk -serial stdio -m 128M -drive file=.build/esp.img,format=raw -drive file=.build/root.img,format=raw $(QEMU_EXTRA); \
	elif [ -t 1 ]; then \
		FIWIX_QEMU_BIOS=ovmf ./tools/qemu.sh -display curses -m 128M -drive file=.build/esp.img,format=raw -drive file=.build/root.img,format=raw $(QEMU_EXTRA); \
	else \
		FIWIX_QEMU_BIOS=ovmf ./tools/qemu.sh -nographic -m 128M -drive file=.build/esp.img,format=raw -drive file=.build/root.img,format=raw $(QEMU_EXTRA); \
	fi

# --- Fiwix64 static musl i386 userland (init + sh), packed into the initrd ---
# `make userland` builds /sbin/init + /bin/sh under .build/rootfs and
# regenerates kernel64/initrd64.c (the embedded minix-v1 initrd) with
# tools/mkinitrd.py. `make run-uefi` does this automatically.
# musl is a vendored submodule (third_party/musl); dash must be cloned into
# third_party/dash (git.kernel.org) or the tree built once with `make dash`.
MUSL_PREFIX = .build/musl
MUSL_SPECS  = $(MUSL_PREFIX)/lib/musl-gcc.specs
MUSL_CC     = gcc -m32 -static -Wl,-m,elf_i386 -specs $(MUSL_SPECS)
ROOTFS      = .build/rootfs
DASH_BIN    = third_party/dash/src/dash

.PHONY: userland musl dash

musl: $(MUSL_SPECS)
$(MUSL_SPECS):
	cd third_party/musl && \
		CC="gcc -m32" ./configure --target=i386 --prefix=$(CURDIR)/$(MUSL_PREFIX) && \
		sed -i 's/^CROSS_COMPILE = .*/CROSS_COMPILE =/' config.mak && \
		$(MAKE) && $(MAKE) install

dash: $(DASH_BIN)
$(DASH_BIN):
	cd third_party/dash && ./autogen.sh && \
		CC="$(CURDIR)/tools/musl-gcc.sh" ./configure --host=i386-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash

userland: $(MUSL_SPECS) $(DASH_BIN)
	@mkdir -p $(ROOTFS)/sbin $(ROOTFS)/bin $(ROOTFS)/dev
	$(MUSL_CC) userland/init.c -o $(ROOTFS)/sbin/init
	cp $(DASH_BIN) $(ROOTFS)/bin/sh
	touch $(ROOTFS)/dev/console
	python3 tools/mkinitrd.py $(ROOTFS) .build/initrd/initrd.img kernel64/initrd64.c
	@echo "userland: initrd regenerated from $(ROOTFS)"

# Build a persistent ext2 root filesystem image (.build/root.img) from the
# same rootfs tree. Attached as a second IDE disk (hdb), it becomes the boot
# root via the kernel cmdline 'root=/dev/hdb rootfstype=ext2'.
rootdisk: userland
	python3 tools/mkext2.py $(ROOTFS) .build/root.img 8
	@echo "rootdisk: .build/root.img ready (ext2, 8MB)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh

compile64:
	@rm -rf .build/64
	@for n in $(DIRS) ; do \
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
	gcc -c -o .build/64/switch64.o kernel64/switch64.S
	$(LD) -m i386pep --entry efi_main --image-base 0x1000000 -o $@ \
		.build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/switch64.o
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
REALSRCS = $(shell find kernel mm fs lib drivers -name '*.c' 2>/dev/null | grep -v 'font-lat9-')
REALOBJS = $(patsubst %.c,$(REALDIR)/%.o,$(REALSRCS))

# gcc 14.x with -fPIC -fvisibility=hidden emits `mov sym(%rip),%rax` instead
# of `lea sym(%rip),%rax` for extern DATA addresses; patch_pic_data.py fixes
# the one-byte opcode in the objects (0x8b -> 0x8d).
PATCH_PIC = tools/patch_pic_data.py

build64real: .build/64/fiwix64.efi

.build/64/fiwix64.efi: $(REALOBJS) kernel64/efi_stub.c kernel64/main64.c kernel64/paging64.c kernel64/mm64.c kernel64/idt64.c kernel64/gdt64.c kernel64/irq64.c kernel64/sched64.c kernel64/user64.c kernel64/kreal64.c kernel64/asm64.c kernel64/sections64.c kernel64/initrd64.c kernel64/switch64.S include/fiwix/efi.h kernel64/serial64.h
	@mkdir -p .build/64
	$(CC64R) -c -o .build/64/efi_stub.o kernel64/efi_stub.c
	$(CC64R) -c -o .build/64/main64.o kernel64/main64.c
	$(CC64R) -c -o .build/64/paging64.o kernel64/paging64.c
	$(CC64R) -c -o .build/64/mm64.o kernel64/mm64.c
	$(CC64R) -c -o .build/64/idt64.o kernel64/idt64.c
	$(CC64R) -c -o .build/64/gdt64.o kernel64/gdt64.c
	$(CC64R) -c -o .build/64/irq64.o kernel64/irq64.c
	$(CC64R) -c -o .build/64/sched64.o kernel64/sched64.c
	$(CC64R) -c -o .build/64/user64.o kernel64/user64.c
	$(CC64R) -c -o .build/64/kreal64.o kernel64/kreal64.c
	$(CC64R) -c -o .build/64/asm64.o kernel64/asm64.c
	$(CC64R) -c -o .build/64/sections64.o kernel64/sections64.c
	$(CC64R) -c -o .build/64/initrd64.o kernel64/initrd64.c
	gcc -c $(M6DEBUG) -o .build/64/switch64.o kernel64/switch64.S
	python3 $(PATCH_PIC) .build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/user64.o .build/64/kreal64.o .build/64/asm64.o .build/64/sections64.o .build/64/initrd64.o > /dev/null
	$(LD) -m i386pep --entry efi_main --image-base 0x1000000 -o $@ \
		$(REALOBJS) \
		.build/64/efi_stub.o .build/64/main64.o .build/64/paging64.o .build/64/mm64.o .build/64/idt64.o .build/64/gdt64.o .build/64/irq64.o .build/64/sched64.o .build/64/user64.o .build/64/kreal64.o .build/64/asm64.o .build/64/sections64.o .build/64/initrd64.o .build/64/switch64.o
	objcopy --remove-section .comment --subsystem 10 $@
	@echo "build64real: $@ ready (PE32+ EFI application, REAL kernel + kernel64 primitives)"

$(REALDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC64R) -c -o $@ $<
	python3 $(PATCH_PIC) $@

