musl64: $(MUSL64_LIBC)
$(MUSL64_LIBC): third_party/musl-fsh.patch third_party/musl-pwconf.patch third_party/musl-hosts.patch
	cd third_party/musl && \
		make clean >/dev/null 2>&1 || true && \
		rm -f src/passwd/pwconf.c src/passwd/pwconf.h && \
		git apply $(CURDIR)/third_party/musl-fsh.patch && \
		git apply $(CURDIR)/third_party/musl-pwconf.patch && \
		git apply $(CURDIR)/third_party/musl-hosts.patch && \
		CC="$(MUSL64_BUILD_CC)" ./configure --target=x86_64 --prefix=$(CURDIR)/$(MUSL64_PREFIX) --syslibdir=/System/Libraries && \
		sed -i 's/^CROSS_COMPILE = .*/CROSS_COMPILE =/' config.mak && \
		sed -i 's|^LIBCC = .*|LIBCC = $(CURDIR)/$(COMPILER_RT_BUILTINS)|' config.mak && \
		$(MAKE) && $(MAKE) install && \
		ln -f $(CURDIR)/$(MUSL64_PREFIX)/lib/libc.so $(CURDIR)/$(MUSL64_PREFIX)/lib/ld-musl-x86_64.so.1 && \
		git checkout -- . && \
		rm -f src/passwd/pwconf.c src/passwd/pwconf.h

# crtbegin.o/crtend.o for C++ (docs/llvm-clang-toolchain-plan.md M2): the
# standalone builtins cmake does not emit crt objects, but clang++ links
# need them - crtbegin defines __dso_handle (libc++ locale/guard code
# references it) and registers .eh_frame, crtend closes the section.
# Compiled from the pinned compiler-rt sources with the static wrapper.
COMPILER_RT_CRTBEGIN = .build/compiler-rt/lib/linux/crtbegin.o
COMPILER_RT_CRTEND   = .build/compiler-rt/lib/linux/crtend.o

$(COMPILER_RT_CRTBEGIN): .build/llvm-src/compiler-rt/lib/builtins/crtbegin.c $(MUSL64_CC_STATIC)
	$(MUSL64_CC_STATIC) -c $< -o $@

$(COMPILER_RT_CRTEND): .build/llvm-src/compiler-rt/lib/builtins/crtend.c $(MUSL64_CC_STATIC)
	$(MUSL64_CC_STATIC) -c $< -o $@

# PIC crtbeginS.o/crtendS.o: shared-object links (libc++.so etc.) also need
# __dso_handle + frame registration, but crtbegin.o is non-PIC - a .so
# cannot take R_X86_64_PC32 relocations against it. Each module defines its
# own hidden __dso_handle (the glibc crtbeginS model).
COMPILER_RT_CRTBEGINS = .build/compiler-rt/lib/linux/crtbeginS.o
COMPILER_RT_CRTENDS   = .build/compiler-rt/lib/linux/crtendS.o
# LLVM C++ runtimes (docs/cpp-toolchain-plan.md P0+P1): pinned fetch via
# tools/fetch-llvm.sh, then a cmake build of static libc++/libc++abi/
# libunwind against musl. Since M2 the compilers are the clang wrappers
# (docs/llvm-clang-toolchain-plan.md M2): the static clang wrapper for C,
# the self-bootstrapping tools/musl-clang++64.sh for C++ (it links the
# musl crt/-lc/compiler-rt itself and only adds the libc++ pieces once
# they are installed, so the runtimes build with it). No CMAKE_SYSROOT;
# the wrapper flags carry -I tools/kernel-headers for linux/futex.h.
llvm-cxx: $(LLVM_CXX_STAMP)

$(LLVM_CXX_SRC)/libcxx/CMakeLists.txt: tools/fetch-llvm.sh
	./tools/fetch-llvm.sh

