/*
 * fnx/kernel/syscalls/inotify.c
 *
 * FNX: inotify_init(253)/inotify_init1(291)/inotify_add_watch(254)/
 * inotify_rm_watch(255), plus the inotifyfs file ops (read/select/
 * ioctl/close) and the VFS event hook inotify_queue().
 *
 * Model: each inotify instance is an inotifyfs inode; watches hang off
 * the instance and reference a target inode. An event on inode X fires
 * every watch whose watch->inode == X (directory watches fire on child
 * events with the child name). Events are queued per instance and read()
 * returns them as struct inotify_event (4-byte aligned, name[] appended).
 */

#include <fnx/fs.h>
#include <fnx/filesystems.h>
#include <fnx/fs_inotify.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/errno.h>
#include <fnx/mm.h>
#include <fnx/string.h>
#include <fnx/stat.h>
#include <fnx/fcntl.h>
#include <fnx/ioctl.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

struct inotify_watch {
	int wd;
	__u32 mask;
	struct inode *inode;
	struct inotify_instance *inst;	/* owning instance */
	struct inotify_watch *next;
};

/* FNX: global watch list so inotify_queue() scans only watches, not
 * the whole inode table (which grows with every file created) - the
 * naive per-instance scan made every write/close O(inode_table). */
static struct inotify_watch *inotify_watch_head;

struct inotify_instance {
	int wd_counter;
	/* event queue: singly-linked, count + byte total for FIONREAD */
	struct inotify_qevent *q_head;
	struct inotify_qevent *q_tail;
	int q_count;
	int q_bytes;
};

/* ABI size of one event: struct inotify_event (16) + name (incl NUL).
 * The user parses events packed as sizeof + ev->len each, so the copy
 * size must be exactly that - no padding between events. */
static __size_t event_size(struct inotify_event *ev)
{
	return sizeof(struct inotify_event) + ev->len;
}

/* ---- inotifyfs file operations ---- */

int inotifyfs_close(struct inode *i, struct fd *f)
{
	struct inotify_instance *inst = i->u.inotify.instance;
	struct inotify_watch *w, *wn, *prev;
	struct inotify_qevent *e, *en;

	if(!inst) {
		return 0;
	}
	/* remove every watch owned by this instance from the global list */
	w = inotify_watch_head;
	while(w) {
		wn = w->next;
		if(w->inst == inst) {
			if(inotify_watch_head == w) {
				inotify_watch_head = w->next;
			} else {
				prev = inotify_watch_head;
				while(prev && prev->next != w) {
					prev = prev->next;
				}
				if(prev) {
					prev->next = w->next;
				}
			}
			iput(w->inode);
			kfree((addr_t)w);
		}
		w = wn;
	}
	e = inst->q_head;
	while(e) {
		en = e->next;
		kfree((addr_t)e);
		e = en;
	}
	kfree((addr_t)inst);
	i->u.inotify.instance = NULL;
	return 0;
}

int inotifyfs_read(struct inode *i, struct fd *f, char *buffer, __size_t count)
{
	struct inotify_instance *inst = i->u.inotify.instance;
	struct inotify_qevent *qe;
	__size_t sz, total;

	if(!inst) {
		return -EINVAL;
	}
	total = 0;
	while(inst->q_head && count >= sizeof(struct inotify_event)) {
		qe = inst->q_head;
		sz = event_size(&qe->ev);
		if(sz > count) {
			break;
		}
		memcpy_b(buffer, &qe->ev, sz);
		/* unlink from the queue */
		inst->q_head = qe->next;
		if(!inst->q_head) {
			inst->q_tail = NULL;
		}
		inst->q_count--;
		inst->q_bytes -= (int)sz;
		kfree((addr_t)qe);
		buffer += sz;
		count -= sz;
		total += sz;
	}
	return total ? (int)total : 0;
}

int inotifyfs_select(struct inode *i, struct fd *f, int flag)
{
	struct inotify_instance *inst = i->u.inotify.instance;

	if(flag == SEL_R) {
		return (inst && inst->q_count) ? 1 : 0;
	}
	return 1;
}

int inotifyfs_ioctl(struct inode *i, struct fd *f, int cmd, addr_t arg)
{
	struct inotify_instance *inst = i->u.inotify.instance;
	int errno;

	if(cmd == FIONREAD) {
		if((errno = check_user_area(VERIFY_WRITE, (void *)arg, sizeof(int)))) {
			return errno;
		}
		*(int *)arg = inst ? inst->q_bytes : 0;
		return 0;
	}
	return -EINVAL;
}

/* ---- event generation (called from the VFS syscall paths) ---- */

/*
 * inotify_queue(): fire watches on 'inode'. For child events (create,
 * delete, move, attrib on a directory entry) pass the directory inode
 * and the child name; for self events (write, close, open on the inode
 * itself) pass the inode and NULL.
 */
