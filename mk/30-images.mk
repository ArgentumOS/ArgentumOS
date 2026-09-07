rootdisk64: userland64
	python3 tools/mkext2.py $(ROOTFS64) .build/root.img 8
	@echo "rootdisk64: .build/root.img ready (ext2, 8MB, native x86_64 userland)"

# AGFS root image (OpenBFS M4f): the same userland tree packed into a BeOS
# AGFS image by tools/mkagfs.py (multi-node dir trees, indirect +
# double-indirect streams, symlinks). AGFS is the DEFAULT root device
# (make run / run-uefi); 64MB leaves headroom for the X11 userland (Xfb
# is a ~16MB static binary).
rootagfs: userland64 m0clang
	python3 tools/mkagfs.py $(ROOTFS64) .build/rootagfs.img 64
	python3 tools/agfscheck.py .build/rootagfs.img $(ROOTFS64)
	@echo "rootagfs: .build/rootagfs.img ready (AGFS, 64MB, native x86_64 userland)"

ovmf: .build/ovmf/OVMF.fd

.build/ovmf/OVMF.fd:
	./tools/fetch-ovmf.sh
