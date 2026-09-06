/*
 * fnx/userland/hello_dl.c
 *
 * M0 acceptance binary for the dynamic-linking move
 * (docs/shared-libraries-plan.md): a dynamic (non-PIE) musl executable.
 * It is exec'd by the kernel, which loads /System/Libraries/ld-musl-
 * x86_64.so.1 as the interpreter (PT_INTERP) at ELF_INTERP_BASE; musl's
 * ld.so then relocates itself, loads libc.so from the baked search path
 * and calls back into this main via auxv AT_ENTRY. Lives under
 * System/Shared/tests because System/Tools stays zero-allow static
 * (fshlint R1) until the whole-world flip.
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	printf("dl hello: dynamic executable alive (pid %d)\n", (int)getpid());
	return 0;
}
