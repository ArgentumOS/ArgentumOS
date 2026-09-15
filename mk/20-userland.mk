m0clang: userland64 $(COMPILER_RT_BUILTINS) tools/musl-clang64.sh tools/musl-clang64-static.sh
	$(MUSL64_CLANG) userland/tests/hello.c -o $(M0CLANG_DIR)/clang-hello-dl
	$(MUSL64_CLANG_STATIC) userland/tests/hello.c -o $(M0CLANG_DIR)/clang-hello-static
	@echo "m0clang: clang-built hellos staged under System/Shared/tests"

dash64: $(DASH64_BIN)
$(DASH64_BIN): $(MUSL64_LIBC) third_party/dash-fsh.patch
	cd third_party/dash && git apply $(CURDIR)/third_party/dash-fsh.patch && \
		./autogen.sh && \
		CC="$(MUSL64_CC)" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(CURDIR)/$(DASH64_BIN) && \
		git checkout -- .

toybox64: $(TOYBOX64_BIN)
$(TOYBOX64_BIN): $(MUSL64_LIBC) tools/mktoybox.sh $(MUSL64_CC) $(FNXLIB_CONFIG)
	TOYBOX_CC="$(MUSL64_CC)" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $(TOYBOX64_BIN)

# --- the static recovery set (docs/shared-libraries-plan.md §2.4/§3) ---
# Static-by-choice binaries that must run when /System/Libraries is
# corrupt or missing: a static dash (RECOVERY_PROGRAM, exec'd by the
# kernel) + a static toybox (the repair tools; its account applets need
# libconfig, hence the static libconfig.a). Both are built with
# MUSL64_CC_STATIC after the dynamic world (serial make order: dash64 /
# toybox64 above run first, so the in-tree third_party builds are not
# clobbered out from under the dynamic outputs).
RECOVERY64      = .build/recovery64
DASH64_RECOVERY = $(RECOVERY64)/dash-static
TOYBOX64_RECOVERY = $(RECOVERY64)/toybox-static

$(FNXLIB)/libconfig.a: $(FNXLIB_CONFIG) userland/libconfig.c userland/libconfig.h
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/libconfig.c -o $(FNXLIB)/libconfig.o
	ar rcs $@ $(FNXLIB)/libconfig.o

$(DASH64_RECOVERY): $(MUSL64_LIBC) third_party/dash-fsh.patch
	@mkdir -p $(RECOVERY64)
	cd third_party/dash && git apply $(CURDIR)/third_party/dash-fsh.patch && \
		./autogen.sh && \
		CC="$(MUSL64_CC_STATIC)" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(CURDIR)/$(DASH64_RECOVERY) && \
		git checkout -- .

$(TOYBOX64_RECOVERY): $(MUSL64_LIBC) tools/mktoybox.sh $(MUSL64_CC_STATIC) $(FNXLIB)/libconfig.a
	@mkdir -p $(RECOVERY64)
	# TOYBOX_STAGE points the static build at its own scratch root so the
	# recovery run cannot clobber .build/toybox-root (the dynamic staging
	# userland64 copies as System/Tools/toybox) with a static toybox.
	TOYBOX_CC="$(MUSL64_CC_STATIC)" TOYBOX_STAGE="$(CURDIR)/$(RECOVERY64)/toybox-stage" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $@

.PHONY: recovery64
recovery64: $(DASH64_RECOVERY) $(TOYBOX64_RECOVERY)
	@echo "recovery64: static recovery set built ($(DASH64_RECOVERY), $(TOYBOX64_RECOVERY))"


# --- Xfb: FNX's native X server (fork of Xvfb; the pristine upstream is
# not vendored - regenerate it on demand, see userland/xfb/README.md).
# xfb64 is phony and always delegates: the inner per-file rules own the
# incremental rebuild (a file-prerequisite here would go stale forever,
# since .build/x11/xfb/Xfb has no source deps at this level).
XFB_SRC = userland/xfb
XFB_OUT = .build/x11/xfb
XFB_BIN = $(XFB_OUT)/Xfb

.PHONY: xfb64
xfb64: $(FNXLIB_CONFIG)
	# Xfb links dynamic (M2): its third-party deps (pixman, xkbfile,
	# Xfont2, Xau) are shared .so in /System/Libraries; the server's own
	# archives stay in the binary. libsha1.a + the server archives are
	# the only static pieces left (single-consumer FNX code).
	$(MAKE) -C $(XFB_SRC) OUT="$(CURDIR)/$(XFB_OUT)" \
		CC="$(MUSL64_CC)" -j8


