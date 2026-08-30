/*
 * fnx/kernel/syscalls/msgctl.c
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
#include <fnx/msg.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

#ifdef CONFIG_SYSVIPC
int sys_msgctl(int msqid, int cmd, struct msqid_ds *buf)
{
	struct msqid_ds *mq;
	struct msginfo *mi;
	struct ipc_perm *perm;
	struct msg *m, *mn;
	int errno;

#ifdef __DEBUG__
	printk("(pid %d) sys_msgctl(%d, %d, 0x%x)\n", current->pid, msqid, cmd, (int)buf);
#endif /*__DEBUG__ */

	if(msqid < 0) {
		return -EINVAL;
	}

	/* musl/glibc OR IPC_64 into cmd; mask it to get the real command */
	cmd &= ~IPC_64;
	switch(cmd) {	
		case MSG_STAT:
		case IPC_STAT:
#ifdef __x86_64__
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct msqid64_ds)))) {
				return errno;
			}
#else
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct msqid_ds)))) {
				return errno;
			}
#endif
			mq = msgque[msqid % MSGMNI];
			if(mq == IPC_UNUSED) {
				return -EINVAL;
			}
			/* MSG_STAT takes a kernel slot id (not seq-encoded) */
			if(cmd == IPC_STAT) {
				IPC_SEQ_CHECK(mq->msg_perm.seq, msqid, MSGMNI);
			}
			if(!ipc_has_perms(&mq->msg_perm, IPC_R)) {
				return -EACCES;
			}
#ifdef __x86_64__
			{
				struct msqid64_ds u;

				ipc64_msqid_to_user(&u, mq);
				memcpy_b(buf, &u, sizeof(u));
			}
#else
			memcpy_b(buf, mq, sizeof(struct msqid_ds));
#endif
			if(cmd == MSG_STAT) {
				return (mq->msg_perm.seq * MSGMNI) + msqid;
			}
			return 0;

		case IPC_SET:
#ifdef __x86_64__
			{
				struct msqid64_ds u;

				if((errno = check_user_area(VERIFY_READ, buf, sizeof(struct msqid64_ds)))) {
					return errno;
				}
				memcpy_b(&u, buf, sizeof(u));
				mq = msgque[msqid % MSGMNI];
				if(mq == IPC_UNUSED) {
					return -EINVAL;
				}
				IPC_SEQ_CHECK(mq->msg_perm.seq, msqid, MSGMNI);
				perm = &mq->msg_perm;
				if(!IS_SUPERUSER && current->euid != perm->uid && current->euid != perm->cuid) {
					return -EPERM;
				}
				if(!IS_SUPERUSER && u.msg_qbytes > MSGMNB) {
					return -EPERM;
				}
				if(u.msg_qbytes > 0xFFFF) {
					return -EINVAL;
				}
				mq->msg_qbytes = u.msg_qbytes;
				ipc64_perm_from_user(perm, &u.msg_perm);
				mq->msg_ctime = CURRENT_TIME;
				return 0;
			}
#else
			if((errno = check_user_area(VERIFY_READ, buf, sizeof(struct msqid_ds)))) {
				return errno;
			}
			mq = msgque[msqid % MSGMNI];
			if(mq == IPC_UNUSED) {
				return -EINVAL;
			}
			IPC_SEQ_CHECK(mq->msg_perm.seq, msqid, MSGMNI);
			perm = &mq->msg_perm;
			if(!IS_SUPERUSER && current->euid != perm->uid && current->euid != perm->cuid) {
				return -EPERM;
			}
			if(!IS_SUPERUSER && buf->msg_qbytes > MSGMNB) {
				return -EPERM;
			}
			mq->msg_qbytes = buf->msg_qbytes;
			perm->uid = buf->msg_perm.uid;
			perm->gid = buf->msg_perm.gid;
			perm->mode = (perm->mode & ~0777) | (buf->msg_perm.mode & 0777);
			mq->msg_ctime = CURRENT_TIME;
			return 0;
#endif

		case IPC_RMID:
			mq = msgque[msqid % MSGMNI];
			if(mq == IPC_UNUSED) {
				return -EINVAL;
			}
			IPC_SEQ_CHECK(mq->msg_perm.seq, msqid, MSGMNI);
			perm = &mq->msg_perm;
			if(!IS_SUPERUSER && current->euid != perm->uid && current->euid != perm->cuid) {
				return -EPERM;
			}
			if((m = mq->msg_first)) {
				do {
					mq->msg_qnum--;
					mq->msg_cbytes -= m->msg_ts;
					num_msgs--;
					mn = m->msg_next;
					msg_release_md(m);
					m = mn;
				} while(m);
			}
			msg_release_mq(mq);
			msgque[msqid % MSGMNI] = (struct msqid_ds *)IPC_UNUSED;
			num_queues--;
			msg_seq++;
			if((msqid % MSGMNI) == max_mqid) {
				while(max_mqid) {
					if(msgque[max_mqid] != IPC_UNUSED) {
						break;
					}
					max_mqid--;
				}
			}
			wakeup(mq);
			return 0;

		case MSG_INFO:
		case IPC_INFO:
			if((errno = check_user_area(VERIFY_WRITE, buf, sizeof(struct msqid_ds)))) {
				return errno;
			}
			mi = (struct msginfo *)buf;
			if(cmd == MSG_INFO) {
				mi->msgpool = num_queues;
				mi->msgmap = num_msgs;
				mi->msgssz = 0;		/* FIXME: pending to do */
				mi->msgtql = 0;		/* FIXME: pending to do */
				mi->msgseg = 0;		/* FIXME: pending to do */
			} else {
				mi->msgpool = 0;	/* FIXME: pending to do */
				mi->msgmap = 0;		/* FIXME: pending to do */
				mi->msgssz = 0;		/* FIXME: pending to do */
				mi->msgtql = MSGTQL;
				mi->msgseg = 0;		/* FIXME: pending to do */
			}
			mi->msgmax = MSGMAX;
			mi->msgmnb = MSGMNB;
			mi->msgmni = MSGMNI;
			return max_mqid;
	}

	return -EINVAL;
}
#endif /* CONFIG_SYSVIPC */
