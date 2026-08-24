/*
 * fnx/kernel/syscalls/shmctl.c
 *
 * Copyright 2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/string.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/errno.h>
#include <fnx/ipc.h>
#ifdef __x86_64__
#include <fnx/ipc64.h>
#endif
#include <fnx/shm.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

#ifdef CONFIG_SYSVIPC
int sys_shmctl(int shmid, int cmd, struct shmid_ds *buf)
{
	struct shmid_ds *seg;
	struct shminfo *si;
	struct shm_info *s_i;
	struct ipc_perm *perm;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_shmctl(%d, %d, 0x%x)\n", current->pid, shmid, cmd, (int)buf);
#endif /*__DEBUG__ */

	if(shmid < 0) {
		return -EINVAL;
	}

	/* musl/glibc OR IPC_64 into cmd; mask it to get the real command */
	cmd &= ~IPC_64;
	switch(cmd) {	
		case IPC_STAT:
		case SHM_STAT:
#ifdef __x86_64__
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct shmid64_ds)))) {
				return errno;
			}
#else
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct shmid_ds)))) {
				return errno;
			}
#endif
			if(cmd == SHM_STAT) {
				if(shmid > max_segid) {
					return -EINVAL;
				}
				seg = shmseg[shmid];
			} else {
				seg = shmseg[shmid % SHMMNI];
			}
			if(seg == IPC_UNUSED) {
				return -EINVAL;
			}
			if(!ipc_has_perms(&seg->shm_perm, IPC_R)) {
				return -EACCES;
			}
#ifdef __x86_64__
			{
				struct shmid64_ds u;

				ipc64_shmid_to_user(&u, seg);
				memcpy_b(buf, &u, sizeof(u));
			}
#else
			memcpy_b(buf, seg, sizeof(struct shmid_ds));
			/* private kernel information zeroed */
			buf->shm_npages = 0;
			buf->shm_pages = 0;
			buf->shm_attaches = 0;
#endif
			if(cmd == SHM_STAT) {
				return (seg->shm_perm.seq * SHMMNI) + shmid;
			}
			return 0;

		case IPC_SET:
#ifdef __x86_64__
			{
				struct shmid64_ds u;

				if((errno = check_user_area(VERIFY_READ, buf, sizeof(struct shmid64_ds)))) {
					return errno;
				}
				memcpy_b(&u, buf, sizeof(u));
				seg = shmseg[shmid % SHMMNI];
				if(seg == IPC_UNUSED) {
					return -EINVAL;
				}
				perm = &seg->shm_perm;
				if(!IS_SUPERUSER && current->euid != perm->uid && current->euid != perm->cuid) {
					return -EPERM;
				}
				ipc64_perm_from_user(perm, &u.shm_perm);
				seg->shm_ctime = CURRENT_TIME;
				return 0;
			}
#else
			if((errno = check_user_area(VERIFY_READ, buf, sizeof(struct shmid_ds)))) {
				return errno;
			}
			seg = shmseg[shmid % SHMMNI];
			if(seg == IPC_UNUSED) {
				return -EINVAL;
			}
			perm = &seg->shm_perm;
			if(!IS_SUPERUSER && current->euid != perm->uid && current->euid != perm->cuid) {
				return -EPERM;
			}
			perm->uid = buf->shm_perm.uid;
			perm->gid = buf->shm_perm.gid;
			perm->mode = (perm->mode & ~0777) | (buf->shm_perm.mode & 0777);
			seg->shm_ctime = CURRENT_TIME;
			return 0;
#endif

		case IPC_RMID:
			seg = shmseg[shmid % SHMMNI];
			if(seg == IPC_UNUSED) {
				return -EINVAL;
			}
			perm = &seg->shm_perm;
			if(!IS_SUPERUSER && current->euid != perm->uid && current->euid != perm->cuid) {
				return -EPERM;
			}
			perm->mode |= SHM_DEST;
			if(!seg->shm_nattch) {
				free_seg(shmid);
			}
			return 0;

		case IPC_INFO:
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct shminfo)))) {
				return errno;
			}
			si = (struct shminfo *)buf;
			si->shmmax = SHMMAX;
			si->shmmin = SHMMIN;
			si->shmmni = SHMMNI;
			si->shmseg = SHMSEG;
			si->shmall = SHMALL;
			return max_segid;

		case SHM_INFO:
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct shm_info)))) {
				return errno;
			}
			s_i = (struct shm_info *)buf;
			s_i->used_ids = num_segs;
			s_i->shm_tot = shm_tot;
			s_i->shm_rss = shm_rss;
			s_i->shm_swp = 0;		/* FIXME: pending to do */
			s_i->swap_attempts = 0;		/* FIXME: pending to do */
			s_i->swap_successes = 0;	/* FIXME: pending to do */
			return max_segid;
	}

	return -EINVAL;
}
#endif /* CONFIG_SYSVIPC */
