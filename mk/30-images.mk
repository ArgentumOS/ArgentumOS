rootdisk64: userland64
	python3 tools/mkext2.py $(ROOTFS64) .build/root.img 8
	@echo "rootdisk64: .build/root.img ready (ext2, 8MB, native x86_64 userland)"

# AGFS root image (OpenBFS M4f): the same userland tree packed into a BeOS
# AGFS image by tools/mkagfs.py (multi-node dir trees, indirect +
# double-indirect streams, symlinks). AGFS is the DEFAULT root device
# (make run / run-uefi); 64MB leaves headroom for the X11 userland (Xfb
# is a ~16MB static binary).
SESSION ?= xfb

# ONE WRITER FOR session.conf, used by BOTH images below (the shipped one and the tester's), so the
# two cannot drift apart. $(1) is the `desktop` value.
#
# It MUST be a PLIST like every other domain: the legacy `desktop = "x"` one-liner was never parsed
# (config_read_file returned 3 and init silently fell back to its XFB default - which is why the
# shipped desktop looked like it came from this file when it did not, and why SESSION had no effect).
define fn_write_session_conf
printf '<?xml version="1.0" encoding="UTF-8"?>\n<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">\n<plist version="1.0">\n<dict>\n\t<key>desktop</key>\n\t<string>%s</string>\n</dict>\n</plist>\n' "$(1)" > $(ROOTFS64)/System/Configuration/session.conf
endef

rootagfs: userland64 m0clang
	# The session boots Xfb + a console shell and nothing else: the class
	# layer and its apps were removed in the 2026-09 UIKit restart, and the
	# toolkit itself is parked (docs/design/argentum-uikit-plan.md, DEFERRED).
	# `make run-xfb` still reaches the demo desktop by name.
	# (session.conf is written through fn_write_session_conf above, which carries the plist-was-
	# never-parsed history. THE TESTER'S IMAGE - `make testimg`, in mk/50-tests.mk - USES THE SAME
	# WRITER and puts the shipped value back when it is done, so the two cannot drift.)
	@$(call fn_write_session_conf,$(SESSION))
	# 64MB stopped being enough when the tree reached ~60MB: the session
	# then failed to start (WORKSPACE "init failed", KESTREL "no display")
	# with an image that mkagfs and agfscheck both called good - the volume
	# was too full to write the LARGEST streams correctly, and Xfb (8.4MB
	# static) is the largest thing in it. A filesystem needs room beyond its
	# payload for indirect blocks, not just for data.
	#
	# RAISED AGAIN TO 128MB (U5b, 2026-09), for headroom rather than for a
	# known break: the UIKit tree plus its generated documentation put the
	# volume at 64284 of 65536 blocks — 98%, where the failure above stopped
	# being theoretical. A font failure during that work looked exactly like
	# the silent-truncation class and was NOT: "FreeType refuses a
	# byte-perfect DejaVuSans.ttf, ft=2" survived this 128MB rebuild, and the
	# cause was elsewhere entirely — the GUEST lets one file be open only
	# twice, and the toolkit opened a font face per SIZE, so its second size
	# was a third open and FreeType reports a failed READ as a bad format
	# (fixed by keeping one face per style; the probe that proved it is
	# userland/tests/font_twice.cpp). Raising the size is still right on its
	# own terms, and the way to watch it is mkagfs's own "blocks: N
	# referenced" against the volume's block count.
	python3 tools/mkagfs.py $(ROOTFS64) .build/rootagfs.img 128
	python3 tools/agfscheck.py .build/rootagfs.img $(ROOTFS64)
	@echo "rootagfs: .build/rootagfs.img ready (AGFS, 128MB)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh
