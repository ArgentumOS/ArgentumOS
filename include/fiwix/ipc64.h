/*
 * fiwix/include/fiwix/ipc64.h
 *
 * Fiwix64: the musl x86-64 (LP64) user-space layouts of the SysV IPC
 * structures. The kernel's struct ipc_perm / semid_ds / msqid_ds /
 * shmid_ds are the i386-era compact layouts (plus kernel-private fields);
 * they must NOT be memcpy'd to/from user space on the 64-bit ABI. These
 * mirror arch/generic/bits/{ipc,sem,msg,shm}.h as built for x86-64.
 */

#ifndef _FIWIX_IPC64_H
#define _FIWIX_IPC64_H

#include <fiwix/types.h>
#include <fiwix/ipc.h>
#include <fiwix/sem.h>
#include <fiwix/msg.h>
#include <fiwix/shm.h>

struct ipc64_perm {
	key_t		key;		/* 0x00 */
	__u32		uid;		/* 0x04 */
	__u32		gid;		/* 0x08 */
	__u32		cuid;		/* 0x0c */
	__u32		cgid;		/* 0x10 */
	unsigned int	mode;		/* 0x14 */
	unsigned int	seq;		/* 0x18 */
	unsigned long	__pad1;		/* 0x20 */
	unsigned long	__pad2;		/* 0x28 */
};					/* 48 bytes */

struct semid64_ds {
	struct ipc64_perm	sem_perm;	/* 0x00 */
	__s64			sem_otime;	/* 0x30 */
	__s64			__unused1;	/* 0x38 */
	__s64			sem_ctime;	/* 0x40 */
	__s64			__unused2;	/* 0x48 */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	unsigned short		sem_nsems;	/* 0x50 */
	unsigned char		__sem_nsems_pad[6];
#else
	unsigned char		__sem_nsems_pad[6];
	unsigned short		sem_nsems;	/* 0x50 */
#endif
	unsigned long		__unused3;	/* 0x58 */
	unsigned long		__unused4;	/* 0x60 */
};					/* 104 bytes */

struct msqid64_ds {
	struct ipc64_perm	msg_perm;	/* 0x00 */
	__s64			msg_stime;	/* 0x30 */
	__s64			msg_rtime;	/* 0x38 */
	__s64			msg_ctime;	/* 0x40 */
	unsigned long		msg_cbytes;	/* 0x48 */
	unsigned long		msg_qnum;	/* 0x50 */
	unsigned long		msg_qbytes;	/* 0x58 */
	int			msg_lspid;	/* 0x60 */
	int			msg_lrpid;	/* 0x64 */
	unsigned long		__unused[2];	/* 0x68 */
};					/* 120 bytes */

struct shmid64_ds {
	struct ipc64_perm	shm_perm;	/* 0x00 */
	unsigned long		shm_segsz;	/* 0x30 */
	__s64			shm_atime;	/* 0x38 */
	__s64			shm_dtime;	/* 0x40 */
	__s64			shm_ctime;	/* 0x48 */
	int			shm_cpid;	/* 0x50 */
	int			shm_lpid;	/* 0x54 */
	unsigned long		shm_nattch;	/* 0x58 */
	unsigned long		__pad1;		/* 0x60 */
	unsigned long		__pad2;		/* 0x68 */
};					/* 112 bytes */

/* Fiwix64: SysV IPC struct conversion (kernel <-> musl x86-64 ABI) */
void ipc64_perm_to_user(struct ipc64_perm *, struct ipc_perm *);
void ipc64_perm_from_user(struct ipc_perm *, struct ipc64_perm *);
void ipc64_semid_to_user(struct semid64_ds *, struct semid_ds *);
void ipc64_msqid_to_user(struct msqid64_ds *, struct msqid_ds *);
void ipc64_shmid_to_user(struct shmid64_ds *, struct shmid_ds *);

#endif /* _FIWIX_IPC64_H */
