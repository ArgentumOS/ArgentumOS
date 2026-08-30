# fnx/fs/bfs/Makefile
#
# Copyright 2024, the FNX project.
# Distributed under the terms of the Fiwix License.
#

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

OBJS = super.o inode.o balloc.o btree.o dir.o file.o namei.o symlink.o xattr.o

all:	$(OBJS)

clean:
	rm -f *.o
