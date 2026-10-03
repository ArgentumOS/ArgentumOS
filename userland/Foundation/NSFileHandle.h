/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFileHandle — A DESCRIPTOR WITH AN OBJECT ROUND IT. docs/design/foundation-plan.md W6c and §44.
 *
 * THE FAMOUS HALF OF THIS CLASS IS THE DEPRECATED ONE, and that shapes this header more than anything else.
 * In the macOS 14 vintage Apple deprecated the nine methods everybody knows — `-readDataToEndOfFile`,
 * `-readDataOfLength:`, `-writeData:`, `-offsetInFile`, `-seekToEndOfFile`, `-seekToFileOffset:`,
 * `-closeFile`, `-synchronizeFile`, `-truncateFileAtOffset:`. **⚠⚠ AND THIS PARAGRAPH USED TO SAY "SO §11.5
 * STRIKES THEM", WHICH BECAME FALSE ON 2026-09-26** (the user's policy retired the deprecation ground): a
 * deprecated door is a PORTING TARGET here, so the ledger counts all nine as OWED — measured, they are `open`
 * rows on this class. **What replaced them is the same operations carrying an error out-parameter**
 * (`-readDataUpToLength:error:` and friends), which is the whole of the synchronous surface below; the old
 * spellings are therefore work this class still owes, and the probe's inventory names each one with that
 * status rather than with a strike.
 *
 * THE ASYNCHRONOUS HALF IS **NOT** DEPRECATED, and it is why this class needed W6a and W6b before it:
 * `-readInBackgroundAndNotify`, `-readToEndOfFileInBackgroundAndNotify`, `-waitForDataInBackgroundAndNotify`,
 * `-acceptConnectionInBackgroundAndNotify` and the two handler doors are all current API, and every one of
 * them is **a descriptor registered with a run loop**. The seam W6a added is what they register with, and
 * NSPort (§43) is the other way to say the same thing.
 *
 * FOUR THINGS TO KNOW BEFORE USING IT — three are Apple's contracts and the fourth is ours:
 *
 *   * **A BACKGROUND READ IS ONE-SHOT, IN APPLE'S OWN WORDS:** *"this method does not cause a continuous
 *     stream of notifications to be sent. If you wish to keep getting notified, you'll also need to call
 *     `readInBackgroundAndNotify` in your observer method."* So the source is REMOVED when it fires and the
 *     observer re-arms. `-readToEndOfFileInBackgroundAndNotify` and `-waitForDataInBackgroundAndNotify`
 *     follow the same rule here **by parity** — Apple's page for the read family states it and the other two
 *     pages do not, which is a choice of ours stated rather than hidden.
 *   * **`-initWithFileDescriptor:` DOES NOT OWN THE DESCRIPTOR** — *"you're responsible for closing the file
 *     descriptor"* — while `-initWithFileDescriptor:closeOnDealloc:` with YES does, and every `+fileHandle…`
 *     creator does. NSPort drew the same line in §43.
 *   * **A HANDLER IS NOT ONE-SHOT:** `-readabilityHandler` is called whenever the descriptor is readable
 *     until it is set to nil, which is Apple's *"typically executed repeatedly until the entire contents of
 *     the file have been read"*. Apple describes the mechanism as a *dispatch source*; there is no GCD in
 *     this tree, so OURS IS THE RUN LOOP, and the handler runs on the thread whose loop monitors it.
 *   * **AND ONE IS OURS: `-acceptConnectionInBackgroundAndNotify` CANNOT FIRE ON THIS SYSTEM AT ALL.**
 *     `select(2)` does not report a LISTENING descriptor as readable — §43 measured it, with `accept(2)`
 *     returning the connection in the same run — so the method is implemented exactly as Apple describes and
 *     is USELESS HERE. That is written down rather than left for a caller to discover: accepting connections
 *     on this system means polling `accept(2)`, which is what `NSSocketPort`'s own check does.
 *
 *   * **AND A REGULAR FILE'S DESCRIPTOR IS NOT A USABLE SOURCE ON THIS SYSTEM EITHER.** `select(2)`
 *     here answers "not readable" for a file that has data waiting (measured: `r=0` for an open file at
 *     offset 0 — the same shape as the listening-socket gap §43 found), so the four background operations
 *     and both handlers FIRE ON PIPES AND SOCKETS and NEVER ON A FILE. Apple's own
 *     `-readabilityHandler` page describes reading a file to the end, which is the one thing this system
 *     cannot do that way: the file is still fully usable SYNCHRONOUSLY, and the probe pins that side.
 *
 * ARCHIVING REFUSES IN BOTH DIRECTIONS, as NSPort's does and for the same kind of reason: Apple documents
 * that a file handle conforms to `NSCoding` and `NSSecureCoding` and publishes **no wire format** for one,
 * so a handle written here could not be read by anything else. Archive the PATH, or the descriptor's
 * meaning, and make a handle from that.
 *
 * ERRORS ARE `NSPOSIXErrorDomain` WITH THE `errno` THAT CAUSED THEM, which is this tree's existing answer
 * for the file family (NSFileManager.m does the same) rather than a Cocoa error code this tree does not
 * carry. `+fileHandleWithNullDevice` answers a handle on `/dev/null`.
 */

