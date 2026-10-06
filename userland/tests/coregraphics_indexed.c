/*
 * coregraphics_indexed — the indexed colour space, its table, and its base.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE TABLE GOES IN AND COMES OUT UNCHANGED: Apple's header describes the array as "m * (lastIndex + 1) BYTES"
 * and says the getter returns "the same format as that passed to CGColorSpaceCreateIndexed", so the check is a
 * byte comparison rather than a shape check.
 *
 * THE COUNT IS ENTRIES AND NOT BYTES, which is the off-by-one this family invites: a table of four bytes with
 * `lastIndex` 3 has FOUR entries, and a check on the byte count alone would pass for either reading.
 *
 * AND THE DOOR THAT ANSWERS FOR TWO KINDS OF SPACE IS CHECKED AGAINST A THIRD THAT HAS NO BASE AT ALL: a device
 * space's base is NULL, its table count is 0, and asking it for a table leaves the caller's buffer alone.
 */
#include <CoreGraphics/CGColorSpace.h>

#include <stdio.h>
#include <string.h>

static int failures;

static void check(const char *name, int ok)
{
	printf("CG-INDEXED %-64s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

int main(void)
{
	/* four grey levels, one byte each because the base space has one component */
	static const uint8_t table[4] = { 0x00, 0x40, 0x80, 0xFF };
	CGColorSpaceRef base = CGColorSpaceCreateDeviceGray();
	CGColorSpaceRef indexed = CGColorSpaceCreateIndexed(base, 3, table);
	uint8_t out[8];

	check("an indexed space is made over a base", indexed != NULL);
	check("...and its model says so, which is how Apple's header says to ask",
	      CGColorSpaceGetModel(indexed) == kCGColorSpaceModelIndexed);
	check("...and its one component is the index",
	      CGColorSpaceGetNumberOfComponents(indexed) == 1);
	check("...and its base is the one it was given", CGColorSpaceGetBaseColorSpace(indexed) == base);

	memset(out, 0xEE, sizeof out);
	check("the table's count is ENTRIES, which is lastIndex + 1", CGColorSpaceGetColorTableCount(indexed) == 4);
	CGColorSpaceGetColorTable(indexed, out);
	check("...and the table comes out exactly as it went in, byte for byte",
	      memcmp(out, table, sizeof table) == 0);
	check("...and nothing beyond the table was written", out[4] == 0xEE && out[5] == 0xEE);
	printf("CG-INDEXED %-64s table=%02x,%02x,%02x,%02x beyond=%02x\n", "...readout", out[0], out[1],
	       out[2], out[3], out[4]);

	/* --- the refusals ------------------------------------------------------------------------- */
	check("a maximum index above 255 is refused, in Apple's own words",
	      CGColorSpaceCreateIndexed(base, 256, table) == NULL);
	check("...as is no table at all", CGColorSpaceCreateIndexed(base, 0, NULL) == NULL);
	check("...and no base at all", CGColorSpaceCreateIndexed(NULL, 0, table) == NULL);
	check("...and a base that is itself indexed, whose values would be indices into a second table",
	      CGColorSpaceCreateIndexed(indexed, 0, table) == NULL);

	/* --- and a space with no base answers for none of it ---------------------------------------- */
	{
		uint8_t untouched[4];

		memset(untouched, 0xAB, sizeof untouched);
		check("a device space has no base", CGColorSpaceGetBaseColorSpace(base) == NULL);
		check("...and no colour table", CGColorSpaceGetColorTableCount(base) == 0);
		CGColorSpaceGetColorTable(base, untouched);
		check("...and asking it for one DOES NOTHING, which is what Apple's header says",
		      untouched[0] == 0xAB && untouched[3] == 0xAB);
	}

	/* THE RELEASE PATH THE NEW RULE GOVERNS: an indexed space is heap-allocated with no profile, and
	 * releasing it has to free the table and the base it holds without touching the singleton it points at.
	 * A LEAK CANNOT BE SEEN FROM IN HERE; a wrong static list would corrupt every probe in this gate, which
	 * is the half that is actually checked. */
	{
		CGColorSpaceRef over_cmyk = CGColorSpaceCreateIndexed(CGColorSpaceCreateDeviceCMYK(), 1,
								     (const uint8_t[]){ 1, 2, 3, 4, 5, 6, 7, 8 });
		CGColorSpaceRef one_more = CGColorSpaceCreateIndexed(base, 0, table);

		check("an indexed space over a CMYK base keeps the table's byte count from the base's components",
		      over_cmyk != NULL && CGColorSpaceGetColorTableCount(over_cmyk) == 2);
		CGColorSpaceRelease(over_cmyk);
		CGColorSpaceRelease(one_more);
		check("...and releasing them leaves the base usable, so the singleton was not freed under it",
		      CGColorSpaceGetNumberOfComponents(base) == 1
		      && CGColorSpaceGetModel(base) == kCGColorSpaceModelMonochrome
		      && CGColorSpaceGetColorTableCount(indexed) == 4);
	}

	CGColorSpaceRelease(indexed);
	CGColorSpaceRelease(base);
	printf("CG-INDEXED: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
