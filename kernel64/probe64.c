/* Fiwix64: signal-info accessor retained for the kernel64 link. */
#include <fiwix/process.h>
#include <fiwix/sigcontext.h>

extern struct proc *current;

unsigned long probe_sig_info(void)
{
	return ((unsigned long)current->pid << 32) | (current->sigpending & 0xffff);
}
