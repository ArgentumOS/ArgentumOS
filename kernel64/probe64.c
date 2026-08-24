/* FNX: signal-info accessor retained for the kernel64 link. */
#include <fnx/process.h>
#include <fnx/sigcontext.h>

extern struct proc *current;

unsigned long probe_sig_info(void)
{
	return ((unsigned long)current->pid << 32) | (current->sigpending & 0xffff);
}
