/*
 * fiwix/kernel64/sections64.c
 *
 * Fiwix64 (M4-B): section-boundary symbols for the REAL kernel
 * (_etext/_edata/_end, referenced by mm/memory.c). A PE linker script
 * corrupted ld i386pep's section headers, so these are plain C symbols:
 * their addresses sit in .bss near the end of the image, which is close
 * enough for the early boot (mem_init carves its tables right after the
 * image and aligns them; KERNEL_BSS_SIZE ends up ~0 so the BSS zeroing in
 * setup_tmp_pgdir() is a no-op). Revisit with a proper PE script when the
 * real mm is ported.
 */

char _etext;
char _edata;
char _end;
