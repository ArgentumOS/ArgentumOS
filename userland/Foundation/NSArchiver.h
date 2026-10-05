/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSArchiver and NSUnarchiver — THE CLASSIC, SEQUENTIAL ARCHIVER PAIR (§62.86), and with it
 * `NXReadNSObjectFromCoder`. This closes `Files and Data Persistence / Deprecated`.
 *
 * SEQUENTIAL IS THE WHOLE POINT: a value is written with a TYPE and read back in the SAME ORDER with the
 * same type — there are no keys and no coercion. That is what makes this pair a *different* format from
 * `NSKeyedArchiver`/`NSKeyedUnarchiver` rather than an older spelling of it, and the two cannot be
 * confused: **each family implements its own doors and RAISES in the other's** (see NSCoder.h), and the
 * sequential reader REFUSES a keyed archive outright.
 *
 * THE WIRE IS OURS, AND THAT IS STATED RATHER THAN IMPLIED. Apple's classic archiver wrote `typedstream`,
 * whose structure is not published; this library neither produces nor consumes it. What is honoured is
 * the CONTRACT (a round-tripping, sequential, single-root archive) and the one interoperability rule
 * Apple states in words — a keyed archive cannot be read here. The layout is in `FNArchiverWire.h`,
 * internal and documented, because a format nobody can read is a format nobody can test.
 *
 * WHAT IS ABSENT, NAMED AND WITH ITS GROUND:
 *   * `-objectZone`/`-setObjectZone:` — NOT DECLARED, because THIS LIBRARY HAS NO `NSZone` AT ALL
 *     (NSObjCRuntime.h records the user-driven removal of 2026-09-18: zones are 32-bit-era API). A door
 *     cannot be declared over a type the library does not have, so the absence is the honest form.
 *   * class-name translation (`-encodeClassName:intoClassName:` and its three siblings) — NOT DECLARED,
 *     which is the SAME REFUSAL `NSKeyedArchiver.h` already records for its own equivalent
 *     (`-setClassName:forClass:`). Two archivers in one library that both say no to one feature is a
 *     decision; one that says yes while the other says no would be an accident.
 *   * `-versionForClassName:` IS declared (it is on the base) and RAISES here, because this wire records
 *     no class versions — answering a number that was never written would be a fabrication.
 *
 * AN OBJECT THAT IS NOT `NSCoding` IS REFUSED (NSInconsistentArchiveException), and so is a class whose
 * `-encodeWithCoder:` reaches for the KEYED doors: that is not a defect of this pair, it is what
 * "sequential" means — a class written against keys has no order to fall back on.
 */

#import <Foundation/NSCoder.h>

NS_ASSUME_NONNULL_BEGIN

@class NSData;
@class NSMutableData;
@class NSString;

@interface NSArchiver : NSCoder
{
@private
	id _data;		/* the NSMutableData the caller supplied, retained */
	id _objects;		/* the object table: the WIRE's indices live here */
	id _replaced;		/* sources, in the order they were registered */
	id _targets;		/* their replacements, index for index */
	BOOL _rootEncoded;
}

/* ONE ROOT, AND THE DATA COMES BACK. Both write a header before any value, so a reader can tell this
 * format from another one; `+archiveRootObject:toFile:` answers NO if the file could not be written. */
+ (NSData *)archivedDataWithRootObject:(id)rootObject;
+ (BOOL)archiveRootObject:(id)rootObject toFile:(NSString *)path;

/* A nil `data` is a PROGRAMMING ERROR, not an empty archive, and it raises `NSInvalidArgumentException`
 * (Apple's own contract). */
- (instancetype)initForWritingWithMutableData:(nullable NSMutableData *)data;

/* WRITE THE GRAPH. Sending this TWICE raises `NSInvalidArgumentException`: one archive holds one root, and
 * a second root would be a second archive wearing the first one's header. */
- (void)encodeRootObject:(id)rootObject;

/* The data this archiver was given — the SAME object, which is why a caller can read it back after
 * `-encodeRootObject:` returns. */
- (NSMutableData *)archiverData;

/* Object substitution on the WRITING side: `object` is written out as `newObject`. A nil replacement
 * means the object is written as nil, which is a value here. */
- (void)replaceObject:(nullable id)object withObject:(nullable id)newObject;

@end

@interface NSUnarchiver : NSCoder
{
@private
	id _data;		/* the NSData being read, retained */
	NSUInteger _pos;	/* where the next byte is */
	id _objects;		/* the object table, built as values are decoded */
	id _replaced;		/* substitution sources */
	id _targets;		/* their replacements */
}

/* AN INVALID ARCHIVE ANSWERS nil RATHER THAN RAISING, because "this is not an archive of mine" is a value
 * a caller can act on — but a nil ARGUMENT is a programming error and raises, as on the writing side. */
+ (nullable id)unarchiveObjectWithData:(NSData *)data;
+ (nullable id)unarchiveObjectWithFile:(NSString *)path;
- (nullable instancetype)initForReadingWithData:(nullable NSData *)data;

/* Whether the reader is at the end of the archive — the door that tells a caller whether the object it
 * just decoded was the WHOLE archive. */
- (BOOL)isAtEnd;

/* Object substitution on the READING side: whenever a value EQUAL TO `object` is decoded from the archive,
 * `replacement` is handed back instead. **THE MATCH IS BY EQUALITY HERE AND BY IDENTITY ON THE WRITING
 * SIDE** — inherent rather than chosen: outbound the caller holds the instance it substitutes, inbound the
 * object does not exist until it has been decoded, so a caller can only name what it is equal to. */
- (void)replaceObject:(nullable id)object withObject:(nullable id)newObject;

@end

/* Reads one object from a coder. APPLE DECLARES THIS IN `NSSerialization.h`, A HEADER THIS LIBRARY DOES
 * NOT SHIP: it is declared here, beside the pair it belongs to, rather than in a header created to hold
 * one line. It is the legacy free-function spelling of `-decodeObject` and does nothing else. */
id _Nullable NXReadNSObjectFromCoder(NSCoder *coder);

NS_ASSUME_NONNULL_END
