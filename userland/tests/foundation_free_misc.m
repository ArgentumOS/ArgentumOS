/*
 * foundation_free_misc.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FREE FUNCTIONS OF §62.52: the stragglers in Apple's `NSObjCRuntime.h` and `NSGeometry.h` that this library did
 * not have — logging, the page arithmetic, what the machine has, a range from its own description, the extra
 * retain count, the stack doors, the geometry predicates, and the four garbage-collector doors.
 *
 * EVERY CHECK HERE IS AN ABSOLUTE ONE OR A RELATION, NEVER "SOMETHING CHANGED". A page size must BE a power of two
 * and its logarithm must agree with it; a rounding must move a non-multiple and leave a multiple alone; a rectangle
 * that only touches must answer NO; a point on the minimum edge must be inside and one on the maximum edge outside,
 * in both coordinate systems. Two of the checks are the kind this thread keeps needing: THE STACK DOORS ARE ASKED
 * FOR LEVELS THEY CANNOT HONOUR and must answer NULL rather than read a frame that is not there, and `NSLog` is
 * measured BY REDIRECTING STANDARD ERROR to a file and reading back what arrived — a log function that wrote
 * nowhere would pass any check that only called it.
 *
 * NOTHING HERE IS RELEASED: a probe compiles under ARC, the library does not.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-FREEFUNCS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FREEFUNCS %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGPIPE, SIG_IGN);

	/* --- PAGES: THE SIZE, ITS LOGARITHM, AND THE ARITHMETIC --------------------------------------------- */
	{
		NSUInteger page = NSPageSize();
		NSUInteger logPage = NSLogPageSize();
		NSUInteger power = 1;
		NSUInteger bits = 0;
		NSUInteger down = NSRoundDownToMultipleOfPageSize(page * 3 + 137);
		NSUInteger up = NSRoundUpToMultipleOfPageSize(page * 3 + 137);

		while (power < page) {
			power <<= 1;
			bits++;
		}
		check("the-page-size-is-a-power-of-two-and-log-page-size-is-its-logarithm",
		      page >= 512 && power == page && logPage == bits,
		      [NSString stringWithFormat:@"NSPageSize() = %lu, a power of two, and NSLogPageSize() = %lu against "
			@"the %lu its own definition gives", (unsigned long)page, (unsigned long)logPage,
			(unsigned long)bits]);

		check("rounding-moves-a-non-multiple-and-leaves-a-multiple-alone",
		      down == page * 3 && up == page * 4 &&
		      NSRoundDownToMultipleOfPageSize(page * 4) == page * 4 &&
		      NSRoundUpToMultipleOfPageSize(page * 4) == page * 4 &&
		      down < page * 3 + 137 && up > page * 3 + 137,
		      [NSString stringWithFormat:@"%lu -> down %lu, up %lu; a multiple %lu is left alone both ways",
			(unsigned long)(page * 3 + 137), (unsigned long)down, (unsigned long)up, (unsigned long)(page * 4)]);
	}

	/* --- WHAT THE MACHINE HAS --------------------------------------------------------------------------- */
	{
		NSUInteger memory = NSRealMemoryAvailable();

		check("the-real-memory-available-is-a-plausible-whole-number-of-pages",
		      memory > (32UL * 1024UL * 1024UL) && (memory % NSPageSize()) == 0,
		      [NSString stringWithFormat:@"NSRealMemoryAvailable() = %lu bytes, a whole number of pages",
			(unsigned long)memory]);
	}

	/* --- A RANGE FROM ITS OWN DESCRIPTION ---------------------------------------------------------------- */
	{
		NSRange parsed = NSRangeFromString(@"{3, 7}");
		NSRange written = NSRangeFromString([NSString stringWithFormat:@"{%lu, %lu}",
							(unsigned long)11, (unsigned long)5]);
		NSRange nonsense = NSRangeFromString(@"this is not a range");

		check("a-range-comes-back-from-its-own-description-and-nonsense-is-empty",
		      parsed.location == 3 && parsed.length == 7 &&
		      written.location == 11 && written.length == 5 &&
		      nonsense.location == 0 && nonsense.length == 0,
		      [NSString stringWithFormat:@"\"{3, 7}\" -> {%lu, %lu}; \"{11, 5}\" -> {%lu, %lu}; nonsense -> "
			@"{%lu, %lu}", (unsigned long)parsed.location, (unsigned long)parsed.length,
			(unsigned long)written.location, (unsigned long)written.length,
			(unsigned long)nonsense.location, (unsigned long)nonsense.length]);
	}

	/* --- THE RECTANGLE PREDICATES ------------------------------------------------------------------------ */
	{
		NSRect a = NSMakeRect(0, 0, 100, 100);
		NSRect overlapping = NSMakeRect(50, 50, 100, 100);
		NSRect touching = NSMakeRect(100, 0, 100, 100);
		NSRect apart = NSMakeRect(200, 200, 10, 10);
		NSRect inside = NSMakeRect(10, 10, 10, 10);

		check("touching-is-not-intersecting-but-overlapping-and-contained-are",
		      NSIntersectsRect(a, overlapping) && !NSIntersectsRect(a, touching) &&
		      !NSIntersectsRect(a, apart) && NSIntersectsRect(a, inside) &&
		      NSIntersectsRect(a, a),
		      @"overlap YES, shared edge NO, separate NO, contained YES");

		{
			/* THE MINIMUM EDGE IS INSIDE AND THE MAXIMUM EDGE IS NOT, and the flag decides WHICH pair that is,
			 * so the same corner answers the same way in both coordinate systems. */
			BOOL plainMinX = NSMouseInRect(NSMakePoint(0, 50), a, NO);
			BOOL plainMaxX = NSMouseInRect(NSMakePoint(100, 50), a, NO);
			BOOL plainMinY = NSMouseInRect(NSMakePoint(50, 0), a, NO);
			BOOL plainMaxY = NSMouseInRect(NSMakePoint(50, 100), a, NO);
			BOOL flipMinX = NSMouseInRect(NSMakePoint(0, 50), a, YES);
			BOOL flipMaxX = NSMouseInRect(NSMakePoint(100, 50), a, YES);
			BOOL middle = NSMouseInRect(NSMakePoint(50, 50), a, NO) &&
				      NSMouseInRect(NSMakePoint(50, 50), a, YES);

			check("a-point-on-the-minimum-edge-is-inside-and-one-on-the-maximum-edge-is-not",
			      plainMinX && !plainMaxX && plainMinY && !plainMaxY &&
			      flipMinX && !flipMaxX && middle,
			      [NSString stringWithFormat:@"unflipped: min-x %d max-x %d min-y %d max-y %d; flipped: min-x %d "
				@"max-x %d; the middle is in both %d", (int)plainMinX, (int)plainMaxX, (int)plainMinY,
				(int)plainMaxY, (int)flipMinX, (int)flipMaxX, (int)middle]);
		}
	}

	/* --- THE EXTRA RETAIN COUNT ------------------------------------------------------------------------- */
	{
		NSObject *object = [[NSObject alloc] init];
		NSUInteger before;
		NSUInteger after;
		/* THE RESULT IS HELD UNRETAINED, AND THAT IS THE WHOLE POINT OF THIS CHECK: the door answers the object,
		 * so ARC would retain a strong `id` result and the COUNT — which is the subject here — would rise by
		 * ARC's retain rather than by the call's. The first version of this check read 2 instead of 1 for exactly
		 * that reason, and the message printed the number rather than the diagnosis. */
		__unsafe_unretained id returned;
		BOOL notZero = NO;

		before = NSExtraRefCount(object);
		returned = NSIncrementExtraRefCount(object);
		after = NSExtraRefCount(object);

		notZero = !NSDecrementExtraRefCountWasZero(object);

		check("the-extra-count-is-the-retain-count-beyond-the-basic-one",
		      before == 0 && after == 1 && returned == object && notZero,
		      [NSString stringWithFormat:@"a fresh object has %lu extra, one increment takes it to %lu, the door "
			@"answers the object, and decrementing from two reports that it was NOT the last",
			(unsigned long)before, (unsigned long)after]);
	}

	/* --- THE STACK DOORS, INCLUDING THE LEVELS THEY CANNOT HONOUR ---------------------------------------- */
	{
		void *frame = NSFrameAddress(0);
		void *caller = NSReturnAddress(0);

		check("the-stack-doors-answer-level-zero-and-refuse-what-they-cannot-honour",
		      frame != NULL && caller != NULL &&
		      NSFrameAddress(1) == NULL && NSFrameAddress(99) == NULL && NSReturnAddress(7) == NULL,
		      @"level zero answers, and beyond it the doors answer NULL rather than read a frame that may not "
		      @"be there — which is why NSCountFrames is not here at all");
	}

	/* --- THE GARBAGE-COLLECTOR DOORS: THE DOCUMENTED BEHAVIOUR WITHOUT A COLLECTOR ------------------------ */
	{
		NSObject *object = [[NSObject alloc] init];
		char *buffer = (char *)malloc(16);
		BOOL held;

		memset(buffer, 'a', 16);
		buffer = (char *)NSReallocateCollectable(buffer, 64, 0);
		buffer[63] = 'z';
		NSRecordAllocationEvent(0, object);

		held = NSMakeCollectable(object) == object && buffer != NULL && buffer[0] == 'a' && buffer[63] == 'z' &&
		       !NSIsFreedObject(object);
		check("the-collector-doors-are-the-documented-behaviour-for-an-uncollected-program",
		      held,
		      @"an object IS collectable, a reallocation keeps what it moved, nothing is reported freed, and "
		      @"recording an event is a no-op");
		free(buffer);
	}

	/* --- A LOG LINE REALLY REACHES STANDARD ERROR -------------------------------------------------------- */
	{
		const char *path = "/System/Temporary Files/foundation-free-misc-nslog.txt";
		int saved = dup(2);
		int fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
		char text[512];
		ssize_t got = 0;
		BOOL wrote;

		text[0] = '\0';
		if (fd >= 0) {
			(void)dup2(fd, 2);
		}
		NSLog(@"the probe wrote %d and this", 42);
		if (fd >= 0) {
			(void)dup2(saved, 2);
			close(fd);
			(void)close(saved);
		}
		fd = open(path, O_RDONLY);
		if (fd >= 0) {
			got = read(fd, text, sizeof text - 1);
			close(fd);
		}
		if (got > 0) {
			text[got] = '\0';
		}
		wrote = got > 0 && strstr(text, "the probe wrote 42 and this") != NULL &&
			strstr(text, "[") != NULL;

		check("a-log-line-reaches-standard-error-with-its-arguments-formatted",
		      wrote,
		      [NSString stringWithFormat:@"standard error received %ld byte(s): %@", (long)got,
			got > 0 ? [NSString stringWithUTF8String:text] : @"(nothing)"]);
		(void)unlink(path);
	}

	printf("FOUNDATION-FREEFUNCS RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FREEFUNCS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-FREEFUNCS DONE\n");
	return failc ? 1 : 0;
}
