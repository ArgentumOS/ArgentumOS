/*
 * fnx/kernel/syscalls/shmat.c
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
#include <fnx/fcntl.h>
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/ipc.h>
#include <fnx/shm.h>
#include <fnx/stdio.h>

#ifdef CONFIG_SYSVIPC
int shm_map_page(struct vma *vma, unsigned long cr2)
{
	struct shmid_ds *seg;
	struct page *pg;
	addr_t addr;
	unsigned long index;

	seg = (struct shmid_ds *)vma->object;
	index = (cr2 - vma->start) / PAGE_SIZE;
	addr = seg->shm_pages[index];

	if(!addr) {
		if(!(addr = map_page(current, cr2, 0, vma->prot))) {
			printk("%s(): Oops, map_page() returned 0!\n", __FUNCTION__);
			return 1;
		}
		seg->shm_pages[index] = addr;
		shm_rss++;
	} else {
		if(!(addr = map_page(current, cr2, V2P(addr), vma->prot))) {
			printk("%s(): Oops, map_page() returned 0!\n", __FUNCTION__);
			return 1;
		}
	}
	pg = &page_table[V2P(addr) >> PAGE_SHIFT];
	pg->count++;

	return 0;
}

addr_t sys_shmat(int shmid, char *shmaddr, int shmflg, unsigned int *raddr)
{
	struct shmid_ds *seg;
	struct vma *sega;
	addr_t addr;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_shmat(%d, 0x%x, %d, 0x%x)\n", current->pid, shmid, (int)shmaddr, shmflg, (int)raddr);
#endif /*__DEBUG__ */

	if(shmid < 0) {
		return -EINVAL;
	}

	seg = shmseg[shmid % SHMMNI];
	if(seg == IPC_UNUSED) {
		return -EINVAL;
	}

	addr = (addr_t)shmaddr;
	if(addr) {
		if(shmflg & SHM_RND) {
			addr &= ~(SHMLBA - 1);
		} else {
			if(addr & (SHMLBA - 1)) {
				return -EINVAL;
			}
		}
		if(find_vma_intersection(addr, addr + seg->shm_segsz)) {
			return -EINVAL;
		}
	} else {
		if(!(addr = get_unmapped_vma_region(seg->shm_segsz))) {
			return -ENOMEM;
		}
	}

	if(!ipc_has_perms(&seg->shm_perm, shmflg & SHM_RDONLY ? IPC_R : IPC_R | IPC_W)) {
		return -EACCES;
	}

	if(!(sega = shm_get_new_attach(seg))) {
		return -ENOMEM;
	}

	sega->start = addr;
	sega->end = addr + seg->shm_segsz;
	/* SHM_RDONLY must not produce a writable mapping: the old code
	 * mapped RWX unconditionally, so a read-only attach could still
	 * modify the segment */
	sega->prot = (shmflg & SHM_RDONLY) ? PROT_READ : (PROT_READ | PROT_WRITE);
	sega->flags = MAP_PRIVATE | MAP_FIXED;
	sega->offset = 0;
	sega->s_type = P_SHM;
	sega->inode = NULL;
	sega->o_mode = shmflg & SHM_RDONLY ? O_RDONLY : O_RDWR;
	seg->shm_nattch++;

	errno = do_mmap(NULL, addr, seg->shm_segsz, sega->prot, sega->flags, sega->offset, sega->s_type, sega->o_mode, seg);
	if(errno < 0 && errno > -PAGE_SIZE) {
		/* do_mmap failed: release the attach record and the count or
		 * every failed shmat leaks a slot and DoSes the segment */
		shm_release_attach(sega);
		seg->shm_nattch--;
		return errno;
	}

	seg->shm_atime = CURRENT_TIME;
	seg->shm_lpid = current->pid;

	/* FNX (x86-64 ABI): musl's shmat() does
	 * (void *)syscall(SYS_shmat, id, addr, flag) - the mapped address is
	 * the syscall RETURN VALUE, not a *raddr output (that was the i386
	 * sys_ipc() convention, which this port deleted). The dispatcher
	 * (user64.c) treats shmat as a 64-bit-return syscall. */
	return addr;
}
#endif /* CONFIG_SYSVIPC */