void inotify_queue(struct inode *inode, __u32 mask, __u32 cookie, const char *name)
{
	struct inotify_watch *w;
	struct inotify_instance *inst;
	struct inotify_qevent *qe;
	__size_t len;

	/* scan the global watch list (typically a handful of watches),
	 * not the inode table which grows with every file created */
	for(w = inotify_watch_head; w; w = w->next) {
		if(w->inode != inode || !(mask & w->mask)) {
			continue;
		}
		inst = w->inst;
		len = name ? strlen(name) + 1 : 0;
		if(!(qe = (struct inotify_qevent *)kmalloc(sizeof(struct inotify_qevent) + len))) {
			continue;
		}
		qe->ev.wd = w->wd;
		qe->ev.mask = mask;
		qe->ev.cookie = cookie;
		qe->ev.len = len;
		qe->next = NULL;
		if(len) {
			memcpy_b(qe->ev.name, name, len);
		}
		if(inst->q_tail) {
			inst->q_tail->next = qe;
		} else {
			inst->q_head = qe;
		}
		inst->q_tail = qe;
		inst->q_count++;
		inst->q_bytes += (int)event_size(&qe->ev);
		wakeup(&do_select);
	}
}

/* ---- the syscalls ---- */

static int inotify_alloc(struct inode **i_res)
{
	struct filesystems *fs;
	struct inode *i;
	int fd, ufd;

	if(!(fs = get_filesystem("inotifyfs"))) {
		printk("WARNING: %s(): inotifyfs filesystem is not registered!\n", __FUNCTION__);
		return -EINVAL;
	}
	if(!(i = ialloc(&fs->mp->sb, S_IFREG))) {
		return -ENOMEM;
	}
	if((fd = get_new_fd(i)) < 0) {
		iput(i);
		return -ENFILE;
	}
	if((ufd = get_new_user_fd(0)) < 0) {
		release_fd(fd);
		iput(i);
		return -EMFILE;
	}
	current->fd[ufd] = fd;
	fd_table[fd].flags = O_RDONLY;
	*i_res = i;
	return ufd;
}

int sys_inotify_init1(int flags)
{
	struct inode *i;
	struct inotify_instance *inst;
	int ufd;

	if(flags & ~(IN_NONBLOCK | IN_CLOEXEC)) {
		return -EINVAL;
	}
	if((ufd = inotify_alloc(&i)) < 0) {
		return ufd;
	}
	if(!(inst = (struct inotify_instance *)kmalloc(sizeof(struct inotify_instance)))) {
		sys_close(ufd);
		return -ENOMEM;
	}
	memset_b(inst, 0, sizeof(struct inotify_instance));
	i->u.inotify.instance = inst;
	if(flags & IN_NONBLOCK) {
		fd_table[current->fd[ufd]].flags |= O_NONBLOCK;
	}
	if(flags & IN_CLOEXEC) {
		current->fd_flags[ufd] |= FD_CLOEXEC;
	}
	return ufd;
}

int sys_inotify_init(void)
{
	return sys_inotify_init1(0);
}

int sys_inotify_add_watch(int ufd, const char *pathname, __u32 mask)
{
	struct inode *i, *dir, *target;
	struct inotify_instance *inst;
	struct inotify_watch *w;
	int errno;

	if(!mask) {
		return -EINVAL;
	}
	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;
	if(i->fsop != &inotifyfs_fsop || !i->u.inotify.instance) {
		return -EINVAL;
	}
	inst = i->u.inotify.instance;

	if((errno = namei(pathname, &target, &dir, 1))) {
		return errno;
	}
	if(dir) {
		iput(dir);
	}

	/* an existing watch on this inode just gets its mask updated */
	for(w = inotify_watch_head; w; w = w->next) {
		if(w->inst == inst && w->inode == target) {
			w->mask = mask;
			return w->wd;
		}
	}

	if(!(w = (struct inotify_watch *)kmalloc(sizeof(struct inotify_watch)))) {
		iput(target);
		return -ENOMEM;
	}
	w->wd = ++inst->wd_counter;
	w->mask = mask;
	w->inode = target;
	w->inst = inst;
	w->next = inotify_watch_head;
	inotify_watch_head = w;
	return w->wd;
}

int sys_inotify_rm_watch(int ufd, int wd)
{
	struct inode *i;
	struct inotify_instance *inst;
	struct inotify_watch *w, *prev;

	CHECK_UFD(ufd);
	i = fd_table[current->fd[ufd]].inode;
	if(i->fsop != &inotifyfs_fsop || !i->u.inotify.instance) {
		return -EINVAL;
	}
	inst = i->u.inotify.instance;

	prev = NULL;
	for(w = inotify_watch_head; w; w = w->next) {
		if(w->inst == inst && w->wd == wd) {
			if(prev) {
				prev->next = w->next;
			} else {
				inotify_watch_head = w->next;
			}
			iput(w->inode);
			kfree((addr_t)w);
			return 0;
		}
		prev = w;
	}
	return -EINVAL;
}
