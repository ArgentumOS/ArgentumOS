# fnx/fs/bfs/Makefile
#
# Copyright 2024, the FNX project.
# Distributed under the terms of the Fiwix License.
#

.c.o:
	$(CC) $(CFLAGS) -c -o $@ $<

OBJS = super.o inode.o btree.o dir.o

all:	$(OBJS)

clean:
	rm -f *.o