#ifndef FOUNDATION_NSFILEHANDLE_H
#define FOUNDATION_NSFILEHANDLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>
#include <stdint.h>

@class NSData;
@class NSError;
@class NSArray;
@class NSMutableArray;	/* the operation list ivar */
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* Posted when `-acceptConnectionInBackgroundAndNotify` accepts, with the NEW handle for the near end under
 * `NSFileHandleNotificationFileHandleItem`. */
extern NSString *const NSFileHandleConnectionAcceptedNotification;

/* Posted by `-waitForDataInBackgroundAndNotify` when data becomes available. No userInfo. */
extern NSString *const NSFileHandleDataAvailableNotification;

/* Posted by `-readInBackgroundAndNotify` and `-readToEndOfFileInBackgroundAndNotify`, with the data read
 * under `NSFileHandleNotificationDataItem`. */
extern NSString *const NSFileHandleReadCompletionNotification;
extern NSString *const NSFileHandleReadToEndOfFileCompletionNotification;

/* THE USERINFO KEYS, and there are exactly two in Apple's documented surface: the data a read produced and
 * the handle an accept produced. (There is no documented error key, so there is not one here either.) */
extern NSString *const NSFileHandleNotificationDataItem;
/* THE RUN-LOOP MODES THE BACKGROUND MONITOR RUNS IN (§62.103). THE CONSTANT AND THE BEHAVIOUR ARE ONE: the
 * notification half asks the run loop in THIS array, so a caller that wanted to know which modes would have
 * to read the implementation before — now it can read the array. It is NOT pointer-const because a global
 * array object cannot be built before the runtime exists: a constructor fills it at LOAD, the standing this
 * library gave its own URL transport (§62.83). */
extern NSArray *NSFileHandleNotificationMonitorModes;
extern NSString *const NSFileHandleNotificationFileHandleItem;

/* The name of the exception a file-handle operation RAISES when it cannot be performed AT ALL — Apple's
 * contract is that the synchronous doors answer errors while these raise. */
extern NSString *const NSFileHandleOperationException;

@interface NSFileHandle : NSObject <NSCoding, NSSecureCoding>
{
	int _fd;			/* -1 once closed */
	BOOL _closeOnDealloc;
	NSMutableArray *_operations;	/* FnFileHandleSource, one per scheduled operation (NSFileHandle.m) */
	void (^_readabilityHandler)(NSFileHandle *);	/* COPIED, or NULL */
	void (^_writeabilityHandler)(NSFileHandle *);	/* COPIED, or NULL */
}

/* --- creating ------------------------------------------------------------ */

/* Neither of these owns `fd`: the caller made the descriptor, so the caller closes it. */
- (instancetype)initWithFileDescriptor:(int)fd;
- (instancetype)initWithFileDescriptor:(int)fd closeOnDealloc:(BOOL)flag;

/* THE CREATORS OWN WHAT THEY OPEN, and answer nil when the open fails. None of them CREATES a file: the
 * reading, writing and updating doors are `open(2)` with O_RDONLY, O_WRONLY and O_RDWR, so a caller that
 * needs a file made uses NSFileManager first — which is Apple's shape (its page promises a handle "for
 * writing to the file", not a new file). */
+ (nullable instancetype)fileHandleForReadingAtPath:(NSString *)path;
+ (nullable instancetype)fileHandleForWritingAtPath:(NSString *)path;
+ (nullable instancetype)fileHandleForUpdatingAtPath:(NSString *)path;

/* The URL forms pair an open with a REAL error, which is what the path forms above cannot do. */
+ (nullable instancetype)fileHandleForReadingFromURL:(NSURL *)url error:(NSError **)errorPtr;
+ (nullable instancetype)fileHandleForWritingToURL:(NSURL *)url error:(NSError **)errorPtr;
+ (nullable instancetype)fileHandleForUpdatingURL:(NSURL *)url error:(NSError **)errorPtr;

/* --- the standard ones --------------------------------------------------- */

