/*
 * fiwix/kernel/syscalls/ipc.c
 *
 * Copyright 2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 *
 * Fiwix64 (pure x86-64 port): the i386 sys_ipc() multiplexer was deleted;
 * this file keeps only the shared SysV IPC helpers used by the native
 * sys_msg, sys_sem and sys_shm syscalls.
 */

#include <fiwix/config.h>
#include <fiwix/types.h>
#include <fiwix/errno.h>
#include <fiwix/process.h>
#include <fiwix/string.h>
#include <fiwix/ipc.h>
#ifdef __x86_64__
#include <fiwix/ipc64.h>
#endif
#include <fiwix/sem.h>
#include <fiwix/msg.h>
#include <fiwix/shm.h>

#ifdef __DEBUG__
#include <fiwix/stdio.h>
#endif /*__DEBUG__ */

#ifdef CONFIG_SYSVIPC
struct resource ipcmsg_resource = { 0, 0 };

void ipc_init(void)
{
	sem_init();
	msg_init();
	shm_init();
}

int ipc_has_perms(struct ipc_perm *perm, int mode)
{
	if(IS_SUPERUSER) {
		return 1;
	}

	if(current->euid != perm->uid || current->euid != perm->cuid) {
		mode >>= 3;
		if(current->egid != perm->gid || current->egid != perm->cgid) {
			mode >>= 3;
		}
	}

	/*
	 * The user may specify zero for the second argument in xxxget() to
	 * bypass this check, and be able to obtain an identifier for an IPC
	 * object even when the user don't has read or write access to that
	 * IPC object. Later specific IPC calls will return error if the
	 * program attempted an operation requiring read or write permission
	 * on the IPC object.
	 */
	if(!mode) {
		return 1;
	}

	if(perm->mode & mode) {
		return 1;
	}

	return 0;
}

#ifdef __x86_64__
/*
 * Fiwix64: convert between the kernel's compact i386-era IPC structs and
 * the musl x86-64 (LP64) user ABI layouts. The kernel structs carry extra
 * private fields and 16/32-bit members; memcpy'ing them to/from user space
 * corrupts the ABI view (IPC_STAT returned wrong fields, IPC_SET wrote
 * them back at wrong offsets).
 */
void ipc64_perm_to_user(struct ipc64_perm *u, struct ipc_perm *k)
{
	memset_b(u, 0, sizeof(struct ipc64_perm));
	u->key = k->key;
	u->uid = k->uid;
	u->gid = k->gid;
	u->cuid = k->cuid;
	u->cgid = k->cgid;
	u->mode = k->mode;
	u->seq = k->seq;
}

void ipc64_perm_from_user(struct ipc_perm *k, struct ipc64_perm *u)
{
	k->uid = u->uid;
	k->gid = u->gid;
	k->cuid = u->cuid;
	k->cgid = u->cgid;
	/* preserve private bits (SHM_DEST etc.) outside the permission range */
	k->mode = (k->mode & ~07777) | (u->mode & 07777);
	/* key and seq are never changed by IPC_SET */
}

void ipc64_semid_to_user(struct semid64_ds *u, struct semid_ds *k)
{
	memset_b(u, 0, sizeof(struct semid64_ds));
	ipc64_perm_to_user(&u->sem_perm, &k->sem_perm);
	u->sem_otime = (__s64)k->sem_otime;
	u->sem_ctime = (__s64)k->sem_ctime;
	u->sem_nsems = k->sem_nsems;
}

void ipc64_msqid_to_user(struct msqid64_ds *u, struct msqid_ds *k)
{
	memset_b(u, 0, sizeof(struct msqid64_ds));
	ipc64_perm_to_user(&u->msg_perm, &k->msg_perm);
	u->msg_stime = (__s64)k->msg_stime;
	u->msg_rtime = (__s64)k->msg_rtime;
	u->msg_ctime = (__s64)k->msg_ctime;
	u->msg_cbytes = k->msg_cbytes;
	u->msg_qnum = k->msg_qnum;
	u->msg_qbytes = k->msg_qbytes;
	u->msg_lspid = k->msg_lspid;
	u->msg_lrpid = k->msg_lrpid;
}

void ipc64_shmid_to_user(struct shmid64_ds *u, struct shmid_ds *k)
{
	memset_b(u, 0, sizeof(struct shmid64_ds));
	ipc64_perm_to_user(&u->shm_perm, &k->shm_perm);
	u->shm_segsz = (unsigned long)k->shm_segsz;
	u->shm_atime = (__s64)k->shm_atime;
	u->shm_dtime = (__s64)k->shm_dtime;
	u->shm_ctime = (__s64)k->shm_ctime;
	u->shm_cpid = k->shm_cpid;
	u->shm_lpid = k->shm_lpid;
	u->shm_nattch = k->shm_nattch;
}
#endif /* __x86_64__ */
#endif /* CONFIG_SYSVIPC */
