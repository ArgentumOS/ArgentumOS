/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * FNArchiverWire.h — THE WIRE THE CLASSIC ARCHIVER WRITES (§62.86). INTERNAL.
 *
 * **THIS FORMAT IS OURS, AND THAT IS STATED RATHER THAN IMPLIED.** Apple's classic archiver wrote
 * `typedstream`, whose structure is not published; nothing here produces or consumes it, and nothing here
 * can — so the choice was between an undocumented format and a documented one of our own, and the second
 * is the one that can be tested, versioned and read by a human. **WHAT IS HONOURED IS THE CONTRACT, NOT
 * THE BYTES**: the pair round-trips a graph, is sequential (order and type ARE the protocol, no coercion),
 * takes ONE root object, and — the one interoperability rule Apple states in words — **a KEYED archive is
 * REFUSED by the sequential reader**, which is what the magic below makes mechanical rather than hopeful.
 *
 * LAYOUT: four magic bytes, one version byte, then exactly one root value.
 *   'F' 'N' 'A' 'R' | version(1) | value
 * A value is one tag byte and its payload. Integers are LITTLE-ENDIAN and fixed-width: a sequential
 * format whose numbers changed width with the machine would break the "architecture-independent"
 * promise the class is named for. Lengths are unsigned 32-bit for the same reason.
 */

#ifndef FOUNDATION_FNARCHIVERWIRE_H
#define FOUNDATION_FNARCHIVERWIRE_H

#include <stdint.h>
#include <string.h>

#define FNAR_VERSION 1

static const unsigned char fn_archive_magic[4] = { 'F', 'N', 'A', 'R' };

/* THE TAGS. A value's tag says how to read it and nothing else — there is no dictionary of names, which
 * is the whole difference between this wire and the keyed one. */
enum {
	FNARTagNil = 0x00,	/* the nil OBJECT: "there was nothing here" */
	FNARTagNull = 0x01,	/* NSNull, which is a value and not an absence */
	FNARTagBool = 0x02,
	FNARTagInt32 = 0x03,
	FNARTagInt64 = 0x04,
	FNARTagFloat = 0x05,
	FNARTagDouble = 0x06,
	FNARTagString = 0x07,
	FNARTagData = 0x08,
	FNARTagArray = 0x09,
	FNARTagDict = 0x0A,
	FNARTagSet = 0x0B,
	FNARTagObjectRef = 0x0C,	/* an index into the object table: the SAME object again */
	FNARTagObject = 0x0D,		/* first sight of an object: class name, then its payload */
	FNARTagBytes = 0x0E		/* the raw-bytes door, length-delimited and type-less */
};

/* --- the byte codec, header-only so the writer and the reader cannot drift apart ----------------- */
static inline void fnar_put_u8(unsigned char *p, uint8_t v) { p[0] = v; }

static inline void fnar_put_u32(unsigned char *p, uint32_t v)
{
	p[0] = (unsigned char)(v & 0xFF);
	p[1] = (unsigned char)((v >> 8) & 0xFF);
	p[2] = (unsigned char)((v >> 16) & 0xFF);
	p[3] = (unsigned char)((v >> 24) & 0xFF);
}

static inline void fnar_put_u64(unsigned char *p, uint64_t v)
{
	int i;

	for (i = 0; i < 8; i++) {
		p[i] = (unsigned char)((v >> (8 * i)) & 0xFF);
	}
}

static inline uint8_t fnar_get_u8(const unsigned char *p) { return p[0]; }

static inline uint32_t fnar_get_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t fnar_get_u64(const unsigned char *p)
{
	uint64_t v = 0;
	int i;

	for (i = 7; i >= 0; i--) {
		v = (v << 8) | (uint64_t)p[i];
	}
	return v;
}

#endif /* FOUNDATION_FNARCHIVERWIRE_H */
