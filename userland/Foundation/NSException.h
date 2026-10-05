/*
 * NSException.h — the object `@throw` carries and `@catch` matches, and what an out-of-range index throws.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * PORTED FROM THE ARCHIVED LIBRARY (archive/Foundation/NSException.{h,m}, API plan F4), AND PRUNED TO WHAT
 * THIS SUBSTRATE CAN SHIP. That is the whole provenance story, and it is stated because the tree's rule is
 * "declare only what can ship": a name or a door that compiles but cannot work is worse than an absent one,
 * because the next reader trusts it.
 *
 * WHAT WAS PRUNED, EACH WITH ITS REASON:
 *   * `NSCopying`/`NSCoding` and the coder doors (`-encodeWithCoder:`/`-initWithCoder:`). They exist upstream
 *     so an exception can cross a distributed-objects reply; THIS library has no NSCoder, no NSCoding and no
 *     NSConnection, so the conformance would be a promise nothing could keep. The archive's own note is
 *     preserved in the plan: the two doors are what the DO path needs, and the DO path is not here yet.
 *   * `NSAssertionHandler` and the `NSAssert`/`NSCAssert` family. The default handler RAISES and stores itself
 *     in the THREAD's dictionary — and this library has no NSThread, no NSMutableDictionary and no
 *     -threadDictionary. An assertion macro that cannot raise on a per-thread handler is not the mechanism
 *     Apple documents, so the family waits for the thread-and-collections work rather than being invented
 *     here in a shape that would have to change.
 *   * The class-specific exception NAMES (NSDestinationInvalidException, NSPortReceive/Send/Timeout, the
 *     NSInvocationOperation pair, NSUndefinedKeyException, NSOldStyleException, …). Each names a failure that
 *     belongs to a class this library does not have (ports, operations, KVC, old-style plists), so declaring
 *     them would be declaring catch names for exceptions nothing here can raise.
 *
 * AND WHAT WAS KEPT IS EXACTLY WHAT THE DOOR NEEDS: the class, its name/reason/userInfo triple, -raise going
 * through the RUNTIME, the format-raising pair, and the five Cocoa-standard names.
 *
 *-raise HANDS THE RECEIVER TO THE RUNTIME. `objc_exception_throw` (declared in <objc/objc-exception.h>) is
 * what clang's @try/@catch lowering unwinds through, so a raised exception is a REAL one rather than a
 * convention — and the two personalities and the unwinder are already in this image, because libobjc itself
 * links libunwind (measured: `readelf -d libobjc.so | grep NEEDED` lists libunwind.so.1, libc++abi.so.1 and
 * libc++.so.1, and both are staged in the guest).
 *
 * NULLABILITY USES APPLE'S SPELLING HERE (NS_ASSUME_NONNULL_BEGIN, defined in NSObject.h), because this
 * header is a PORT and its annotations came across with it. The library's older three headers annotate with
 * the `_Nonnull`/`_Nullable` KEYWORDS instead — both are the same region note, and the two spellings
 * coexisting is stated rather than tidied, so a reader does not "fix" one of them into a compile error.
 */

#ifndef FNX_NSEXCEPTION_H
#define FNX_NSEXCEPTION_H

#import <Foundation/NSObject.h>
#include <stdarg.h>

@class NSString;
/* DECLARED, NOT AVAILABLE: `userInfo` is an NSDictionary in Apple's signature and this library has no
 * NSDictionary yet, so the type is forward-declared to keep the SIGNATURE faithful (surface rule §11.0) while
 * nothing can construct one. Every raise site in this library passes nil, and -userInfo answers nil. */
@class NSDictionary;

NS_ASSUME_NONNULL_BEGIN

/* Apple spells an exception name with its own type, and so does this header: a name is an NSString. */
typedef NSString *NSExceptionName;

/* THE FIVE COCOA-STANDARD NAMES. Apple declares them all in NSException.h and they are here for the same
 * reason: a caller has to be able to CATCH one, and a catch needs a name. Values are their own names (the
 * names are Apple's; §11.6.1 D2 for the strings). */
extern NSExceptionName const NSGenericException;
extern NSExceptionName const NSRangeException;
extern NSExceptionName const NSInvalidArgumentException;
extern NSExceptionName const NSInternalInconsistencyException;
extern NSExceptionName const NSMallocException;

/* THE UNCAUGHT-EXCEPTION HANDLER, and the detection belongs to the RUNTIME: -raise calls
 * objc_exception_throw, which never returns to Foundation when nothing catches it, so the only code that
 * KNOWS an exception went uncaught is the Objective-C runtime — which exposes this hook. The handler is
 * called on the thread that threw, before the runtime terminates the process. */
@class NSException;	/* the typedef below names the class, which is declared after it */

typedef void (*NSUncaughtExceptionHandler)(NSException *exception);

extern NSUncaughtExceptionHandler _Nullable NSGetUncaughtExceptionHandler(void);
extern void NSSetUncaughtExceptionHandler(NSUncaughtExceptionHandler _Nullable handler);

@interface NSException : NSObject
{
	NSString *_name;
	NSString *_reason;
	NSDictionary *_userInfo;
}

+ (NSException *)exceptionWithName:(NSExceptionName)name
                            reason:(NSString *_Nullable)reason
                          userInfo:(NSDictionary *_Nullable)userInfo;

/* NON-NULL IN AND OUT, WHICH IS WHAT THE IMPLEMENTATION DOES RATHER THAN WHAT LOOKS TIDY: the archive's
 * "refuse a nil name" rule lived in its CODER door (an archive carrying no name is refused), and that door is
 * one of the prunes, so there is no nil path left here to annotate for. */
- (instancetype)initWithName:(NSExceptionName)name
                      reason:(NSString *_Nullable)reason
                    userInfo:(NSDictionary *_Nullable)userInfo;

- (NSExceptionName)name;
- (NSString *_Nullable)reason;
- (NSDictionary *_Nullable)userInfo;

/* DOES NOT RETURN. The declaration says void because that is Apple's, and the implementation's last statement
 * is objc_exception_throw, which unwinds or terminates. */
- (void)raise;

/* THE FORMAT PAIR. Apple's, and the reason it is worth keeping: a raise site inside this library wants to
 * name the offending index, and building that string is the exception's job rather than every caller's. */
+ (void)raise:(NSExceptionName)name format:(NSString *)format, ...;
+ (void)raise:(NSExceptionName)name format:(NSString *)format arguments:(va_list)arguments;

@end

NS_ASSUME_NONNULL_END

#endif	/* FNX_NSEXCEPTION_H */
