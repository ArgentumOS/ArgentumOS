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
	printf 'desktop = "kestrel"\n' > $(ROOTFS64)/System/Configuration/session.conf
	python3 tools/mkagfs.py $(ROOTFS64) .build/rootagfs.img 64
	python3 tools/agfscheck.py .build/rootagfs.img $(ROOTFS64)
	@echo "rootagfs: .build/rootagfs.img ready (AGFS, 64MB, the Argentum desktop)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh
