/*
 * fnx/kernel/syscalls/shmdt.c
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
#include <fnx/mm.h>
#include <fnx/mman.h>
#include <fnx/ipc.h>
#include <fnx/shm.h>
#include <fnx/stdio.h>

#ifdef CONFIG_SYSVIPC
int sys_shmdt(char *shmaddr)
{
	struct vma *vma;
	struct shmid_ds *seg;
	addr_t addr;
	int n;

#ifdef __DEBUG__
	printk("(pid %d) sys_shmdt(0x%x)\n", current->pid, (int)shmaddr);
#endif /*__DEBUG__ */

	addr = (addr_t)shmaddr;

	if(!(vma = find_vma_region(addr))) {
		printk("WARNING: %s(): no vma region found!\n", __FUNCTION__);
		return 0;
	}
	if(vma->s_type != P_SHM) {
		printk("WARNING: %s(): vma region is not a shared memory!\n", __FUNCTION__);
		return 0;
	}
	if(!(seg = (struct shmid_ds *)vma->object)) {
		printk("WARNING: %s(): object is NULL!\n", __FUNCTION__);
		return 0;
	}

	for(n = 0; n < NUM_ATTACHES_PER_SEG; n++) {
		if(seg->shm_attaches[n].start == addr) {
			do_munmap(addr, seg->shm_attaches[n].end - seg->shm_attaches[n].start);
			shm_release_attach(&seg->shm_attaches[n]);
			seg->shm_nattch--;
		}
	}

	/* IPC_RMID (SHM_DEST) while attached must free the segment when the
	 * last attach detaches - otherwise every detach of a destroyed-but-
	 * still-attached segment leaks it forever (e.g. the compositor's
	 * resize: it RMIDs the old backing while the client still holds its
	 * attach, and the client's shmdt on the resize ack is the last
	 * detach) */
	if((seg->shm_perm.mode & SHM_DEST) && !seg->shm_nattch) {
		int shmid = -1;

		for(n = 0; n < SHMMNI; n++) {
			if(shmseg[n] == seg) {
				shmid = (seg->shm_perm.seq * SHMMNI) + n;
				break;
			}
		}
		if(shmid >= 0) {
			free_seg(shmid);
		}
	}

	return 0;
}
#endif /* CONFIG_SYSVIPC */
