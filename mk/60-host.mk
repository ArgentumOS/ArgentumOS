# The Argentum UIKit and the Widget Zoo built for the HOST (mk/60-host.mk).
#
# Why this exists: a UIKit change is usually a layout, a control, a menu or a
# piece of text — and to look at it the guest route costs a full image rebuild
# and a QEMU boot. The toolkit is plain C++ over Xlib (no syscalls, no /dev,
# no FSH paths; displayOpen() already honours $DISPLAY) and links only X11,
# pixman, fontconfig, HarfBuzz and FreeType — all of which the host has. So the
# SAME sources build and run here, in seconds.
#
# WHAT A HOST RUN IS *NOT*. It never touches the kernel, Xfb, the framebuffer,
# /dev input, USB HID, the FSH config domains, or the session's px/pt factor.
# A host run says "the toolkit does this"; it cannot say "the OS does this".
# The guest gates (make test / test-all) stay the verification of record.
#
#   make hostlib      build libconfig + libargentum for the host
#   make hostapps     build the toolkit test binaries and WidgetZoo
#   make host-tests   run every toolkit test binary (self-checking: they exit
#                     non-zero on failure), no display needed for most of them
#   make run-host     run the Widget Zoo on $DISPLAY (HOST_RUN_SECONDS overrides
#                     the board's own 90)
#
# The compiler is the SAME clang family the system build uses, only aimed at the
# host's glibc/libstdc++ instead of musl/libc++. That is deliberate: the one
# place gcc could sneak back into this tree is the one place nobody polices
# (tools/toolchain-gate.py scans tools/ and the Makefile, not mk/). Override
# with HOST_CXX=... if you need to.

HOST_LLVM     ?= /usr/lib/llvm-19/bin
HOST_CXX      ?= $(HOST_LLVM)/clang++
HOST_CC       ?= $(HOST_LLVM)/clang
HOST_BUILD    = .build/host
HOST_LIBDIR   = $(HOST_BUILD)/lib
HOST_APPDIR   = $(HOST_BUILD)/bin

HOST_PKGS     = x11 xext pixman-1 fontconfig harfbuzz freetype2
HOST_CFLAGS   = $(shell pkg-config --cflags $(HOST_PKGS))
HOST_LDLIBS   = $(shell pkg-config --libs $(HOST_PKGS))
HOST_CXXFLAGS = -std=c++17 -O1 -g -fPIC -Iuserland $(HOST_CFLAGS)
# the apps must find the .so they were linked against, wherever they run
HOST_RPATH    = -Wl,-rpath,$(CURDIR)/$(HOST_LIBDIR)
HOST_LINK     = $(HOST_CXX) $(HOST_CXXFLAGS) $(HOST_RPATH) -L$(HOST_LIBDIR)

HOST_LIBCONFIG   = $(HOST_LIBDIR)/libconfig.so
HOST_LIBARGENTUM = $(HOST_LIBDIR)/libargentum.so

# WidgetZoo's own default, so `make run-host` behaves like the gate runs do.
HOST_RUN_SECONDS ?= 90

# The toolkit links libconfig even though it does not call it (the guest build
# does the same), so the host library has to exist to satisfy -lconfig.
$(HOST_LIBCONFIG): userland/libconfig.c userland/libconfig.h
	@mkdir -p $(HOST_LIBDIR)
	$(HOST_CC) -fPIC -shared -Iuserland -o $@ userland/libconfig.c

$(HOST_LIBARGENTUM): $(ARGENTUM_SRCS) userland/argentum/argentum.h $(HOST_LIBCONFIG)
	@mkdir -p $(HOST_LIBDIR)
	$(HOST_CXX) $(HOST_CXXFLAGS) -shared -o $@ $(ARGENTUM_SRCS) \
		-L$(HOST_LIBDIR) $(HOST_LDLIBS) -lconfig

# The toolkit's own test programs. Seven are pure logic and need no display;
# control_click and window_draw open one, as the app does.
HOST_TEST_NAMES = cell_basic control_click view_layout layout_solve kvc_basic \
	notification_basic viewcontroller_basic window_draw text_stack stack_view \
	scroll_view collection_view tab_view
HOST_TESTS = $(addprefix $(HOST_APPDIR)/,$(HOST_TEST_NAMES))
HOST_APP   = $(HOST_APPDIR)/WidgetZoo

$(HOST_APPDIR)/%: userland/tests/%.cpp $(HOST_LIBARGENTUM)
	@mkdir -p $(HOST_APPDIR)
	$(HOST_LINK) -o $@ $< -largentum -lconfig $(HOST_LDLIBS)

$(HOST_APP): userland/apps/widgetzoo.cpp $(HOST_LIBARGENTUM)
	@mkdir -p $(HOST_APPDIR)
	$(HOST_LINK) -o $@ $< -largentum -lconfig $(HOST_LDLIBS)

.PHONY: hostlib hostapps host-tests run-host

hostlib: $(HOST_LIBARGENTUM)
	@echo "hostlib: $(HOST_LIBDIR)/libargentum.so ready"

hostapps: $(HOST_APP) $(HOST_TESTS)
	@echo "hostapps: $(HOST_APP) and $(words $(HOST_TESTS)) test binaries ready"

# Each test prints its own verdict and exits 1 on failure, so this is a wrapper
# and not a harness - no display, no QEMU, no assertions duplicated here.
host-tests: $(HOST_TESTS)
	@fail=0; \
	for t in $(HOST_TESTS); do \
		if out=$$("$$t" 2>&1); then \
			echo "PASS $$(basename $$t)"; \
		else \
			fail=$$((fail + 1)); \
			echo "FAIL $$(basename $$t)"; \
			echo "$$out" | sed 's/^/    /'; \
		fi; \
	done; \
	if [ $$fail -ne 0 ]; then \
		echo "host-tests: $$fail of $(words $(HOST_TESTS)) failed"; \
		exit 1; \
	fi; \
	echo "host-tests: $(words $(HOST_TESTS))/$(words $(HOST_TESTS)) passed"

run-host: $(HOST_APP)
	@if [ -z "$$DISPLAY" ]; then \
		echo "run-host: no DISPLAY set - the toolkit needs an X server."; \
		echo "          (start one, or: DISPLAY=:0 make run-host)"; \
		exit 1; \
	fi
	@if ! xdpyinfo -display "$$DISPLAY" >/dev/null 2>&1; then \
		echo "run-host: DISPLAY=$$DISPLAY does not answer."; \
		exit 1; \
	fi
	@echo "run-host: $(HOST_APP) on DISPLAY=$$DISPLAY for $(HOST_RUN_SECONDS)s (the board's own log is below)"
	$(HOST_APP) $(HOST_RUN_SECONDS)