$(LLVM_CXX_CFG): $(LLVM_CXX_SRC)/libcxx/CMakeLists.txt
	rm -rf .build/llvm-cxx $(LLVM_CXX_PREFIX)
	# FNX ships no libatomic: the config probe finds the HOST libatomic
	# and private-links it into libc++.so (a vestigial NEEDED the guest
	# loader would hard-fail on - libc++.so uses no __atomic_* symbols).
	sed -i 's/check_library_exists(atomic __atomic_fetch_add_8 "" LIBCXX_HAS_ATOMIC_LIB)/set(LIBCXX_HAS_ATOMIC_LIB NO)/' \
		$(LLVM_CXX_SRC)/libcxx/cmake/config-ix.cmake
	cmake -G "Unix Makefiles" -S $(LLVM_CXX_SRC)/runtimes -B .build/llvm-cxx \
	  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
	  -DCMAKE_C_COMPILER=$(MUSL64_CC_STATIC) \
	  -DCMAKE_CXX_COMPILER=$(CURDIR)/tools/musl-clang++64.sh \
	  -DCMAKE_C_FLAGS="" \
	  -DCMAKE_CXX_FLAGS="" \
	  -DCMAKE_EXE_LINKER_FLAGS="" \
	  -DCMAKE_INSTALL_PREFIX=$(CURDIR)/$(LLVM_CXX_PREFIX) \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DLIBCXX_ENABLE_SHARED=ON -DLIBCXXABI_ENABLE_SHARED=ON \
	  -DLIBUNWIND_ENABLE_SHARED=ON \
	  -DLIBCXX_ENABLE_STATIC=ON -DLIBCXXABI_ENABLE_STATIC=ON \
	  -DLIBUNWIND_ENABLE_STATIC=ON \
	  -DLIBCXX_ENABLE_STATIC_ABI_LIBRARY=OFF \
	  -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
	  -DLIBUNWIND_INCLUDE_TESTS=OFF \
	  -DLIBCXX_HAS_MUSL_LIBC=ON

$(LLVM_CXX_STAMP): $(LLVM_CXX_CFG) $(COMPILER_RT_CRTBEGIN) $(COMPILER_RT_CRTEND) $(COMPILER_RT_CRTBEGINS) $(COMPILER_RT_CRTENDS)
	cmake --build .build/llvm-cxx -j$$(nproc)
	cmake --install .build/llvm-cxx
	touch $(LLVM_CXX_STAMP)

# --- compiler-rt builtins (Clang toolchain M0) ------------------------
# libgcc.a's replacement, built from the pinned llvm-src by clang against
# the musl target triple. Builtins are freestanding, so the build needs no
# musl sysroot - the archive (plus clang's own resource headers) is what
# the musl-clang wrappers link. The standalone lib/builtins cmake project
# needs no runnable configure-test binaries (TRY_COMPILE -> static lib).
$(COMPILER_RT_CFG): .build/llvm-src/compiler-rt/lib/builtins/CMakeLists.txt
	rm -rf .build/compiler-rt
	cmake -G "Unix Makefiles" -S .build/llvm-src/compiler-rt/lib/builtins \
	  -B .build/compiler-rt \
	  -DCOMPILER_RT_DEFAULT_TARGET_TRIPLE=x86_64-unknown-linux-musl \
	  -DCMAKE_C_COMPILER=/usr/lib/llvm-19/bin/clang \
	  -DCMAKE_C_COMPILER_TARGET=x86_64-unknown-linux-musl \
	  -DCMAKE_ASM_COMPILER=/usr/lib/llvm-19/bin/clang \
	  -DCMAKE_ASM_COMPILER_TARGET=x86_64-unknown-linux-musl \
	  -DCMAKE_AR=/usr/lib/llvm-19/bin/llvm-ar \
	  -DCMAKE_RANLIB=/usr/lib/llvm-19/bin/llvm-ranlib \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_C_FLAGS="-ffreestanding -fno-builtin -fPIC" \
	  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY

$(COMPILER_RT_BUILTINS): $(COMPILER_RT_CFG)
	cmake --build .build/compiler-rt -j$$(nproc)


$(COMPILER_RT_CRTBEGINS): .build/llvm-src/compiler-rt/lib/builtins/crtbegin.c $(MUSL64_CC_STATIC)
	$(MUSL64_CC_STATIC) -fPIC -c $< -o $@

$(COMPILER_RT_CRTENDS): .build/llvm-src/compiler-rt/lib/builtins/crtend.c $(MUSL64_CC_STATIC)
	$(MUSL64_CC_STATIC) -fPIC -c $< -o $@

# --- Clang toolchain M0 proof (docs/llvm-clang-toolchain-plan.md M0) ---
# userland/tests/hello.c compiled twice by the clang wrappers - dynamic and
# static - and staged under System/Shared/tests (a lint carve-out tree,
# so the static ELF does not trip the System/Tools zero-allow gate).
M0CLANG_DIR = $(ROOTFS64)/System/Shared/tests

