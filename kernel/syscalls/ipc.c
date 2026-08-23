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
#endif /* CONFIG_SYSVIPC */
