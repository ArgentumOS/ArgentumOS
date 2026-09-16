# fnx/Makefile — top-level driver; the heavy lifting lives in mk/*.mk
#
# Copyright 2018-2022, Jordi Sanfeliu. All rights reserved.
# Distributed under the terms of the Fiwix License.
#
# The 32-bit i386 build was REMOVED in the FNX pivot: this tree builds
# only the 64-bit long-mode kernel (PE32+ UEFI application via buildfnx).
#
# The mk/ fragments are included in order and together form one make
# database (fragments 00-40 below follow the file's historical order:
# common vars/harness, clang toolchain builds, userland, images, kernel).
# 60-host is the OUT-OF-BAND one: it builds the toolkit for THIS machine to
# run on (see the fragment's header). Nothing in the guest build depends on
# it, and a host build never touches .build/64 or .build/rootagfs.img.

# Default target: the 64-bit UEFI kernel.
all: buildfnx
	@echo "make: .build/64/fnx.efi ready (FNX, 64-bit only)"

clean:
	rm -rf .build/64 .build/64real
	rm -f *.o fnx System.map.gz

.PHONY: all clean

include mk/00-base.mk
include mk/10-toolchain.mk
include mk/20-userland.mk
include mk/30-images.mk
include mk/40-kernel.mk
include mk/50-tests.mk
include mk/60-host.mk