# --- Objective-C runtime (libobjc2) for the musl userland ---------------
# docs/design/objc-toolchain-plan.md P1. An EXPLICIT target, deliberately
# NOT in userland64's dependency chain: the sources need a fetch (network)
# and nothing ships the runtime yet, so it must not enter the default
# build. P3 is what stages it and wires the guest gate.
#
# Same contract as llvm-cxx above: no CMAKE_SYSROOT (the clang wrappers
# carry the musl/FSH link contract), and CMAKE_TRY_COMPILE_TARGET_TYPE=
# STATIC_LIBRARY so the configure probes don't try to LINK a musl binary.
# All four compilers are set because libobjc2 enables both OBJC and OBJCXX.
OBJC_SRC        = .build/libobjc2-src
OBJC_BUILD      = .build/objc-build
OBJC_PREFIX     = .build/objc-prefix
OBJC_STAMP      = $(OBJC_PREFIX)/.installed
OBJC_SOURCES    = .build/libobjc2-src/.pinned
ROBINMAP_SRC    = .build/robin-map
ROBINMAP_PREFIX = .build/robin-map-prefix
ROBINMAP_STAMP  = $(ROBINMAP_PREFIX)/.installed

$(OBJC_SOURCES): tools/fetch-libobjc2.sh
	./tools/fetch-libobjc2.sh
	touch $@

# robin-map is header-only. Installing our PINNED copy is what keeps
# libobjc2's `find_package(tsl-robin-map)` from falling back to
# FetchContent-ing an unpinned robin-map at configure time.
$(ROBINMAP_STAMP): $(OBJC_SOURCES) $(ROBINMAP_SRC)/CMakeLists.txt
	rm -rf .build/robin-map-build $(ROBINMAP_PREFIX)
	cmake -G "Unix Makefiles" -S $(ROBINMAP_SRC) -B .build/robin-map-build \
	  -DCMAKE_INSTALL_PREFIX=$(CURDIR)/$(ROBINMAP_PREFIX) \
	  -DCMAKE_BUILD_TYPE=Release
	cmake --install .build/robin-map-build
	touch $@

.PHONY: libobjc64
libobjc64: $(OBJC_STAMP)

$(OBJC_STAMP): $(OBJC_SOURCES) third_party/libobjc2-fnx.patch $(ROBINMAP_STAMP) $(MUSL64_LIBC) $(LLVM_CXX_STAMP)
	rm -rf $(OBJC_BUILD) $(OBJC_PREFIX)
	# FNX port patch (third_party/libobjc2-fnx.patch), applied idempotently:
	#  * LINKER_LANGUAGE C -> CXX. Upstream links the runtime with the C
	#    driver; a Debian/glibc `cc -shared` silently supplies crtbeginS.o
	#    (and so __dso_handle), but the FNX C wrapper deliberately does NOT
	#    add crt objects to a -shared link ("plain C needs none"), so the
	#    .so link died on `undefined reference to __dso_handle`. The runtime
	#    contains C++ (objcxx_eh.cc) and its EH interop must pair with OUR
	#    libc++abi, so the C++ driver is the correct linker here.
	#  * the STATIC target must see robin-map's include dirs (upstream only
	#    links tsl::robin_map into the shared target), or selector_table.cc
	#    fails with 'tsl/robin_set.h' file not found.
	@cd $(OBJC_SRC) && (git apply --reverse --check $(CURDIR)/third_party/libobjc2-fnx.patch 2>/dev/null \
		|| git apply $(CURDIR)/third_party/libobjc2-fnx.patch)
	cmake -G "Unix Makefiles" -S $(OBJC_SRC) -B $(OBJC_BUILD) \
	  -DCMAKE_C_COMPILER=$(CURDIR)/tools/musl-clang64.sh \
	  -DCMAKE_CXX_COMPILER=$(CURDIR)/tools/musl-clang++64.sh \
	  -DCMAKE_OBJC_COMPILER=$(CURDIR)/tools/musl-clang64.sh \
	  -DCMAKE_OBJCXX_COMPILER=$(CURDIR)/tools/musl-clang++64.sh \
	  -DCMAKE_INSTALL_PREFIX=$(CURDIR)/$(OBJC_PREFIX) \
	  -DCMAKE_BUILD_TYPE=Release \
	  -DCMAKE_PREFIX_PATH=$(CURDIR)/$(ROBINMAP_PREFIX) \
	  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
	  -DGNUSTEP_INSTALL_TYPE=NONE -DCMAKE_INSTALL_LIBDIR=lib \
	  -DTESTS=OFF -DOLDABI_COMPAT=OFF -DLLVM_OPTS=OFF \
	  -DBUILD_STATIC_LIBOBJC=ON \
	  -DCMAKE_C_FLAGS="-DGNUSTEP" -DCMAKE_OBJC_FLAGS="-DGNUSTEP"
	cmake --build $(OBJC_BUILD) -j$$(nproc)
	cmake --install $(OBJC_BUILD)
	touch $@
