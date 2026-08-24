/*
 * fnx/kernel64/efi_stub.c
 *
 * FNX EFI stub (M1).
 *
 * This file IS the PE32+ UEFI application entry point. The firmware starts
 * it in long mode with paging enabled (identity map) and a working stack.
 *
 * Tasks:
 *   1. Collect the EFI memory map (GetMemoryMap) and keep it as the handoff
 *      E820-style map for the kernel (bios_map_init() equivalent).
 *   2. Mask the 8259 PICs so no timer/keyboard IRQ can fire once the
 *      firmware's interrupt handlers are no longer active.
 *   3. Exit boot services.
 *   4. Hand off to kernel64_main() (long-mode C) which runs on the
 *      firmware's still-valid identity-mapped page tables until milestone
 *      M2 installs the kernel's own 4-level paging.
 *
 * Copyright 2026. Distributed under the terms of the Fiwix License.
 */

#include <fnx/efi.h>
#include <fnx/gop.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);
void kernel64_main(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, EFI_SYSTEM_TABLE *);

/* GOP framebuffer captured by the stub, consumed later by the real kernel */
struct fnx_gop_fb fnx_gop_fb;

static void outb(unsigned short port, unsigned char val)
{
	__asm__ __volatile__("outb %0, %1" :: "a"(val), "Nd"(port));
}

static unsigned char inb(unsigned short port)
{
	unsigned char val;

	__asm__ __volatile__("inb %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static void cli(void)
{
	__asm__ __volatile__("cli");
}

static void serial_putc(char c)
{
	while(!(inb(0x3FD) & 0x20)) {	/* COM1 LSR: wait for THR empty */
	}
	outb(0x3F8, c);
}

void serial_puts(const char *s)
{
	while(*s) {
		if(*s == '\n') {
			serial_putc('\r');
		}
		serial_putc(*(s++));
	}
}

static void serial_hex(UINT64 v)
{
	static const char digits[] = "0123456789abcdef";
	char buf[17];
	int n;

	buf[16] = 0;
	for(n = 15; n >= 0; n--) {
		buf[n] = digits[v & 0x0F];
		v >>= 4;
	}
	serial_puts("0x");
	serial_puts(buf);
}

/*
 * Return the runtime address of this instruction. Used instead of taking
 * &efi_main: with -fPIC, gcc addresses a non-static global function through
 * the GOT, and ld -m i386pep resolves that GOT reference by loading FROM the
 * symbol's own address (the first bytes of the function code) instead of
 * computing its address - garbage. An explicit lea 0(%%rip) is immune.
 */
static unsigned long get_rip(void)
{
	unsigned long addr;

	__asm__ __volatile__("lea 0(%%rip), %0" : "=r"(addr));
	return addr;
}

/* early serial marker: proves the app was entered and where it was loaded */
static void debug_early(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	int n;
	unsigned char lsr;

	outb(0x3F9, 0x00);		/* COM1 IER: all irqs off */
	outb(0x3FB, 0x80);		/* LCR: DLAB on */
	outb(0x3F8, 0x03);		/* divisor low: 115200 */
	outb(0x3F9, 0x00);		/* divisor high */
	outb(0x3FB, 0x03);		/* 8N1, DLAB off */
	outb(0x3FC, 0x0B);		/* MCR: DTR + RTS + OUT2 */

	/* bounded poll, then dump the LSR value as a character (no THRE wait) */
	lsr = inb(0x3FD);
	for(n = 0; n < 1000 && !(lsr & 0x20); n++) {
		lsr = inb(0x3FD);
	}
	outb(0x3F8, 'A' + (lsr & 0x3F));
	outb(0x3F8, '\n');
	serial_puts("FNX EFI entry reached\n");
	serial_puts("ImageHandle @ ");
	serial_hex((UINT64)ImageHandle);
	serial_puts("\nSystemTable @ ");
	serial_hex((UINT64)SystemTable);
	serial_puts("\nthis code runs at ");
	serial_hex((UINT64)get_rip());
	serial_puts("\n");
}

static void mask_pic_irqs(void)
{
	/* mask all IRQs on the master (0x21) and slave (0xA1) 8259 PICs */
	outb(0x21, 0xFF);
	outb(0xA1, 0xFF);
}

static void query_gop(EFI_BOOT_SERVICES *bs)
{
	EFI_GUID gop_guid = EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_LOCATE_PROTOCOL locate;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *mode;

	fnx_gop_fb.phys_base = 0;
	gop = NULL;

	locate = (EFI_LOCATE_PROTOCOL)bs->LocateProtocol;
	if(locate(&gop_guid, NULL, (void **)&gop) != EFI_SUCCESS || !gop) {
		return;
	}
	mode = gop->Mode;
	if(!mode || !mode->Info) {
		return;
	}

	fnx_gop_fb.phys_base = (unsigned long)mode->FrameBufferBase;
	fnx_gop_fb.size = (unsigned long)mode->FrameBufferSize;
	fnx_gop_fb.width = mode->Info->HorizontalResolution;
	fnx_gop_fb.height = mode->Info->VerticalResolution;
	fnx_gop_fb.pixels_per_scanline = mode->Info->PixelsPerScanLine;
	fnx_gop_fb.pixel_format = (unsigned int)mode->Info->PixelFormat;

	serial_puts("[GOP] framebuffer ");
	serial_hex(fnx_gop_fb.phys_base);
	serial_puts(" size=");
	serial_hex(fnx_gop_fb.size);
	serial_puts(" ");
	serial_hex((UINT64)fnx_gop_fb.width);
	serial_puts("x");
	serial_hex((UINT64)fnx_gop_fb.height);
	serial_puts(" pitch=");
	serial_hex((UINT64)fnx_gop_fb.pixels_per_scanline);
	serial_puts(" fmt=");
	serial_hex((UINT64)fnx_gop_fb.pixel_format);
	serial_puts("\n");
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
	EFI_BOOT_SERVICES *bs;
	EFI_MEMORY_DESCRIPTOR *map;
	UINTN map_size, map_key, desc_size;
	UINT32 desc_ver;
	EFI_STATUS st;

	debug_early(ImageHandle, SystemTable);

	bs = SystemTable->BootServices;

	/* first call sizes the map (expected to return EFI_BUFFER_TOO_SMALL) */
	map_size = 0;
	map_key = 0;
	desc_size = 0;
	st = bs->GetMemoryMap(&map_size, NULL, &map_key, &desc_size, &desc_ver);
	if(st != EFI_BUFFER_TOO_SMALL) {
		return st;
	}

	/* add slack: the map can grow between the two calls */
	map_size += desc_size * 4;

	st = bs->AllocatePool(EfiLoaderData, map_size, (void **)&map);
	if(st != EFI_SUCCESS) {
		return st;
	}

	st = bs->GetMemoryMap(&map_size, map, &map_key, &desc_size, &desc_ver);
	if(st != EFI_SUCCESS) {
		return st;
	}

	/*
	 * From this point on no UEFI service can be called. Mask the PICs so
	 * that no hardware interrupt is delivered after the firmware's
	 * interrupt handlers are no longer guaranteed to be active.
	 */
	mask_pic_irqs();

	/* grab the GOP framebuffer before boot services go away */
	query_gop(bs);

	st = bs->ExitBootServices(ImageHandle, map_key);
	if(st != EFI_SUCCESS) {
		return st;
	}

	cli();

	kernel64_main(map, map_size, desc_size, map_key, SystemTable);

	for(;;) {
		__asm__ __volatile__("hlt");
	}
}
