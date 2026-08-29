# fnx/fs/devfs/Makefile
#
# Copyright 2026, Kyle J Cardoza. All rights reserved.
# Distributed under the terms of the Fiwix License.
#

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

OBJS = super.o inode.o namei.o dir.o nodes.o symlink.o

all:	$(OBJS)

clean:
	rm -f *.o
