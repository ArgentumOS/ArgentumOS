# kernel/boot64 — the UEFI boot half

Sources for the PE32+ EFI application (the thing UEFI loads from the ESP).
It owns the firmware handoff: EFI entry (`efi_stub.c`), long-mode setup
(`main64.c`, `paging64.c`, `gdt64.c`, `idt64.c`, `irq64.c`), the trampolines
(`switch64.S`, `init_trampoline64.S`), and the handoff into the real kernel
(`kreal64.c` → `kernel/main.c start_kernel()`).

Compiled by the top Makefile with `CC64K` into `.build/64/` and linked (GNU
ld `-m i386pep` + llvm-objcopy) with the real kernel's `.build/64real/`
objects into `.build/64/fnx.efi`. Excluded from the `REALSRCS` find: it is
never part of the real-kernel object set. `kreal64.c` is the special rule
that bakes the boot cmdline / `FNX_RECOVERY_PARAM` (see the Makefile).
