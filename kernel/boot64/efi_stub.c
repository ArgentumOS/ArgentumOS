/*
 * fnx/kernel/boot64/efi_stub.c
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
#include <fnx/bootconf.h>

EFI_STATUS EFIAPI efi_main(EFI_HANDLE, EFI_SYSTEM_TABLE *);
void kernel64_main(EFI_MEMORY_DESCRIPTOR *, UINTN, UINTN, UINTN, EFI_SYSTEM_TABLE *);

/*
 * FNX: the LOW window the real kernel builds its boot structures in (page
 * pool, hash tables, page tables, per-page structs, process and fd tables,
 * video buffer). start_kernel() takes it as its `last_boot_addr`, so the
 * carve-out lands here instead of after the image.
 *
 * Why: the carve-out grows UPWARD. The loader puts the image near the top
 * of a large machine's RAM, and the top of RAM is full of small firmware
 * holes, so carving from the image's end ran the kernel's own
 * is_addr_in_bios_map() stage guards into a RESERVED hole and PANIC'ed -
 * invisibly, because early printk output is only flushed to the console
 * much later (serial.c). Every boot above ~256MB died that way.
 *
 * Why EfiLoaderData: mm64.c walks the UEFI map and only ever frees
 * Conventional/BootServices memory, so the window stays the kernel's.
 * kreal64.c then reports it AVAILABLE in the multiboot map the kernel
 * itself reads, which is what its stage guards require.
 *
 * Zero means "no window": fall back to the image's end.
 */
unsigned long fnx_boot_window_base;
unsigned long fnx_boot_window_size;

/* What the structures cost: ~19MB at 128MB of RAM, ~26MB at 512MB, ~37MB at
 * 1GB (the per-page structs and page tables grow with RAM; the process table
 * does not). Scale with RAM, bounded at both ends. */
#define FNX_WINDOW_MIN		(24UL << 20)
#define FNX_WINDOW_MAX		(128UL << 20)
#define FNX_WINDOW_ALLOC_MAX	0x10000000UL	/* ask to land below 256MB */

/* Re-take the memory map into a buffer that fits it, growing the buffer if
 * needed. On success the caller holds the CURRENT map and key. */
static void
fnx_memory_map_again(EFI_BOOT_SERVICES *bs, EFI_MEMORY_DESCRIPTOR **map,
		     UINTN *map_size, UINTN *map_key, UINTN *desc_size)
{
	UINTN size = 0, key = 0, desc = 0;
	UINT32 ver = 0;
	EFI_STATUS st;

	st = bs->GetMemoryMap(&size, NULL, &key, &desc, &ver);
	if(st != EFI_BUFFER_TOO_SMALL) {
		return;
	}
	size += desc * 4;
	if(size > *map_size) {
		EFI_MEMORY_DESCRIPTOR *bigger;

		if(bs->AllocatePool(EfiLoaderData, size, (void **)&bigger)
		   != EFI_SUCCESS) {
			return;
		}
		*map = bigger;
		*map_size = size;
	}
	st = bs->GetMemoryMap(map_size, *map, &key, &desc, &ver);
	if(st == EFI_SUCCESS) {
		*map_key = key;
		*desc_size = desc;
	}
}

static unsigned long
boot_window_pages(EFI_MEMORY_DESCRIPTOR *map, UINTN map_size, UINTN desc_size)
{
	EFI_MEMORY_DESCRIPTOR *d;
	unsigned long long usable = 0;
	unsigned long want;
	UINTN n, count;

	count = map_size / desc_size;
	for(n = 0; n < count; n++) {
		d = (EFI_MEMORY_DESCRIPTOR *)((char *)map + (n * desc_size));
		if(d->Type == EfiConventionalMemory ||
		   d->Type == EfiBootServicesCode ||
		   d->Type == EfiBootServicesData) {
			usable += (unsigned long long)d->NumberOfPages << 12;
		}
	}
	want = 16UL << 20;			/* base cost */
	want += (unsigned long)(usable >> 4);	/* + RAM/16 */
	if(want < FNX_WINDOW_MIN) {
		want = FNX_WINDOW_MIN;
	}
	if(want > FNX_WINDOW_MAX) {
		want = FNX_WINDOW_MAX;
	}
	if(want > usable / 4) {
		want = (unsigned long)(usable / 4) & ~0xFFFUL;
	}
	return want >> 12;
}

/* GOP framebuffer captured by the stub, consumed later by the real kernel */
struct fnx_gop_fb fnx_gop_fb;

/*
 * M0 (docs/design/kernel-conf-plan.md): kernel.conf (ESP boot config) read
 * by the stub before ExitBootServices and handed to the kernel through the
 * image, like fnx_gop_fb: the real kernel reads it via the high-half alias
 * (struct fnx_kconf, include/fnx/bootconf.h). The PE loader zero-fills
 * .bss, so fnx_kconf.data is zeroed at entry and fnx_kconf.size = 0 means
 * "no kernel.conf" (compiled-in defaults). The 8KB cap answers the plan's
 * buffer-sizing open item: the v1 template is ~1-2KB.
 */
struct fnx_kconf fnx_kconf;

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

static void putdec(unsigned int v)
{
	char buf[10];
	int n;

	n = 0;
	if(v == 0) {
		serial_putc('0');
		return;
	}
	while(v) {
		buf[n++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while(n) {
		serial_putc(buf[--n]);
	}
}

/*
 * M0 kernel.conf reader. Runs before the memory map is taken (any file I/O
 * allocation would invalidate the map key ExitBootServices needs). Walks
 * the loaded image's FilePath to its directory, opens 'kernel.conf' there
 * on the boot volume, and reads it (bounded by FNX_KCONF_MAX) into the
 * fnx_kconf handoff. Absent file -> silent defaults (size stays 0).
 */
static void esp_open_kernel_conf(EFI_HANDLE ImageHandle, EFI_BOOT_SERVICES *bs)
{
	EFI_GUID loaded_guid = EFI_LOADED_IMAGE_PROTOCOL_GUID;
	EFI_GUID fs_guid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
	EFI_OPEN_PROTOCOL open;
	EFI_LOADED_IMAGE_PROTOCOL *img;
	EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *vol;
	EFI_FILE_PROTOCOL *root, *f;
	EFI_DEVICE_PATH *node;
	CHAR16 path[128];
	UINTN off, rd, len;
	EFI_STATUS st;
	CHAR16 *sp;
	int n, o, got_dir;

	open = (EFI_OPEN_PROTOCOL)bs->OpenProtocol;

	img = NULL;
	if(open(ImageHandle, &loaded_guid, (void **)&img, ImageHandle, NULL,
		EFI_OPEN_PROTOCOL_GET_PROTOCOL) != EFI_SUCCESS || !img) {
		serial_puts("[kernel.conf] no LoadedImage protocol; using defaults\n");
		return;
	}

	/* build the image's directory path from its FilePath device path */
	o = 0;
	got_dir = 0;
	node = img->FilePath;
	while(node && node->Type != DEVICE_PATH_TYPE_END && node->Length >= 4) {
		if(node->Type == DEVICE_PATH_TYPE_MEDIA && node->SubType == MEDIA_FILEPATH_DP) {
			/* a FILEPATH node may hold the whole path or one component */
			sp = (CHAR16 *)((char *)node + 4);
			len = (node->Length - 4) / 2;
			if(o > 0 && path[o - 1] != (CHAR16)'\\') {
				path[o++] = (CHAR16)'\\';	/* node boundary separator */
			}
			while(len-- > 0 && *sp && o < 126) {
				path[o++] = *sp++;
			}
			got_dir = 1;
		}
		node = (EFI_DEVICE_PATH *)((char *)node + node->Length);
	}
	path[o] = 0;
	if(!got_dir || o == 0) {
		serial_puts("[kernel.conf] no file path in device path; using defaults\n");
		return;
	}

	/* drop the image filename, append 'kernel.conf' */
	for(n = o - 1; n >= 0; n--) {
		if(path[n] == (CHAR16)'\\') {
			break;
		}
	}
	if(n < 0) {
		serial_puts("[kernel.conf] unrooted image path; using defaults\n");
		return;
	}
	/* widen "kernel.conf" char-by-char: L"" literals are wchar_t (4-byte)
	 * elements on clang/Linux, not CHAR16 - reading them as CHAR16 hits
	 * the zero high half after the first character. */
	o = n + 1;
	{
		static const char kname[] = "kernel.conf";
		int k;

		for(k = 0; kname[k] && o < 127; k++) {
			path[o++] = (CHAR16)kname[k];
		}
	}
	path[o] = 0;

	vol = NULL;
	if(open(img->DeviceHandle, &fs_guid, (void **)&vol, ImageHandle, NULL,
		EFI_OPEN_PROTOCOL_GET_PROTOCOL) != EFI_SUCCESS || !vol) {
		serial_puts("[kernel.conf] boot volume has no file system; using defaults\n");
		return;
	}
	root = NULL;
	if(vol->OpenVolume(vol, &root) != EFI_SUCCESS || !root) {
		serial_puts("[kernel.conf] OpenVolume failed; using defaults\n");
		return;
	}

	f = NULL;
	st = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
	if(st != EFI_SUCCESS || !f) {
		serial_puts("[kernel.conf] kernel.conf not found (compiled-in defaults)\n");
		root->Close(root);
		return;
	}

	off = 0;
	for(;;) {
		rd = FNX_KCONF_MAX - 1 - off;
		if(rd == 0) {
			break;
		}
		st = f->Read(f, &rd, fnx_kconf.data + off);
		if(st != EFI_SUCCESS || rd == 0) {
			break;
		}
		off += rd;
	}
	f->Close(f);
	root->Close(root);

	fnx_kconf.size = (unsigned int)off;
	fnx_kconf.data[off] = 0;

	serial_puts("[kernel.conf] read ");
	putdec((unsigned int)off);
	serial_puts(" bytes\n");
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

	/* M0: kernel.conf read must happen before the memory map is taken -
	 * file I/O allocations would invalidate the ExitBootServices key. */
	/* BISECT-B: open the file but perform no Read (size stays 0) */
	esp_open_kernel_conf(ImageHandle, bs);

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

	/* Take the kernel's low boot-structures window, then re-take the map:
	 * the allocation changes it, and ExitBootServices() needs the key from
	 * the CURRENT map - a stale key drops the firmware to its shell. */
	{
		UINTN want = boot_window_pages(map, map_size, desc_size);
		EFI_PHYSICAL_ADDRESS base = FNX_WINDOW_ALLOC_MAX;

		if(bs->AllocatePages(AllocateMaxAddress, EfiLoaderData, want,
				     &base) == EFI_SUCCESS) {
			fnx_boot_window_base = (unsigned long)base;
			fnx_boot_window_size = (unsigned long)want << 12;
			fnx_memory_map_again(bs, &map, &map_size, &map_key,
					     &desc_size);
		} else {
			fnx_boot_window_base = 0;
			fnx_boot_window_size = 0;
		}
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
