/*
 * fnx/kernel/syscalls/prlimit64.c
 *
 * FNX: prlimit64(302) - get and/or set a process's resource limits.
 *
 * musl's getrlimit()/setrlimit() call SYS_prlimit64 first (with pid=0)
 * and only fall back to the legacy getrlimit/setrlimit on ENOSYS, so
 * without this syscall every RLIMIT call fails. The ABI is the LP64
 * struct rlimit64: two unsigned long longs (rlim_cur, rlim_max).
 *
 *   prlimit64(pid, resource, new_limit, old_limit)
 *     - pid 0: the calling process
 *     - pid > 0: that process (permission-checked)
 *     - new_limit NULL: query only; old_limit NULL: set only
 */

#include <fnx/fs.h>
#include <fnx/resource.h>
#include <fnx/process.h>
#include <fnx/sched.h>
#include <fnx/errno.h>
#include <fnx/string.h>

#ifdef __DEBUG__
#include <fnx/stdio.h>
#endif /*__DEBUG__ */

int sys_prlimit64(__pid_t pid, int resource, const struct rlimit64 *new_limit, struct rlimit64 *old_limit)
{
	struct rlimit64 rl;
	struct proc *p;
	int errno;

	if(resource < 0 || resource >= RLIM_NLIMITS) {
		return -EINVAL;
	}
	if(!new_limit && !old_limit) {
		return -EINVAL;
	}

	if(pid == 0) {
		p = current;
	} else {
		FOR_EACH_PROCESS(p) {
			if(p->pid == pid) {
				break;
			}
		}
		if(!p || p->pid != pid) {
			return -ESRCH;
		}
		if(p->state == PROC_ZOMBIE) {
			return -ESRCH;
		}
		/* permission: same uid, or superuser */
		if(!IS_SUPERUSER && p->euid != current->euid && p->uid != current->uid) {
			return -EPERM;
		}
	}

	if(old_limit) {
		if((errno = check_user_area(VERIFY_WRITE, old_limit, sizeof(struct rlimit64)))) {
			return errno;
		}
		rl.rlim_cur = p->rlim[resource].rlim_cur;
		rl.rlim_max = p->rlim[resource].rlim_max;
		memcpy_b(old_limit, &rl, sizeof(struct rlimit64));
	}

	if(new_limit) {
		if((errno = check_user_area(VERIFY_READ, new_limit, sizeof(struct rlimit64)))) {
			return errno;
		}
		memcpy_b(&rl, new_limit, sizeof(struct rlimit64));
		if(rl.rlim_cur > rl.rlim_max) {
			return -EINVAL;
		}
		if(!IS_SUPERUSER) {
			if(rl.rlim_max > p->rlim[resource].rlim_max) {
				return -EPERM;
			}
		}
		/* Linux clamps RLIMIT_INFINITY from 64-bit to the kernel's
		 * 32-bit RLIMIT_INFINITY (0x7fffffff) in the stored table */
		if(rl.rlim_cur > RLIMIT_INFINITY) {
			rl.rlim_cur = RLIMIT_INFINITY;
		}
		if(rl.rlim_max > RLIMIT_INFINITY) {
			rl.rlim_max = RLIMIT_INFINITY;
		}
		p->rlim[resource].rlim_cur = rl.rlim_cur;
		p->rlim[resource].rlim_max = rl.rlim_max;
	}

	return 0;
}
