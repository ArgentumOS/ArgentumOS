/*
 * fnx/userland/tests/hello.c
 *
 * M0 acceptance source for the Clang toolchain move
 * (docs/design/llvm-clang-toolchain-plan.md): one plain-C main built twice by
 * the clang wrappers - tools/musl-clang64.sh (dynamic) and
 * tools/musl-clang64-static.sh (static). Both are staged under
 * System/Shared/tests (a lint carve-out tree) by `make m0clang`.
 */
#include <stdio.h>
#include <unistd.h>

int main(void)
{
	printf("clang hello alive (pid %d)\n", (int)getpid());
	return 0;
}