# ---------------------------------------------------------------------------
# userland64: stage the native x86_64 root in the FNX hierarchy
# (docs/fsh-proposal.md). The root contains exactly the five top-level
# entries: Applications/, Shared/, System/, Users/, Volumes/. Tools live in
# System/Tools, machine config in System/Configuration, device nodes come
# from devfs at /System/Devices (boot-time kernel mount), and the
# virtual-fs mount points (/System/Processes, devpts under Devices) exist
# as directories.
# ---------------------------------------------------------------------------
# toybox installs applets into PREFIX/{bin,sbin,usr/...} per toy flags;
# stage into a scratch root and merge every applet dir into System/Tools.
TOYBOX64_STAGE = .build/toybox-root
userland64: toolchain-gate $(MUSL64_LIBC) $(DASH64_BIN) $(TOYBOX64_BIN) $(LLVM_CXX_STAMP) $(LVGL64) $(XFB_BIN) $(FNXLIB_CONFIG) $(FNXLIB_ARGENTUM) $(DASH64_RECOVERY) $(TOYBOX64_RECOVERY)
	rm -rf $(ROOTFS64)
	@mkdir -p $(ROOTFS64)
	# third-party X11 + toolchain tests live under System/Shared
	@mkdir -p "$(ROOTFS64)/System/Shared/X11/bin" "$(ROOTFS64)/System/Shared/tests"
	# --- the FSH skeleton (spaced names verbatim, Q7). No root /tmp: the
	# FSH maps /tmp to /System/Temporary Files (staged below); fshlint
	# bans the /tmp string in System/Tools; fs_repair_tmpdir() recreates
	# /System/Temporary Files at mount if a kill-replay left it non-dir. ---
	# Application Support: behaviour material (scripts, app data) per the
	# config policy carve-out - the same domain key and scope tree as
	# Configuration/, a different payload kind (config-design.md §0).
	@mkdir -p "$(ROOTFS64)/System/Application Support" \
		"$(ROOTFS64)/Shared/Application Support" \
		"$(ROOTFS64)/System/User Template/Application Support" \
		"$(ROOTFS64)/System/Application Support/system.widgetzoo"
	@mkdir -p "$(ROOTFS64)/Applications" "$(ROOTFS64)/Volumes"
	@mkdir -p "$(ROOTFS64)/Shared/Configuration" "$(ROOTFS64)/Shared/Libraries" \
		"$(ROOTFS64)/Shared/Fonts" "$(ROOTFS64)/Shared/Images" \
		"$(ROOTFS64)/Shared/Sounds" "$(ROOTFS64)/Shared/Videos" \
		"$(ROOTFS64)/Shared/Documentation" "$(ROOTFS64)/Shared/Themes"
	@mkdir -p "$(ROOTFS64)/System/Tools" "$(ROOTFS64)/System/Libraries" \
		"$(ROOTFS64)/System/Configuration" "$(ROOTFS64)/System/Devices" \
		"$(ROOTFS64)/System/Devices/pts" "$(ROOTFS64)/System/Processes" \
		"$(ROOTFS64)/System/ESP" "$(ROOTFS64)/System/Documentation/HTML/FNX" \
		"$(ROOTFS64)/System/Documentation/PDF/FNX" \
		"$(ROOTFS64)/System/Source Code" "$(ROOTFS64)/System/Shared/Fonts" \
		"$(ROOTFS64)/System/Shared/Images/Icons" \
		"$(ROOTFS64)/System/Shared/Images/Wallpaper" \
		"$(ROOTFS64)/System/Shared/Sounds" "$(ROOTFS64)/System/Shared/Videos" \
		"$(ROOTFS64)/System/Shared/X11/xkb" \
		"$(ROOTFS64)/System/Temporary Files" \
		"$(ROOTFS64)/System/Variable Data/X11/xkb/compiled" \
		"$(ROOTFS64)/System/User Template/Configuration" \
		"$(ROOTFS64)/System/User Template/Applications" \
		"$(ROOTFS64)/System/User Template/Documents" \
		"$(ROOTFS64)/System/User Template/Desktop" \
		"$(ROOTFS64)/System/User Template/Music" \
		"$(ROOTFS64)/System/User Template/Pictures" \
		"$(ROOTFS64)/System/User Template/Videos" \
		"$(ROOTFS64)/System/User Template/Shared/Libraries" \
		"$(ROOTFS64)/System/User Template/Shared/Fonts" \
		"$(ROOTFS64)/System/User Template/Shared/Images" \
		"$(ROOTFS64)/System/User Template/Shared/Sounds" \
		"$(ROOTFS64)/System/User Template/Shared/Videos" \
		"$(ROOTFS64)/System/User Template/Shared/Documentation" \
		"$(ROOTFS64)/System/User Template/Temporary Files" \
		"$(ROOTFS64)/System/User Template/Variable Data"
	# --- tools (executables) ---
	# toybox is built + installed by mktoybox.sh (fresh config + the
	# libconfig link); userland64 only flattens the staged applet dirs.
	@for d in bin sbin usr/bin usr/sbin; do \
		if [ -d "$(TOYBOX64_STAGE)/$$d" ]; then \
			cp -a $(TOYBOX64_STAGE)/$$d/. "$(ROOTFS64)/System/Tools/"; \
		fi; \
	done
	# toybox install links applets with PREFIX-relative targets that break
	# once bin/sbin/usr/bin are flattened into one Tools dir: point every
	# symlink at the toybox binary sitting next to it.
	@cd "$(ROOTFS64)/System/Tools" && for l in *; do \
		if [ -L "$$l" ]; then ln -sfn toybox "$$l"; fi; \
	done
	# suid root: the kernel honors S_ISUID at exec, and toybox drops to
	# the real uid for every applet except the account tools
	# (TOYFLAG_STAYROOT/ROOTONLY) — that is what lets non-root `su`
	# authenticate and switch users (M4).
	@chmod 4755 "$(ROOTFS64)/System/Tools/toybox"
	@chmod 0755 "$(ROOTFS64)/System/Tools/config" \
		"$(ROOTFS64)/System/Tools/init" 2>/dev/null || true
	$(MUSL64_CC) userland/tools/init.c -o "$(ROOTFS64)/System/Tools/init"
	$(MUSL64_CXX) userland/tests/cpp_smoke.cpp -o "$(ROOTFS64)/System/Shared/tests/cpp_smoke"
	# argentum_hello: Argentum S0.1 acceptance — dynamic link against the
	# shared libargentum.so.1 (NEEDED libargentum.so.1 resolved from
	# /System/Libraries at exec; no static copy).
	$(MUSL64_CXX) -Iuserland -L$(CURDIR)/$(FNXLIB) \
		userland/tests/argentum_hello.cpp -largentum -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/argentum_hello"
	# argentum_demo: Argentum S0.2 acceptance — opens a window on Xfb and
	# blits a solid fill via core protocol (Window::fill/XPutImage).
	# Run from the shell with DISPLAY=:0 once Xfb is up.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/argentum_demo.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/argentum_demo"
	# units_probe: Argentum S1.1 acceptance — prints the session
	# px/pt factor (Application::pxPerPt). The S1.1 gate runs it with
	# system.display width_mm/height_mm unset (expect 4/3 fallback)
	# then after `config write -s system.display ...` (expect 8/3 =
	# 2x). Needs the X session like the demo.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/units_probe.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/units_probe"
	# theme_primitives: Argentum S1.2 acceptance — draws the chrome
	# shape set (solid/linear/radial/rounded-rect/1px lines) into a
	# BitmapImage via GraphicsContext and flushes it to the window.
	# The S1.2 gate screendumps Xfb and pixel-probes the fixed board.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/theme_primitives.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/theme_primitives"
	# theme_chrome: Argentum S1.3 acceptance — the themed frame+button
	# render at the fallback factor: loads the active Theme and draws
	# a chrome window frame + three state buttons with theme params.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/theme_chrome.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/theme_chrome"
	# interface_roundtrip: Weaver IB0 acceptance (docs/design/weaver-plan.md
	# §8) — the interface document survives its own emitter: every fixture is
	# loaded, emitted, reloaded and emitted again, and the two emissions must
	# be byte-identical, with content assertions on top because dropping
	# every field would satisfy idempotence on its own. Display-free on
	# purpose: no window, no input, no staged control is involved.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/interface_roundtrip.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/interface_roundtrip"
	# interface_v2: Weaver W0 acceptance (docs/design/weaver-gorm-model.md
	# §4) — the v2 object-graph document round-trips: proxies, non-view
	# objects, connections and classes. Display-free, like the IB0 probe.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/interface_v2.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/interface_v2"
	# interface_dispatch: Weaver W2 acceptance (docs/design/
	# weaver-gorm-model.md) — the load-time dispatcher: an action
	# connection runs the app's binding, an unbound selector is refused.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/interface_dispatch.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/interface_dispatch"
	# interface_build: Weaver IB1 acceptance (docs/design/weaver-plan.md §8)
	# — instantiate a document, find a control by identifier, and lay it out
	# from the frames and parent-relative masks the document records. No
	# window and no input: the evidence is the log and the resulting frames.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/interface_build.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/interface_build"
	# weaver_suppress: Weaver IB2 D12 probe — a non-hit-testable canvas
	# gives the press to the editor surface instead of firing the live
	# Button's action (synthetic dispatch, display-free like IB0/IB1).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/weaver_suppress.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/weaver_suppress"
	cp userland/tests/weaver_ib2.conf \
		"$(ROOTFS64)/System/Shared/tests/weaver_ib2.conf"
	# viewtree_a: Argentum S2.1a acceptance — the View core + tree +
	# composite display (bare window over a 2-level hierarchy; child
	# clipping). Same link recipe as the theme probes.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/viewtree_a.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/viewtree_a"
	# viewtree_b: Argentum S2.1b acceptance — a11y metadata + role
	# read-back (parents-before-children VTREE-B: lines, indented).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/viewtree_b.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/viewtree_b"
	# viewtree_c: Argentum S2.1c acceptance — responder chain +
	# hit-testing (XSendEvent clicks -> HIT <view>@<local pt> logs).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/viewtree_c.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/viewtree_c"
	# viewtree_d: Argentum S2.1d acceptance — springs/struts
	# relayout (XResizeWindow -> VTREE-D frame read-back lines).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/viewtree_d.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/viewtree_d"
	# text_gc: Argentum S2.2a acceptance — GraphicsContext::drawText
	# (text in the view-tree composite, clipped) + Window::drawText
	# parity (S22A-CAPTURED / S22A-M / ARGENTUM-TEXT: blitted).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/text_gc.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/text_gc"
	# widgets_b: Argentum S2.2b acceptance — Control (enabled +
	# action + state machine) + Label (theme text + a11y); logs
	# S22B-STATE/S22B-ACTION/S22B-A11Y/S22B-CAPTURED.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/widgets_b.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_b"
	# widgets_c: Argentum S2.2c acceptance — Button
	# (Push/Checkbox/Radio) + hover + minimal focus; logs
	# S22C-EVT/S22C-ACTION/S22C-CHECK/S22C-RADIO/S22C-A11Y/S22C-DRAW.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/widgets_c.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_c"
	# widgets_d: Argentum S2.2d acceptance — TextField edit engine on
	# the S2.2 interactive board (label + field + button); logs
	# S22D-EDIT (per edit) / S22D-A11Y / S22D-DRAW / S22D-READY.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/widgets_d.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_d"
	# widgets_e: Argentum S2.3a acceptance — drag + Slider + Stepper
	# + Menu model (S23A-FINAL / S23A-ACTION / S23A-A11Y / S23A-MENU).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/widgets_e.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_e"
	# widgets_f: Argentum S2.3b acceptance â SegmentedControl +
	# ProgressIndicator + LevelIndicator (S23B-FINAL / S23B-ACTION /
	# S23B-A11Y / S23B-DRAW).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/widgets_f.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_f"
	# widgets_g: Argentum S2.3c acceptance app — ImageView content modes
	# + PopUpButton (image painted via a BitmapImage+GC); driven by the
	# widgets_g_inj helper below (S23C-A11Y / S23C-DRAW / S23C-APP-READY).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/widgets_g.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_g"
	# widgets_g_inj: the S2.3c helper (second X connection; phases
	# S23C-A..D markers the gate screendumps between).
	$(MUSL64_CXX) -I$(X11PREFIX)/include -L$(X11PREFIX)/lib \
		userland/tests/widgets_g_inj.cpp -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/widgets_g_inj"
	# --- S5.2d: the reference apps ship as BUNDLES in /Applications ----
	# <DisplayName>.app/{manifest, bin/<Executable>, Resources/} per
	# docs/design/app-model.md §2: a flat directory identified by the
	# .app extension and the manifest.  The payload is a plain binary
	# inside the bundle and the dock execs it directly (unmediated —
	# bundle-launch-plan.md's launch helper is a later milestone), so
	# every payload lives at /Applications/<DisplayName>.app/bin/.
	# Widget Zoo is the S2.5 reference app (every S2.2+S2.3 control on
	# one live board): `make zoo` boots a session that runs this bundle.
	mkdir -p "$(ROOTFS64)/Applications/Widget Zoo.app/bin" \
		 "$(ROOTFS64)/Applications/Widget Zoo.app/Resources"
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/apps/widgetzoo/widget_zoo.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/Applications/Widget Zoo.app/bin/WidgetZoo"
	cp userland/apps/widgetzoo/manifest \
		"$(ROOTFS64)/Applications/Widget Zoo.app/manifest"
	# Calculator: the S5.2d reference app #2 — four-function arithmetic
	# from buttons or the keyboard, with its own menubar.
	mkdir -p "$(ROOTFS64)/Applications/Calculator.app/bin" \
		 "$(ROOTFS64)/Applications/Calculator.app/Resources"
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/apps/calculator/calculator.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/Applications/Calculator.app/bin/Calculator"
	cp userland/apps/calculator/manifest \
		"$(ROOTFS64)/Applications/Calculator.app/manifest"
	# Weaver: the interface editor (docs/design/weaver-plan.md) — opens a
	# document, builds it on a non-hit-testable canvas, and drives its own
	# state from scripted argv commands (IB2, plan §8a fallback).
	mkdir -p "$(ROOTFS64)/Applications/Weaver.app/bin" \
		 "$(ROOTFS64)/Applications/Weaver.app/Resources"
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/apps/weaver/weaver.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/Applications/Weaver.app/bin/Weaver"
	cp userland/apps/weaver/manifest \
		"$(ROOTFS64)/Applications/Weaver.app/manifest"
	# Wren: the IB6 sample app (weaver-plan §8, D10) — its interface ships
	# as a document in its own Resources/, loaded by the same loader and
	# bound by identifier (D1). Display-free: the gate runs it from the
	# shell and reads its outlet log.
	mkdir -p "$(ROOTFS64)/Applications/Wren.app/bin" \
		 "$(ROOTFS64)/Applications/Wren.app/Resources"
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/apps/wren/wren.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/Applications/Wren.app/bin/Wren"
	cp userland/apps/wren/manifest \
		"$(ROOTFS64)/Applications/Wren.app/manifest"
	cp userland/apps/wren/Interface.conf \
		"$(ROOTFS64)/Applications/Wren.app/Resources/Interface.conf"
	# Workspace: W0a — the desktop shell app that owns the surface (the
	# wallpaper). A bundle like any other, launched by the session at
	# login; the WM recognises it by the _ARGENTUM_DESKTOP marker and
	# never frames it. See docs/design/workspace-plan.md W0a.
	mkdir -p "$(ROOTFS64)/Applications/Workspace.app/bin" \
		 "$(ROOTFS64)/Applications/Workspace.app/Resources"
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/apps/workspace/workspace.cpp -largentum -lX11 -lconfig \
		-o "$(ROOTFS64)/Applications/Workspace.app/bin/Workspace"
	cp userland/apps/workspace/manifest \
		"$(ROOTFS64)/Applications/Workspace.app/manifest"
	# zoo_inj: redraw-storm driver for the zoo (Expose x10 from a second
	# X connection; redraw-cost regression counts text resolutions/draw).
	$(MUSL64_CXX) -I$(X11PREFIX)/include -L$(X11PREFIX)/lib \
		userland/tests/zoo_inj.cpp -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/zoo_inj"
	# structure_a: S2.4a Box acceptance — titled Column Box packs buttons
	# + a nested Row Box; logs BOX-A arranged frames for the gate.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_a.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_a"
	# structure_b: S2.4b ScrollView acceptance — banded doc in a ScrollView,
	# Down/Up scroll by 60 pt; logs SCROLL-B offsets for the gate.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_b.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_b"
	# scrollbar_a: S2.4b (external) ScrollBar acceptance — arrows, track
	# and scroller driven by a real USB pointer, the wheel via QMP;
	# logs SBAR offsets for the gate.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/scrollbar_a.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/scrollbar_a"
	# kestrel: the S4 window manager (uikit-plan §5) — System/Tools
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/kestrel/kestrel.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-lXcomposite -lXext -lXcursor \
		-o "$(ROOTFS64)/System/Tools/kestrel"
	# krel_a/b: S4.1a managed probes (mapped under Kestrel)
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/krel_a.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/krel_a"
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/krel_b.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/krel_b"
	# krel_slow: S4.3b probe (a window that dawdles before its first
	# paint, so the gate can sample "created but not yet drawn")
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/krel_slow.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/krel_slow"
	# oom_probe: S4.3d (eats memory until a page cannot be faulted in,
	# to prove the fault path reports it and sends SIGBUS)
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/oom_probe.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/oom_probe"
	# textview_c: TXT-c TextView scroll integration acceptance
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/textview_c.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/textview_c"
	# textview_b: TXT-b TextView document-editing acceptance
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/textview_b.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/textview_b"
	# textview_a: TXT-a TextView layout + read-only draw acceptance
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/textview_a.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/textview_a"
	# structure_h: S3.2 secure-field acceptance — bullets + Return commit
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_h.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_h"
	# structure_g: S3.1b keyboard-equivalents acceptance — arrows/Space by Tab
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_g.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_g"
	# structure_f: S3.1a focus-traversal acceptance — Tab/Shift-Tab
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_f.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_f"
	# structure_e: S2.4e TableView acceptance — data-source rows in a ScrollView
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_e.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_e"
	# structure_d: S2.4d TabView acceptance — tab clicks switch the page
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_d.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_d"
	# structure_c: S2.4c SplitView acceptance — divider drag resizes panes
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/structure_c.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -largentum -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/structure_c"
	# xclick: generic synthetic-click injector for the S2.4 gates
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xclick.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xclick"
	# xshm_m0: MIT-SHM M0 acceptance — Xlib client (the UIKit transport)
	# paints a window via a SysV segment + XShmPutImage (libXext); logs
	# SHMM0-DONE for the gate.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xshm_m0.c \
		-L$(X11PREFIX)/lib -lXext -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xshm_m0"
	# xshm_geo: the Xfb short-window SHM measurement (S4.2a open item).
	# XShmPutImage into a matrix of geometries - including the toolkit's
	# real one (window 1920x30 with a 25%-larger backing) - reads each
	# window back with XGetImage and prints a verdict per geometry; run
	# from the console shell with DISPLAY=:0 and read the SHMGEO lines.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xshm_geo.c \
		-L$(X11PREFIX)/lib -lXext -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xshm_geo"
	# xwinprobe: the shadow-vs-screen probe (XGetImage over a screen rect,
	# twice) — written for the menubar strip that never repaints; useful
	# for any "the draw landed but the screen never changed" question.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xwinprobe.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xwinprobe"
	# xcomp_probe: de-risking a compositing Kestrel — does Xfb's Composite
	# redirect + hand out a usable pixmap, and what does a screen-sized
	# frame cost. Linked against the xcb bindings deliberately: the Xlib
	# wrappers (libXcomposite/libXdamage/libXrender) are not built here.
	$(MUSL64_CC) -I$(X11PREFIX)/include \
		-I$(X11PREFIX)/include/pixman-1 \
		userland/tests/xcomp_probe.c \
		-L$(X11PREFIX)/lib -lX11 -lX11-xcb -lxcb -lxcb-composite \
		-lpixman-1 -lXrender \
		-o "$(ROOTFS64)/System/Shared/tests/xcomp_probe"
	# rogue_resize: security audit 2026-09 — the hostile-geometry probe
	# (any X client can resize another's window; the toolkit must clamp).
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/rogue_resize.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/rogue_resize"
	# xbtn: X11 mouse-leg regression client (window + pointer poll +
	# button print) — the S0.6 mouse gate drives QEMU monitor mouse at
	# it and expects "XBTN: button 1 press/release".
	$(MUSL64_CC) -I$(X11PREFIX)/include -I$(X11PREFIX)/include/X11 \
		-L$(X11PREFIX)/lib \
		userland/tests/xbtn.c -lX11 -o "$(ROOTFS64)/System/Shared/tests/xbtn"
	$(MUSL64_CXX) -I$(X11PREFIX)/include -I$(X11PREFIX)/include/freetype2 \
		-I$(X11PREFIX)/include/harfbuzz \
		-L$(X11PREFIX)/lib -L$(CURDIR)/$(FNXLIB) \
		userland/tests/text_pipeline.cpp \
		-lfontconfig -lharfbuzz -lfreetype -lconfig \
		-o "$(ROOTFS64)/System/Shared/tests/text_pipeline"
	$(MUSL64_CC) userland/tools/acl.c -o "$(ROOTFS64)/System/Tools/acl"
	$(MUSL64_CC) -Iinclude -Iuserland userland/tools/config.c -L$(CURDIR)/$(FNXLIB) \
		-lconfig -o "$(ROOTFS64)/System/Tools/config"
	$(MUSL64_CC) -Iinclude tools/shm_leak_test.c -o "$(ROOTFS64)/System/Tools/shm_leak_test"
	$(MUSL64_CC) -Iinclude tools/shm_cap_test.c -o "$(ROOTFS64)/System/Tools/shm_cap_test"
	$(MUSL64_CC) tools/config_m3_test.c -o "$(ROOTFS64)/System/Tools/config_m3_test"
	$(MUSL64_CC) userland/tools/pty_test.c -o "$(ROOTFS64)/System/Tools/pty_test"
	# Probes the test harness drives (tests/cases/*): the security battery from
	# the audit rounds, the AGFS metadata/stream probes and the mmap probe.
	# They are committed sources; nothing built them into the image before.
	$(MUSL64_CC) -Iinclude tools/sec_test.c -o "$(ROOTFS64)/System/Shared/tests/sec_test"
	$(MUSL64_CC) userland/tests/test_mmap.c -o "$(ROOTFS64)/System/Shared/tests/test_mmap"
	$(MUSL64_CC) userland/tests/agfsattr.c -o "$(ROOTFS64)/System/Shared/tests/agfsattr"
	$(MUSL64_CC) userland/tests/agfsdir.c -o "$(ROOTFS64)/System/Shared/tests/agfsdir"
	$(MUSL64_CC) userland/tests/agfsxattr.c -o "$(ROOTFS64)/System/Shared/tests/agfsxattr"
	$(MUSL64_CC) userland/tools/agfsquery.c -o "$(ROOTFS64)/System/Tools/agfsquery"
	$(MUSL64_CC) userland/tools/agfsqtest.c -o "$(ROOTFS64)/System/Tools/agfsqtest"
	$(MUSL64_CC) userland/tools/tone.c -o "$(ROOTFS64)/System/Tools/tone" -lm
	$(MUSL64_CC) userland/tools/fbdump.c -o "$(ROOTFS64)/System/Tools/fbdump"
	cp $(DASH64_BIN) "$(ROOTFS64)/System/Tools/sh"
	# --- the static recovery set (docs/shared-libraries-plan.md §2.4/§3):
	# insurance when /System/Libraries is corrupt or missing. The kernel
	# boots these (RECOVERY_PROGRAM) instead of init on a 'recovery'
	# param or a failed NEEDED-closure probe. Static dash + static
	# toybox; the /System/Recovery/bin applet links mirror the dynamic
	# /System/Tools set so repair commands resolve to the static toybox.
	cp $(DASH64_RECOVERY) "$(ROOTFS64)/System/Tools/recovery-sh"
	cp $(TOYBOX64_RECOVERY) "$(ROOTFS64)/System/Tools/recovery-toybox"
	@chmod 0755 "$(ROOTFS64)/System/Tools/recovery-sh" \
		"$(ROOTFS64)/System/Tools/recovery-toybox"
	@mkdir -p "$(ROOTFS64)/System/Recovery/bin"
	@for l in $(CURDIR)/$(ROOTFS64)/System/Tools/*; do \
		if [ -L "$$l" ] && [ "$$(readlink "$$l")" = toybox ]; then \
			ln -sfn /System/Tools/recovery-toybox \
				"$(ROOTFS64)/System/Recovery/bin/$$(basename "$$l")"; \
		fi; \
	done
	# third-party X11 lives under System/Shared/X11 (outside the
	# zero-allow System/Tools lint scope); System/Tools stays first-party
	# + ported toybox only.
	@mkdir -p "$(ROOTFS64)/System/Shared/X11/bin" "$(ROOTFS64)/System/Shared/tests"
	cp $(XFB_BIN) "$(ROOTFS64)/System/Shared/X11/bin/Xfb"
	cp .build/x11-prefix/bin/xkbcomp "$(ROOTFS64)/System/Shared/X11/bin/xkbcomp"
	# xkb data for the runtime XKB compile (Xfb's libxkbfile default is
	# System/Shared/X11/xkb). Host xkeyboard-config is the established
	# source (bundled data mismatches the server's xkbcomp).
	@if [ -d /usr/share/X11/xkb/rules ]; then \
		cp -r /usr/share/X11/xkb/. "$(ROOTFS64)/System/Shared/X11/xkb/"; \
	else \
		echo "WARNING: /usr/share/X11/xkb missing - Xfb keyboard init will fail"; \
	fi
	@cp userland/tests/test_toybox.sh "$(ROOTFS64)/System/Shared/tests/test_toybox.sh" 2>/dev/null || \
		{ mkdir -p "$(ROOTFS64)/System/Shared/tests" && \
		  cp userland/tests/test_toybox.sh "$(ROOTFS64)/System/Shared/tests/test_toybox.sh"; }
	@chmod +x "$(ROOTFS64)/System/Tools/sh" "$(ROOTFS64)/System/Tools/init"
	@mkdir -p "$(ROOTFS64)/System/Shared/scripts/dhcp" && \
		cp userland/scripts/dhcp_script.sh "$(ROOTFS64)/System/Shared/scripts/dhcp/default.script" 2>/dev/null || true
	# --- machine configuration (System/Configuration; Q9 accounts) ---
	# Identity, name resolution and machine identity are record domains
	# shipped in System scope (docs/system-config-files-plan.md M2/M5).
	# No legacy colon/line files exist (passwd, group, shells, hosts).
	@cp userland/configuration/system.passwd.conf "$(ROOTFS64)/System/Configuration/system.passwd.conf"
	@cp userland/configuration/system.group.conf "$(ROOTFS64)/System/Configuration/system.group.conf"
	@cp userland/configuration/system.shells.conf "$(ROOTFS64)/System/Configuration/system.shells.conf"
	@cp userland/configuration/system.hosts.conf "$(ROOTFS64)/System/Configuration/system.hosts.conf"
	@cp userland/configuration/system.network.conf "$(ROOTFS64)/System/Configuration/system.network.conf"
	@cp userland/configuration/system.mounts.conf "$(ROOTFS64)/System/Configuration/system.mounts.conf"
	# --- FSH fonts (text stack): OS fonts in /System/Shared/Fonts; the
	# fontconfig config is the libconfig domain system.fonts.conf (M0,
	# docs/design/fontconfig-config-plan.md) - no XML fonts.conf ships.
	# --- the interim cursor theme (userland/cursors): an Xcursor theme is
	# <dir>/<theme>/cursors/<name>, which is the layout libXcursor searches
	# under XCURSOR_PATH. Kestrel points XCURSOR_PATH at this and defines
	# the cursor on the root window.
	# Idempotent ON PURPOSE: `cp -a src dst` copies src INTO dst when dst
	# is already a directory, so a second make run nested a whole duplicate
	# theme at cursors/cursors/ (~11.7MB, 146 entries) and pushed the tree
	# past the 64MB image. Clear the destination first.
	@mkdir -p "$(ROOTFS64)/System/Shared/Icons/default"
	@rm -rf "$(ROOTFS64)/System/Shared/Icons/default/cursors"
	@cp -a userland/cursors \
		"$(ROOTFS64)/System/Shared/Icons/default/cursors"
	@mkdir -p "$(ROOTFS64)/System/Shared/Fonts" \
		"$(ROOTFS64)/System/Variable Data/fontconfig"
	@cp userland/fonts/DejaVuSans.ttf userland/fonts/DejaVuSans-Bold.ttf \
		"$(ROOTFS64)/System/Shared/Fonts/"
	@cp userland/configuration/system.fonts.conf \
		"$(ROOTFS64)/System/Configuration/system.fonts.conf"
	# --- shared libc (docs/shared-libraries-plan.md): stage the dynamic
	# linker + libc for the dynamic userland. The interpreter is a
	# hardlink of libc.so (same inode), matching musl's own install, so
	# the loader recognizes libc as itself.
	@cp $(MUSL64_PREFIX)/lib/libc.so "$(ROOTFS64)/System/Libraries/libc.so"
	@ln -f "$(ROOTFS64)/System/Libraries/libc.so" "$(ROOTFS64)/System/Libraries/ld-musl-x86_64.so.1"
	# --- shared X stack (docs/shared-libraries-plan.md §6, M2): the
	# versioned .so files of the X11 dependency prefix. musl's loader
	# resolves each NEEDED soname (libX11.so.6, libxcb.so.1, ...) as an
	# exact filename against the baked /System/Libraries search path; the
	# glob carries the soname symlink + the versioned real file (the bare
	# dev symlink libX11.so is link-time only and skipped). Only the libs
	# today's consumers NEED are staged. libX11-xcb + libxcb-composite are
	# now staged because something DOES link them: xcomp_probe, which
	# de-risks a compositing Kestrel. libXrender is vendored too (the
	# RENDER client the compositor blends through); libXcomposite and
	# libXdamage are still not - the xcb bindings cover Composite.
	@if [ ! -d "$(X11PREFIX)/lib" ]; then \
		echo "X11 prefix missing - run tools/x11-shared-build.sh first"; \
		exit 1; \
	fi
	@for l in libX11.so libxcb.so libXau.so libXdmcp.so libxkbfile.so \
		libpixman-1.so libXfont2.so libfontenc.so libz.so libXext.so \
		libX11-xcb.so libxcb-composite.so libXrender.so \
		libXcursor.so libXfixes.so libXcomposite.so; do \
		cp -a $(X11PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# FNX's own shared libconfig (first-party, .build/fnxlib): the
	# config tool, toybox account tools and Xfb's configargs all NEEDED it.
	@cp $(FNXLIB_CONFIG) "$(ROOTFS64)/System/Libraries/libconfig.so.1"
	# FNX's C++ GUI toolkit (first-party): libargentum.so.1 staged under
	# the same rule — argentum_hello (S0.1) NEEDs it at runtime.
	@cp $(FNXLIB_ARGENTUM) "$(ROOTFS64)/System/Libraries/libargentum.so.1"
	# --- shared C++ stack (dynamic-C++): the versioned libc++/libc++abi/
	# libunwind .so files from the llvm-cxx prefix (built shared since the
	# dynamic-C++ milestone). Same staging rule as the X stack: the glob
	# carries the soname symlink + the versioned real file; the bare dev
	# symlink is link-time only and skipped. cpp_smoke (and libargentum
	# later) NEED these sonames at runtime.
	@if [ ! -d "$(LLVM_CXX_PREFIX)/lib" ]; then \
		echo "llvm-cxx prefix missing - run make llvm-cxx first"; \
		exit 1; \
	fi
	@for l in libc++.so libc++abi.so libunwind.so; do \
		cp -a $(LLVM_CXX_PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# --- shared text stack (docs/design/argentum-uikit-plan.md): fontconfig +
	# HarfBuzz + FreeType + the libpng/expat leaves, built SHARED into the
	# X prefix. Only what a consumer NEEDs today is staged (libharfbuzz-
	# subset/gobject are left out until something links them); the glob
	# carries soname + real file (libpng16.so.* covers libpng16.so.16,
	# not the bare libpng.so dev link).
	@if [ ! -d "$(X11PREFIX)/lib" ]; then \
		echo "X11 prefix missing - run tools/x11-shared-build.sh first"; \
		exit 1; \
	fi
	@for l in libpng16.so libexpat.so libfreetype.so libfontconfig.so \
		libharfbuzz.so; do \
		cp -a $(X11PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# hello_dl: the dynamic-linker smoke test. Staged under
	# System/Shared/tests - System/Tools is dynamic too since M1, but the
	# linter carve-out keeps this one out of the zero-allow scope.
	@mkdir -p "$(ROOTFS64)/System/Shared/tests"
	$(MUSL64_CC) userland/tests/hello_dl.c -o "$(ROOTFS64)/System/Shared/tests/hello_dl"
	# Overridable first-party defaults ship in Shared (plan §5.0); a
	# System copy overrides them (Xfb reads via resolved libconfig reads).
	@cp userland/configuration/system.xfb.conf "$(ROOTFS64)/Shared/Configuration/system.xfb.conf"
	# Argentum session defaults (S0.5, domain system.argentum): same
	# Shared-scope convention; the S0.5 acceptance overrides via
	# `config write -s system.argentum ...` (System wins on read).
	@cp userland/configuration/system.argentum.conf \
		"$(ROOTFS64)/Shared/Configuration/system.argentum.conf"
	@cp userland/configuration/system.workspace.conf \
		"$(ROOTFS64)/Shared/Configuration/system.workspace.conf"
	@cp userland/configuration/system.kestrel.conf \
		"$(ROOTFS64)/Shared/Configuration/system.kestrel.conf"
	# Argentum display physical size (S1.1, domain system.display): the
	# FNX-owned panel's real mm; 0 = unknown -> 96 dpi (4/3 px/pt)
	# fallback. The S1.1/S1.4 gates override via `config write -s
	# system.display display.width_mm <mm> display.height_mm <mm>`.
	@cp userland/configuration/system.display.conf \
		"$(ROOTFS64)/Shared/Configuration/system.display.conf"
	# Argentum active theme (S1.3, domain system.theme): names the
	# theme file under /Shared/Themes. Same Shared-scope convention.
	@cp userland/configuration/system.theme.conf \
		"$(ROOTFS64)/Shared/Configuration/system.theme.conf"
	# Argentum theme files (S1.3, plan §3 Themes): plain .conf data
	# under /Shared/Themes/<name>.conf, read raw via config_read_file
	# (NOT domains — they live outside the Configuration/ scope dirs).
	@cp userland/configuration/themes/Argentum.conf \
		"$(ROOTFS64)/Shared/Themes/Argentum.conf"
	# --- the Admin home: the User Template, copied (Q9) ---
	rm -rf "$(ROOTFS64)/Users"
	@mkdir -p "$(ROOTFS64)/Users"
	cp -a "$(ROOTFS64)/System/User Template" "$(ROOTFS64)/Users/Admin"
	@echo "userland64: native x86_64 rootfs staged in $(ROOTFS64)"

# Build the native x86_64 ext2 root filesystem image (.build/root.img) from
# the ELF64 userland tree, so the kernel can boot /System/Tools/init straight
# off hdb (no initrd). Attached as a second IDE disk (hdb), it becomes the
# boot root via the kernel cmdline 'root=/dev/hdb rootfstype=ext2'.
