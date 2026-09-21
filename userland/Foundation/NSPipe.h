/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSPipe — TWO DESCRIPTORS THAT ARE THE ENDS OF ONE CHANNEL. docs/design/foundation-plan.md W6c and §44.
 *
 * A PIPE IS ONE-WAY AND THE TWO HANDLES ARE NOT INTERCHANGEABLE: writing to `-fileHandleForReading` fails
 * with EBADF, and the read handle reports END OF FILE once every write handle is closed — which is the fact
 * that makes `readDataToEndOfFile` the idiom it is. `NSPipe` itself is a small class because `pipe(2)` is a
 * small system call: it creates the pair and owns the two handles.
 *
 * THE HANDLES OWN THEIR DESCRIPTORS (closing happens when they are deallocated, as `-fileHandleForReading`'s
 * counterpart in NSFileHandle documents for anything that opened its own descriptor), so a caller that wants
 * the pipe gone should close or release the handles rather than the pipe.
 */

#ifndef FOUNDATION_NSPIPE_H
#define FOUNDATION_NSPIPE_H

#import <Foundation/NSObject.h>

@class NSFileHandle;

NS_ASSUME_NONNULL_BEGIN

@interface NSPipe : NSObject
{
	NSFileHandle *_fileHandleForReading;	/* owns the read end */
	NSFileHandle *_fileHandleForWriting;	/* owns the write end */
}

/* "Returns an NSPipe object" — created from pipe(2), and nil is not a documented outcome: a failed pipe(2)
 * is a process out of descriptors, which raises here rather than answering a half-made channel. */
+ (NSPipe *)pipe;

- (NSFileHandle *)fileHandleForReading;
- (NSFileHandle *)fileHandleForWriting;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSPIPE_H */