/* The three standard descriptors and a null device, each a SHARED handle that does not own its descriptor
 * (nothing should close fd 0, 1 or 2, and there is one null device for the process). Apple declares these
 * as CLASS PROPERTIES; the house shape is a method, which is the same ABI and the same call.
 *
 * ⚠ THE THREE WERE `+standardInput`/`+standardOutput`/`+standardError` AND ARE NOW APPLE'S SPELLINGS (§63.68).
 * The house names were a near-neighbour of `fileHandleWithStandard…` — the same defect class as the
 * `+nullDevice` paragraph below, found the same way. MEASURED FROM TWO SOURCES: the macOS 14.5 headers
 * declare `fileHandleWithStandardInput/Output/Error` on NSFileHandle, and Apple's documentation index carries
 * all three as OPEN rows for this class. So this is NOT a deletion but a RENAME ONTO APPLE'S NAME, and it
 * CLOSES THREE OPEN LEDGER ROWS while removing three names of ours. (`standardInput` IS Apple's too — on
 * NSTask and NSUserUnixTask, as an INSTANCE property, which is why a text search never flagged it.) */
+ (NSFileHandle *)fileHandleWithStandardInput;
+ (NSFileHandle *)fileHandleWithStandardOutput;
+ (NSFileHandle *)fileHandleWithStandardError;
/* ⚠ THE SPELLING WAS `+nullDevice` AND APPLE'S IS `+fileHandleWithNullDevice` — corrected 2026-10-01
 * (§63.50). `nullDevice` is the SWIFT name (Apple's index carries `class var nullDevice: FileHandle`, because
 * Swift drops the `fileHandleWith` prefix), and this header had taken the Swift spelling. MEASURED against the
 * macOS 14.5 SDK headers, which is the only corpus that can settle it: `NSFileHandle.h` declares
 * `fileHandleWithNullDevice` and contains NO occurrence of `nullDevice`. A header that takes the Swift name
 * compiles, passes every ledger check — the ledger has no row either way — and is WRONG, which is exactly the
 * defect class `tools/foundation-sdk-subset.py` exists to find. */
+ (NSFileHandle *)fileHandleWithNullDevice;

/* --- the descriptor ------------------------------------------------------ */

- (int)fileDescriptor;

/* --- reading and writing, the modern doors ------------------------------- */

- (nullable NSData *)readDataUpToLength:(NSUInteger)length error:(NSError **)errorPtr;

/* Reads until END OF FILE — which BLOCKS on a pipe or socket until the other end closes, exactly as
 * `-readDataToEndOfFile` did. */
- (nullable NSData *)readDataToEndOfFileAndReturnError:(NSError **)errorPtr;

/* Writes ALL of it, or reports the error: a partial write is not a success, which is the lesson W6b's
 * sibling work already learned on the socket side. */
- (BOOL)writeData:(NSData *)data error:(NSError **)errorPtr;

/* --- the file pointer ---------------------------------------------------- */

/* THE OFFSET OUT-PARAMETER IS NULLABLE, because a caller may want only the answer — Apple's own pages
 * pass NULL in their examples, and the implementation guards it. */
- (BOOL)getOffset:(unsigned long long * _Nullable)offsetInFile error:(NSError **)errorPtr;
- (BOOL)seekToOffset:(unsigned long long)offset error:(NSError **)errorPtr;
- (BOOL)seekToEndReturningOffset:(unsigned long long * _Nullable)offsetInFile error:(NSError **)errorPtr;

/* --- operating on the file ----------------------------------------------- */

- (BOOL)closeAndReturnError:(NSError **)errorPtr;
- (BOOL)synchronizeAndReturnError:(NSError **)errorPtr;

/* Truncates OR extends to `offset`, and LEAVES THE FILE POINTER THERE — Apple's sentence, and two
 * operations rather than one. */
- (BOOL)truncateAtOffset:(unsigned long long)offset error:(NSError **)errorPtr;

/* --- reading in the background, one-shot --------------------------------- */

- (void)readInBackgroundAndNotify;
- (void)readInBackgroundAndNotifyForModes:(nullable NSArray *)modes;
- (void)readToEndOfFileInBackgroundAndNotify;
- (void)readToEndOfFileInBackgroundAndNotifyForModes:(nullable NSArray *)modes;
- (void)waitForDataInBackgroundAndNotify;
- (void)waitForDataInBackgroundAndNotifyForModes:(nullable NSArray *)modes;

/* Accepts a connection on a LISTENING descriptor — and see this header's fourth note: it cannot fire on
 * this system, because the kernel never reports a listening descriptor as readable. */
- (void)acceptConnectionInBackgroundAndNotify;
- (void)acceptConnectionInBackgroundAndNotifyForModes:(nullable NSArray *)modes;

/* --- monitoring for readability and writability -------------------------- */

/* Assigning a block registers the descriptor with the CURRENT run loop; assigning nil cancels it. The block
 * is COPIED, and it takes the handle it belongs to. */
- (nullable void (^)(NSFileHandle *handle))readabilityHandler;
- (void)setReadabilityHandler:(nullable void (^)(NSFileHandle *handle))handler;
- (nullable void (^)(NSFileHandle *handle))writeabilityHandler;
- (void)setWriteabilityHandler:(nullable void (^)(NSFileHandle *handle))handler;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEHANDLE_H */
