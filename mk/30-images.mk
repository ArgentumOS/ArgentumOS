rootdisk64: userland64
	python3 tools/mkext2.py $(ROOTFS64) .build/root.img 8
	@echo "rootdisk64: .build/root.img ready (ext2, 8MB, native x86_64 userland)"

# AGFS root image (OpenBFS M4f): the same userland tree packed into a BeOS
# AGFS image by tools/mkagfs.py (multi-node dir trees, indirect +
# double-indirect streams, symlinks). AGFS is the DEFAULT root device
# (make run / run-uefi); 64MB leaves headroom for the X11 userland (Xfb
# is a ~16MB static binary).
rootagfs: userland64 m0clang
	# S5.1: the standard image IS the Argentum desktop — init reads this
	# and runs Xfb + Kestrel as the session. The variant images
	# (xfbdesk/uitest/zoo/kestrel-root) overwrite it in their own staging
	# dir, so the demo desktop stays reachable by name (`make run-xfb`).
	# the UIKit restart (2026-09): the class layer and its apps are gone,
	# so the session boots Xfb + a console shell and nothing else.
	printf 'desktop = "xfb"\n' > $(ROOTFS64)/System/Configuration/session.conf
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
	# (fixed in userland/argentum/text.cpp; the probe that proved it is
	# userland/tests/font_twice.cpp). Raising the size is still right on its
	# own terms, and the way to watch it is mkagfs's own "blocks: N
	# referenced" against the volume's block count.
	python3 tools/mkagfs.py $(ROOTFS64) .build/rootagfs.img 128
	python3 tools/agfscheck.py .build/rootagfs.img $(ROOTFS64)
	@echo "rootagfs: .build/rootagfs.img ready (AGFS, 128MB)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh
