rootdisk64: userland64
	python3 tools/mkext2.py $(ROOTFS64) .build/root.img 8
	@echo "rootdisk64: .build/root.img ready (ext2, 8MB, native x86_64 userland)"

# XBFS root image (OpenBFS M4f): the same userland tree packed into a BeOS
# XBFS image by tools/mkxbfs.py (multi-node dir trees, indirect +
# double-indirect streams, symlinks). XBFS is the DEFAULT root device
# (make run / run-uefi); 64MB leaves headroom for the X11 userland (Xfb
# is a ~16MB static binary).
rootxbfs: userland64 m0clang
	python3 tools/mkxbfs.py $(ROOTFS64) .build/rootxbfs.img 64
	python3 tools/xbfscheck.py .build/rootxbfs.img $(ROOTFS64)
	@echo "rootxbfs: .build/rootxbfs.img ready (XBFS, 64MB, native x86_64 userland)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh
