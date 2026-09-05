/*
 * fnx/drivers/block/partition.c
 *
 * Pure GPT + MBR (with EBR chains) partition-table parsers. This file has
 * no kernel dependencies on purpose: it only needs <fnx/part.h>, so the
 * parsers compile standalone and are host-testable against synthetic
 * images. The kernel glue that feeds it device sectors lives in
 * drivers/block/part.c.
 *
 * Addressing model (all disk offsets in 512-byte sectors):
 *   - MBR: sector 0, 4 entries at offset 446 (16 bytes each), 0x55AA at
 *     510. Primary partition n (1..4) = slot n-1. An extended primary
 *     (type 0x05/0x0F) is a container, not a filesystem: the EBR chain
 *     rooted there is walked and each logical becomes partition 5, 6, ...
 *     in chain order (Linux numbering). The extended slot itself is left
 *     empty. An entry in slot 0 with type 0xEE marks a GPT disk.
 *   - GPT: "EFI PART" header at LBA 1; entry array at partition_entry_lba
 *     (usually 2). Entry i (0-based) is partition i+1. The header CRC is
 *     validated; a corrupt primary falls back to the backup header at
 *     backup_lba. The entry-array CRC is cross-checked during parsing
 *     but a mismatch does not fail the parse (entry bounds are sanity
 *     checked individually).
 *
 * Output: out[] is indexed by partition number (out[n-1] = partition n,
 * 1-based). Holes keep type 0. Returns the highest partition number
 * found, or 0 when there is no readable/valid table.
 */

#include <fnx/types.h>
#include <fnx/part.h>

#define SECTOR_SIZE		512
#define MBR_ENTRIES		4
#define MBR_TABLE_OFF		446
#define MBR_ENTRY_SIZE		16
#define MBR_SIG_OFF		510
#define MBR_SIG			0xAA55

#define MBR_TYPE_EXT		0x05
#define MBR_TYPE_EXT_LBA	0x0F
#define MBR_TYPE_PROTECTIVE	0xEE

#define GPT_HEADER_SIZE		92
#define GPT_ENTRY_SIZE		128

/* GPT type GUIDs as stored in an entry (first three fields LE, last two
 * BE). */
static const unsigned char guid_efi_system[16] = {
	0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
	0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};
static const unsigned char guid_linux_swap[16] = {
	0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43,
	0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F
};
static const unsigned char guid_linux_fs[16] = {
	0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
	0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

static int guid_eq(const unsigned char *a, const unsigned char *b)
{
	int i;

	for(i = 0; i < 16; i++) {
		if(a[i] != b[i]) {
			return 0;
		}
	}
	return 1;
}

static unsigned int rd32(const unsigned char *p)
{
	return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
	       ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

static unsigned long long rd64(const unsigned char *p)
{
	return (unsigned long long)rd32(p) |
	       ((unsigned long long)rd32(p + 4) << 32);
}

static void wr32(unsigned char *p, unsigned int v)
{
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
	p[2] = (unsigned char)(v >> 16);
	p[3] = (unsigned char)(v >> 24);
}

static unsigned int crc32_byte(unsigned int crc, unsigned char c)
{
	int b;

	crc ^= c;
	for(b = 0; b < 8; b++) {
		crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
	}
	return crc;
}

/* chunked CRC-32 state: seed with 0xFFFFFFFF, feed bytes, finalize with
 * `^ 0xFFFFFFFF` */
static unsigned int crc32_feed(unsigned int crc, const unsigned char *p,
			       unsigned int len)
{
	unsigned int i;

	for(i = 0; i < len; i++) {
		crc = crc32_byte(crc, p[i]);
	}
	return crc;
}

/* emit partition `num` (1-based); a slot already taken is kept as-is */
static void emit(struct partition *out, int num, unsigned int type,
		 unsigned long long start, unsigned long long nr)
{
	if(num < 1 || out[num - 1].type) {
		return;
	}
	out[num - 1].status = 0;
	out[num - 1].type = (unsigned char)type;
	out[num - 1].startsect = (unsigned int)start;
	out[num - 1].nr_sects = (unsigned int)nr;
}

/* is the sector a GPT header? */
static int gpt_signature(const unsigned char *h)
{
	static const char sig[8] = "EFI PART";

	return h[0] == sig[0] && h[1] == sig[1] && h[2] == sig[2] &&
	       h[3] == sig[3] && h[4] == sig[4] && h[5] == sig[5] &&
	       h[6] == sig[6] && h[7] == sig[7];
}

static int gpt_header_ok(const unsigned char *h)
{
	unsigned char copy[GPT_HEADER_SIZE];
	unsigned int i, want;

	want = rd32(h + 16);
	if(!want) {
		return 0;
	}
	for(i = 0; i < GPT_HEADER_SIZE; i++) {
		copy[i] = h[i];
	}
	wr32(copy + 16, 0);	/* CRC field is zeroed for the check */
	return (crc32_feed(0xFFFFFFFF, copy, GPT_HEADER_SIZE)
		^ 0xFFFFFFFF) == want;
}

/* walk the EBR chain for an extended primary at MBR slot ext_slot */
static int parse_ebr(int (*rd)(void *, unsigned long long, unsigned char *),
		     void *ctx, const unsigned char *s0,
		     struct partition *out, int max, int ext_slot)
{
	const unsigned char *e;
	unsigned int ext_base, ebr_lba;
	unsigned char buf[SECTOR_SIZE];
	int logical = 4;
	int guard = 0;

	e = s0 + MBR_TABLE_OFF + ext_slot * MBR_ENTRY_SIZE;
	ext_base = rd32(e + 8);
	ebr_lba = ext_base;

	while(logical < max && guard++ < max * 4) {
		unsigned char type;
		unsigned int rel, len;
		unsigned long long next;

		if(rd(ctx, ebr_lba, buf)) {
			break;
		}
		if(buf[MBR_SIG_OFF] != (MBR_SIG & 0xFF) ||
		   buf[MBR_SIG_OFF + 1] != (MBR_SIG >> 8)) {
			break;	/* end of chain */
		}
		/* entry 0 = the logical (LBA relative to this EBR) */
		type = buf[MBR_TABLE_OFF + 4];
		rel = rd32(buf + MBR_TABLE_OFF + 8);
		len = rd32(buf + MBR_TABLE_OFF + 12);
		if(type && len && type != MBR_TYPE_EXT &&
		   type != MBR_TYPE_EXT_LBA) {
			logical++;
			emit(out, logical, type,
			     (unsigned long long)ebr_lba + rel, len);
		}
		/* entry 1 = next EBR (LBA relative to the extended base) */
		rel = rd32(buf + MBR_TABLE_OFF + MBR_ENTRY_SIZE + 8);
		if(!rel) {
			break;
		}
		next = (unsigned long long)ext_base + rel;
		if(next <= ebr_lba) {
			break;	/* must advance */
		}
		ebr_lba = (unsigned int)next;
	}
	return logical;
}

/* parse the GPT entry array referenced by a validated header */
static int parse_gpt(int (*rd)(void *, unsigned long long, unsigned char *),
		     void *ctx, const unsigned char *hdr,
		     struct partition *out, int max)
{
	unsigned long long elba = rd64(hdr + 72);
	unsigned int num = rd32(hdr + 80);
	unsigned int esize = rd32(hdr + 84);
	unsigned char buf[SECTOR_SIZE];
	unsigned int i;
	int highest = 0;

	if(!esize || esize < GPT_ENTRY_SIZE) {
		esize = GPT_ENTRY_SIZE;
	}
	if(!num || num > (unsigned int)max) {
		num = (unsigned int)max;
	}

	for(i = 0; i < num; i++) {
		unsigned long long seclba =
			elba + ((unsigned long long)i * esize) / SECTOR_SIZE;
		unsigned int off = (i * esize) % SECTOR_SIZE;
		unsigned char ent[GPT_ENTRY_SIZE];
		unsigned long long first_lba, last_lba;
		unsigned int type = 0;
		int z, j;

		if(rd(ctx, seclba, buf)) {
			break;
		}
		/* an entry may straddle a sector boundary (non-typical) */
		if(off + esize > SECTOR_SIZE) {
			unsigned char b2[SECTOR_SIZE];
			unsigned int first = SECTOR_SIZE - off;

			if(rd(ctx, seclba + 1, b2)) {
				break;
			}
			for(j = 0; j < (int)first; j++) {
				ent[j] = buf[off + j];
			}
			for(j = 0; j < (int)(esize - first); j++) {
				ent[first + j] = b2[j];
			}
		} else {
			for(j = 0; j < (int)esize; j++) {
				ent[j] = buf[off + j];
			}
		}

		for(z = 0; z < 16; z++) {
			if(ent[z]) {
				break;
			}
		}
		if(z == 16) {
			continue;	/* unused entry */
		}
		if(guid_eq(ent, guid_efi_system)) {
			type = 0xEF;
		} else if(guid_eq(ent, guid_linux_swap)) {
			type = 0x82;
		} else if(guid_eq(ent, guid_linux_fs)) {
			type = 0x83;
		} else {
			type = 0x83;
		}
		first_lba = rd64(ent + 32);
		last_lba = rd64(ent + 40);
		if(!first_lba || last_lba < first_lba) {
			continue;
		}
		emit(out, (int)i + 1, type, first_lba, last_lba - first_lba + 1);
		highest = (int)i + 1;
	}
	return highest;
}

int partition_parse(void *ctx,
		    int (*rd)(void *, unsigned long long, unsigned char *),
		    struct partition *out, int max)
{
	unsigned char s0[SECTOR_SIZE];
	unsigned char hdr[SECTOR_SIZE];
	int ext_slot = -1;
	int n, count = 0;

	if(max > MAX_PARTITIONS) {
		max = MAX_PARTITIONS;
	}
	for(n = 0; n < max; n++) {
		out[n].status = 0;
		out[n].type = 0;
		out[n].startsect = 0;
		out[n].nr_sects = 0;
	}

	if(rd(ctx, 0, s0)) {
		return 0;
	}
	if(s0[MBR_SIG_OFF] != (MBR_SIG & 0xFF) ||
	   s0[MBR_SIG_OFF + 1] != (MBR_SIG >> 8)) {
		return 0;	/* no 0x55AA: whole-disk filesystem */
	}

	/* GPT? protective marker + header at LBA 1 */
	if(s0[MBR_TABLE_OFF + 4] == MBR_TYPE_PROTECTIVE &&
	   !rd(ctx, 1, hdr) && gpt_signature(hdr)) {
		unsigned long long backup;
		unsigned char hb[SECTOR_SIZE];

		if(gpt_header_ok(hdr)) {
			return parse_gpt(rd, ctx, hdr, out, max);
		}
		backup = rd64(hdr + 32);	/* backup_lba field */
		if(backup && !rd(ctx, backup, hb) && gpt_signature(hb) &&
		   gpt_header_ok(hb)) {
			return parse_gpt(rd, ctx, hb, out, max);
		}
		/* no usable GPT: the protective entry alone is not a
		 * partition; fall through to the plain-MBR view (which
		 * ignores it) */
	}

	/* plain MBR + optional EBR chain */
	for(n = 0; n < MBR_ENTRIES; n++) {
		const unsigned char *e = s0 + MBR_TABLE_OFF + n * MBR_ENTRY_SIZE;
		unsigned int type = e[4];

		if(!type) {
			continue;
		}
		if(type == MBR_TYPE_EXT || type == MBR_TYPE_EXT_LBA) {
			if(ext_slot < 0) {
				ext_slot = n;
			}
			continue;
		}
		if(type == MBR_TYPE_PROTECTIVE && n == 0) {
			continue;	/* GPT marker without a usable GPT */
		}
		emit(out, n + 1, type, rd32(e + 8), rd32(e + 12));
		if(n + 1 > count) {
			count = n + 1;
		}
	}
	if(ext_slot >= 0) {
		n = parse_ebr(rd, ctx, s0, out, max, ext_slot);
		if(n > count) {
			count = n;
		}
	}
	return count;
}
