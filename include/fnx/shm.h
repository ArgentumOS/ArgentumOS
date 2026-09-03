/*
 * fnx/include/fnx/shm.h
 */

#ifdef CONFIG_SYSVIPC

#ifndef _FNX_SHM_H
#define _FNX_SHM_H

#include <fnx/types.h>
#include <fnx/ipc.h>

#define SHM_DEST	01000		/* destroy segment on last detach */
#define	SHM_RDONLY	010000		/* attach a read-only segment */
#define	SHM_RND		020000		/* round attach address to SHMLBA */
#define	SHM_REMAP	040000		/* take-over region on attach */

/* super user shmctl commands */
#define SHM_LOCK 	11
#define SHM_UNLOCK 	12

/* system-wide limits */
/*
 * *shm_pages is a dynamically sized array of page-frame pointers
 * (one addr_t per segment page) allocated with kmalloc64() at shmget
 * time, so a segment is only limited by SHMMAX (and memory). 64 MB
 * matches the Linux-x86 shminfo default and comfortably fits full-screen
 * backing stores (the 1280x800x32 GOP framebuffer is 4 MB).
 */
#define SHMMAX		0x4000000	/* max. segment size (in bytes) = 64MB */

#define SHMMIN		1		/* min. segment size (in bytes) */
#define SHMMNI		128		/* max. number of shared segments */
#define SHMSEG		SHMMNI		/* max. segments per process */
#define SHMLBA		PAGE_SIZE	/* low boundary address (in bytes) */
#define SHMALL		524288		/* max. total segments (in pages) */

#define SHM_STAT 	13
#define SHM_INFO 	14

#define NUM_ATTACHES_PER_SEG	(PAGE_SIZE / sizeof(struct vma))

struct shmid_ds {
	struct ipc_perm shm_perm;	/* access permissions */
	__size_t shm_segsz;		/* size of segment (in bytes) */
	__time_t shm_atime;		/* time of the last shmat() */
	__time_t shm_dtime;		/* time of the last shmdt() */
	__time_t shm_ctime;		/* time of the last change */
	unsigned short shm_cpid;	/* pid of creator */
	unsigned short shm_lpid;	/* pid of last shm operation */
	unsigned short shm_nattch;	/* num. of current attaches */
	/* the following are for kernel only */
	unsigned short shm_npages;	/* size of segment (in pages) */
	addr_t *shm_pages;	/* array of ptrs to frames -> SHMMAX */
	struct vma *shm_attaches;	/* ptr to array of attached regions */
};

struct shminfo {
	int shmmax;
	int shmmin;
	int shmmni;
	int shmseg;
	int shmall;
};

struct shm_info {
	int used_ids;
	unsigned int shm_tot;		/* total allocated shm */
	unsigned int shm_rss;		/* total resident shm */
	unsigned int shm_swp;		/* total swapped shm */
	unsigned int swap_attempts;
	unsigned int swap_successes;
};

extern struct shmid_ds *shmseg[];
extern unsigned int num_segs;
extern unsigned int max_segid;
extern unsigned int shm_seq;
extern unsigned int shm_tot;
extern unsigned int shm_rss;

void shm_init(void);
struct shmid_ds *shm_get_new_seg(void);
void shm_release_seg(struct shmid_ds *);
void free_seg(int);
struct vma *shm_get_new_attach(struct shmid_ds *);
void shm_release_attach(struct vma *);
int shm_map_page(struct vma *, unsigned long);
addr_t sys_shmat(int, char *, int, unsigned int *);
int sys_shmdt(char *);
int sys_shmget(key_t, __size_t, int);
int sys_shmctl(int, int, struct shmid_ds *);

#endif /* _FNX_SHM_H */

#endif /* CONFIG_SYSVIPC */
