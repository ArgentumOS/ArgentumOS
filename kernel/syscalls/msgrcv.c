/*
 * fnx/kernel/syscalls/msgrcv.c
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
int sys_msgrcv(int msqid, void *msgp, __size_t msgsz, int msgtyp, int msgflg)
{
	struct msqid_ds *mq;
	struct msg *m, *mprev;
	int errno, found, count;

#ifdef __DEBUG__
	printk("(pid %d) sys_msgrcv(%d, 0x%08x, %d, %d, 0x%x)\n", current->pid, msqid, (int)msgp, msgsz, msgtyp, msgflg);
#endif /*__DEBUG__ */

	if(msqid < 0) {
		return -EINVAL;
	}
	if((errno = check_user_area(VERIFY_WRITE, msgp, sizeof(void *)))) {
		return errno;
	}
	mq = msgque[msqid % MSGMNI];
	if(mq == IPC_UNUSED) {
		return -EINVAL;
	}
	IPC_SEQ_CHECK(mq->msg_perm.seq, msqid, MSGMNI);
	found = 0;
	mprev = NULL;
	for(;;) {
		if(!ipc_has_perms(&mq->msg_perm, IPC_R)) {
			return -EACCES;
		}
		if((m = mq->msg_first)) {
			if(!msgtyp) {
				break;
			} else if(msgtyp > 0) {
				if(msgflg & MSG_EXCEPT) {
					while(m) {
						if(m->msg_type != msgtyp) {
							found = 1;
							break;
						}
						mprev = m;
						m = m->msg_next;
					}
				} else {
					while(m) {
						if(m->msg_type == msgtyp) {
							found = 1;
							break;
						}
						mprev = m;
						m = m->msg_next;
					}
				}
			} else {
				/* FIXME: pending to do */
			}
		}
		if(found) {
			break;
		}
		if(msgflg & IPC_NOWAIT) {
			return -ENOMSG;
		}
		if(sleep(mq, PROC_INTERRUPTIBLE)) {
			return -EINTR;
		}
		mq = msgque[msqid % MSGMNI];
		if(mq == IPC_UNUSED) {
			return -EIDRM;
		}
		IPC_SEQ_CHECK(mq->msg_perm.seq, msqid, MSGMNI);
	}

	if(msgsz < m->msg_ts) {
		if(!(msgflg & MSG_NOERROR)) {
			return -E2BIG;
		}
		count = msgsz;
	} else {
		count = m->msg_ts;
	}

	/* x86-64 user ABI: 8-byte long mtype, text at +8; the kernel
	 * stores an int msg_type, so zero-extend it into the user's long.
	 * Both writes go through the fault-recovering copy_to_user (the old
	 * code verified only 8 bytes and wrote up to 4 KiB unchecked). */
	{
		long utype = (long)m->msg_type;

		if((errno = copy_to_user(msgp, &utype, sizeof(long)))) {
			return errno;
		}
		if((errno = copy_to_user((char *)msgp + sizeof(long), m->msg_spot, count))) {
			return errno;
		}
	}

	lock_resource(&ipcmsg_resource);
	kfree((addr_t)m->msg_spot);
	if(!mprev) {
		mq->msg_first = m->msg_next;
	} else {
		mprev->msg_next = m->msg_next;
	}
	mq->msg_rtime = mq->msg_ctime = CURRENT_TIME;
	mq->msg_qnum--;
	mq->msg_cbytes -= m->msg_ts;
	mq->msg_lrpid = current->pid;
	num_msgs--;
	unlock_resource(&ipcmsg_resource);
	msg_release_md(m);
	wakeup(mq);
	return count;
}
#endif /* CONFIG_SYSVIPC */
