/*
 * fnx/kernel/syscalls/msgsnd.c
 *
 * Copyright 2022, Jordi Sanfeliu. All rights reserved.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/config.h>
#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/string.h>
#include <fnx/errno.h>
#include <fnx/process.h>
#include <fnx/sleep.h>
#include <fnx/sched.h>
#include <fnx/mm.h>
#include <fnx/ipc.h>
#include <fnx/msg.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

#ifdef CONFIG_SYSVIPC
int sys_msgsnd(int msqid, const void *msgp, __size_t msgsz, int msgflg)
{
	struct msqid_ds *mq;
	struct msg *m;
	char *mtext;
	long utype;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_msgsnd(%d, 0x%08x, %d, 0x%x)\n", current->pid, msqid, (int)msgp, msgsz, msgflg);
#endif /*__DEBUG__ */

	if(msqid < 0 || msgsz > MSGMAX) {
		return -EINVAL;
	}
	/* the x86-64 user ABI is struct { long mtype; char mtext[]; } - the
	 * mtype is 8 bytes and the text starts at +8, not at the kernel's
	 * 4-byte 'int mtype' offset */
	if((errno = check_user_area(VERIFY_READ, msgp, sizeof(void *)))) {
		return errno;
	}
	mtext = ((char *)msgp) + sizeof(long);
	utype = *(long *)msgp;
	if(utype < 0) {
		return -EINVAL;
	}
	/* the kernel stores an int msg_type; reject values that would
	 * silently truncate and collide with small types */
	if(utype > 0x7FFFFFFF) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_READ, mtext, msgsz))) {
		return errno;
	}

	mq = msgque[msqid % MSGMNI];
	if(mq == IPC_UNUSED) {
		return -EINVAL;
	}
	for(;;) {
		if(!ipc_has_perms(&mq->msg_perm, IPC_W)) {
			return -EACCES;
		}
		if(mq->msg_cbytes + msgsz > mq->msg_qbytes || mq->msg_qnum + 1 > mq->msg_qbytes) {
			if(msgflg & IPC_NOWAIT) {
				return -EAGAIN;
			}
			if(sleep(mq, PROC_INTERRUPTIBLE)) {
				return -EINTR;
			}
		}
		if(mq == IPC_UNUSED) {
			return -EIDRM;
		}
		break;
	}

	if(!(m = msg_get_new_md())) {
		return -ENOMEM;
	}
	m->msg_next = NULL;
	m->msg_type = (int)utype;
	if(!(m->msg_spot = (void *)kmalloc(PAGE_SIZE))) {
		msg_release_md(m);
		return -ENOMEM;
	}
	memcpy_b(m->msg_spot, mtext, msgsz);
	m->msg_stime = CURRENT_TIME;
	m->msg_ts = msgsz;
	lock_resource(&ipcmsg_resource);
	if(!mq->msg_first) {
		mq->msg_first = mq->msg_last = m;
	} else {
		mq->msg_last->msg_next = m;
		mq->msg_last = m;
	}
	mq->msg_stime = mq->msg_ctime = CURRENT_TIME;
	mq->msg_qnum++;
	mq->msg_cbytes += msgsz;
	mq->msg_lspid = current->pid;
	num_msgs++;
	unlock_resource(&ipcmsg_resource);
	wakeup(mq);
	return 0;
}
#endif /* CONFIG_SYSVIPC */
