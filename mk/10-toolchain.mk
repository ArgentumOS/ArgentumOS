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
	cmake -G "Unix Makefiles" -S $(LLVM_CXX_SRC)/runtimes -B .build/llvm-cxx \
	  -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" \
	  -DCMAKE_C_COMPILER=$(MUSL64_CC_STATIC) \
	  -DCMAKE_CXX_COMPILER=$(CURDIR)/tools/musl-clang++64.sh \
	  -DCMAKE_C_FLAGS="" \
	  -DCMAKE_CXX_FLAGS="" \
	  -DCMAKE_EXE_LINKER_FLAGS="" \
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

# --- Clang toolchain M0 proof (docs/llvm-clang-toolchain-plan.md M0) ---
# userland/tests/hello.c compiled twice by the clang wrappers - dynamic and
# static - and staged under System/Shared/tests (a lint carve-out tree,
# so the static ELF does not trip the System/Tools zero-allow gate).
M0CLANG_DIR = $(ROOTFS64)/System/Shared/tests
