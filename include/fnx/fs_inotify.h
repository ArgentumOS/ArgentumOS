/*
 * fnx/include/fnx/fs_inotify.h
 *
 * FNX: inotify (253/254/255/291) - kernel-side structures.
 *
 * The user ABI (musl <sys/inotify.h>) is:
 *   struct inotify_event { int wd; __u32 mask; __u32 cookie;
 *                          __u32 len; char name[]; }
 * Events are read() from the inotify fd, aligned to 4 bytes.
 */

#ifndef _FNX_FS_INOTIFY_H
#define _FNX_FS_INOTIFY_H

#include <fnx/types.h>
#include <fnx/fs.h>

struct superblock;

/* event masks (musl values) */
#define IN_ACCESS	0x00000001
#define IN_MODIFY	0x00000002
#define IN_ATTRIB	0x00000004
#define IN_CLOSE_WRITE	0x00000008
#define IN_CLOSE_NOWRITE	0x00000010
#define IN_OPEN		0x00000020
#define IN_MOVED_FROM	0x00000040
#define IN_MOVED_TO	0x00000080
#define IN_CREATE	0x00000100
#define IN_DELETE	0x00000200
#define IN_DELETE_SELF	0x00000400
#define IN_MOVE_SELF	0x00000800
#define IN_UNMOUNT	0x00002000
#define IN_Q_OVERFLOW	0x00004000
#define IN_IGNORED	0x00008000
#define IN_CLOSE	(IN_CLOSE_WRITE | IN_CLOSE_NOWRITE)
#define IN_MOVE		(IN_MOVED_FROM | IN_MOVED_TO)
#define IN_ALL_EVENTS	(IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_CLOSE_WRITE \
			| IN_CLOSE_NOWRITE | IN_OPEN | IN_MOVED_FROM \
			| IN_MOVED_TO | IN_CREATE | IN_DELETE \
			| IN_DELETE_SELF | IN_MOVE_SELF)

/* inotify_init1 flags */
#define IN_NONBLOCK	0x00000800
#define IN_CLOEXEC	0x02000000

/* flag bits OR'd into the event mask */
#define IN_ISDIR	0x40000000
#define IN_ONESHOT	0x80000000

/* kernel-only: the per-instance data hung off the inode union */
struct inotify_instance;

struct inotifyfs_inode {
	struct inotify_instance *instance;	/* NULL for the superblock */
};

/* one queued event (name[] is variable-length) */
struct inotify_event {
	int wd;
	__u32 mask;
	__u32 cookie;
	__u32 len;		/* length of name[], including NUL */
	char name[];		/* optional filename, NUL-terminated */
};

/* internal queue node: a queued event plus its next link.
 * next MUST come first: struct inotify_event ends with the flexible
 * name[], so ev.name[] extends into the allocation beyond the struct;
 * if next came after ev, writing the name would clobber it. */
struct inotify_qevent {
	struct inotify_qevent *next;
	struct inotify_event ev;
};

extern struct fs_operations inotifyfs_fsop __attribute__((visibility("hidden")));
extern int inotifyfs_ialloc(struct inode *, int);
extern void inotifyfs_ifree(struct inode *);
extern int inotifyfs_read_superblock(__dev_t, struct superblock *);
extern int inotifyfs_init(void);
extern void inotify_queue(struct inode *, __u32, __u32, const char *);

#endif /* _FNX_FS_INOTIFY_H */
