/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSException — the object `@throw` carries and `@catch` matches.
 * docs/design/foundation-plan.md, F4.
 *
 * THE NAME IS FREE, and that is worth stating because it is unusual here: the
 * runtime declares no NSException (checked: zero matches in libobjc2), so
 * `@catch (NSException *e)` in our own code and in any Cocoa-shaped code matches
 * OUR class without a collision. `Object` and `NSAutoreleasePool` were the two
 * names the runtime DOES own, and the reasons this Foundation carries the NS
 * prefix at all.
 *
 * -raise hands the receiver to the runtime: objc_exception_throw (declared in
 * <objc/objc-exception.h>) is what clang's @try/@catch lowering unwinds through,
 * so a raised exception is a real one, not a convention.
 */

#ifndef FOUNDATION_NSEXCEPTION_H
#define FOUNDATION_NSEXCEPTION_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>
#include <stdarg.h>

@class NSString;
@class NSDictionary;

/* NULLABILITY (F6, slice 3): NONNULL by default. -name is required (the init takes
 * it nonnull and nothing clears it), while the other two are optional — both are
 * `[<arg> copy]` of an argument Cocoa allows to be nil, NSException.m passes
 * userInfo:nil itself when raising from a format, and -description branches on
 * `_reason != nil`. The two constructs are nullable for the same reason as
 * NSError's: -initWithName:... is a measured `return nil;` site and the factory
 * returns its result. */
NS_ASSUME_NONNULL_BEGIN

/* Apple spells an exception name with its own type. Declared here because the string constants (and the
 * NSString ones) are spelled with it. */
typedef NSString *NSExceptionName;

/* THE UNCAUGHT-EXCEPTION HANDLER. Apple's pair, and the detection belongs to the RUNTIME: -raise calls
 * objc_exception_throw, which never returns to Foundation when nothing catches it, so the only code that KNOWS
 * an exception went uncaught is the Objective-C runtime - which exposes this very hook. The handler is called
 * on the thread that threw, before the runtime terminates the process. */
@class NSException;   /* the typedef below names the class, which is declared after it */

typedef void (*NSUncaughtExceptionHandler)(NSException *exception);

extern NSUncaughtExceptionHandler _Nullable NSGetUncaughtExceptionHandler(void);
extern void NSSetUncaughtExceptionHandler(NSUncaughtExceptionHandler _Nullable handler);

/* THE NAMES OF THE EXCEPTIONS THIS LIBRARY DOES NOT RAISE ITSELF. Apple declares them all in NSException.h, and
 * they are here for the same reason: a caller has to be able to CATCH one, and a catch needs a name. Values are
 * their own names (the names are Apple's; §11.6.1 D2 for the strings). */

/* Cocoa's standard names; code that catches by name expects these spellings. */
extern NSString *const NSGenericException;
extern NSString *const NSRangeException;
extern NSString *const NSInvalidArgumentException;
extern NSString *const NSInternalInconsistencyException;
extern NSString *const NSMallocException;

/* **`NSCoding` IS PART OF APPLE'S DECLARATION AND WAS MISSING HERE** (§62.91). It is not decoration: an
 * exception is what crosses a distributed-objects REPLY (`-replyWithException:`), and the DO wire refuses an
 * unarchivable object on the SENDING side — so without these two doors the reply path simply cannot carry one. */
@interface NSException : NSObject <NSCopying, NSCoding>
{
	NSString *_name;
	NSString *_reason;
	NSDictionary *_userInfo;
}

+ (nullable NSException *)exceptionWithName:(NSString *)name
			    reason:(nullable NSString *)reason
			  userInfo:(nullable NSDictionary *)userInfo;

- (nullable id)initWithName:(NSString *)name
	    reason:(nullable NSString *)reason
	  userInfo:(nullable NSDictionary *)userInfo;

- (NSString *)name;
- (nullable NSString *)reason;
- (nullable NSDictionary *)userInfo;

- (void)raise;

/* THE CODER DOORS. The KEYS are this library's (Apple's are private) and follow the house convention — the
 * plain property names, as NSTermOfAddress and NSMorphology write theirs. */
- (void)encodeWithCoder:(NSCoder *)coder;
- (nullable instancetype)initWithCoder:(NSCoder *)coder;

+ (void)raise:(NSString *)name format:(NSString *)format, ...;
+ (void)raise:(NSString *)name format:(NSString *)format arguments:(va_list)arguments;




extern NSExceptionName const NSDestinationInvalidException;
extern NSExceptionName const NSInconsistentArchiveException;
extern NSExceptionName const NSInvalidArchiveOperationException;
extern NSExceptionName const NSInvalidReceivePortException;
extern NSExceptionName const NSInvalidSendPortException;
extern NSExceptionName const NSInvalidUnarchiveOperationException;
extern NSExceptionName const NSInvocationOperationCancelledException;
extern NSExceptionName const NSInvocationOperationVoidResultException;
extern NSExceptionName const NSObjectInaccessibleException;
extern NSExceptionName const NSObjectNotAvailableException;
extern NSExceptionName const NSOldStyleException;
extern NSExceptionName const NSPortReceiveException;
extern NSExceptionName const NSPortSendException;
extern NSExceptionName const NSPortTimeoutException;
extern NSExceptionName const NSUndefinedKeyException;

NS_ASSUME_NONNULL_END

@end

/*
 * THE ASSERTION FAMILY (W2d). An assertion is not a log line: the default handler RAISES, which is
 * what makes a failed assertion a failure. The handler is PER THREAD and lives in that thread's
 * `-threadDictionary` under `NSAssertionHandlerKey`, so a program can install its own and turn
 * assertions into something else — Apple's documented mechanism, and the reason this family needed
 * `-threadDictionary` to exist at all.
 *
 * THE MESSAGE'S SHAPE IS OURS: Apple documents the exception's NAME and that the description is
 * carried, not the exact string. The probe asserts the name, that the description survives, and that
 * a replacement handler is the one consulted.
 *
 * THE `(NSString *)` CASTS ARE NOT COSMETIC: `+stringWithUTF8String:` answers a NULLABLE string, and
 * these macros pass it to parameters that are nonnull, so without the cast the family does not
 * compile under `-Werror=nullable-to-nonnull-conversion`. The cast is sound here because `__FILE__`
 * and `__func__` are always valid C strings.
 *
 * `NS_BLOCK_ASSERTIONS` compiles the whole family out — and when it does, the CONDITION IS NOT
 * EVALUATED, which is the one thing about that switch a program can depend on.
 */
NS_ASSUME_NONNULL_BEGIN

@interface NSAssertionHandler : NSObject

+ (NSAssertionHandler *)currentHandler;

- (void)handleFailureInMethod:(SEL)selector
		       object:(id)object
			 file:(NSString *)fileName
		   lineNumber:(NSInteger)line
		  description:(NSString *)format, ...;

- (void)handleFailureInFunction:(NSString *)functionName
			   file:(NSString *)fileName
		     lineNumber:(NSInteger)line
		    description:(NSString *)format, ...;

@end

/* The thread-dictionary key the current handler is stored under. Its VALUE is this library's own:
 * the only requirement is that the code storing a handler and the code reading it agree, and both
 * are this file. */
extern NSString *NSAssertionHandlerKey;

NS_ASSUME_NONNULL_END

#ifndef NS_BLOCK_ASSERTIONS

#define NSAssert(condition, desc, ...)						\
	do {									\
		if (!(condition)) {						\
			[[NSAssertionHandler currentHandler]			\
				handleFailureInMethod:_cmd object:self			\
				file:(NSString *)[NSString stringWithUTF8String:__FILE__]		\
				lineNumber:__LINE__ description:(desc), ##__VA_ARGS__];	\
		}								\
	} while (0)

#define NSCAssert(condition, desc, ...)						\
	do {									\
		if (!(condition)) {						\
			[[NSAssertionHandler currentHandler]			\
				handleFailureInFunction:(NSString *)[NSString stringWithUTF8String:__func__] \
				file:(NSString *)[NSString stringWithUTF8String:__FILE__]		\
				lineNumber:__LINE__ description:(desc), ##__VA_ARGS__];	\
		}								\
	} while (0)

#else /* NS_BLOCK_ASSERTIONS */

#define NSAssert(condition, desc, ...)		((void)0)
#define NSCAssert(condition, desc, ...)		((void)0)

#endif /* NS_BLOCK_ASSERTIONS */

/* THE NUMBERED FORMS exist for compilers without variadic macros; they are one call with the
 * arguments spelled out. */
#define NSAssert1(condition, desc, arg1) NSAssert((condition), (desc), (arg1))
#define NSAssert2(condition, desc, arg1, arg2) NSAssert((condition), (desc), (arg1), (arg2))
#define NSAssert3(condition, desc, arg1, arg2, arg3) NSAssert((condition), (desc), (arg1), (arg2), (arg3))
#define NSAssert4(condition, desc, arg1, arg2, arg3, arg4) NSAssert((condition), (desc), (arg1), (arg2), (arg3), (arg4))
#define NSAssert5(condition, desc, arg1, arg2, arg3, arg4, arg5) NSAssert((condition), (desc), (arg1), (arg2), (arg3), (arg4), (arg5))
#define NSCAssert1(condition, desc, arg1) NSCAssert((condition), (desc), (arg1))
#define NSCAssert2(condition, desc, arg1, arg2) NSCAssert((condition), (desc), (arg1), (arg2))
#define NSCAssert3(condition, desc, arg1, arg2, arg3) NSCAssert((condition), (desc), (arg1), (arg2), (arg3))
#define NSCAssert4(condition, desc, arg1, arg2, arg3, arg4) NSCAssert((condition), (desc), (arg1), (arg2), (arg3), (arg4))
#define NSCAssert5(condition, desc, arg1, arg2, arg3, arg4, arg5) NSCAssert((condition), (desc), (arg1), (arg2), (arg3), (arg4), (arg5))

/* The parameter forms name the CONDITION in the message, which is why they are separate macros
 * rather than a flag on the others. */
#define NSParameterAssert(condition)						\
	NSAssert((condition), @"Invalid parameter not satisfying: %s", #condition)
#define NSCParameterAssert(condition)						\
	NSCAssert((condition), @"Invalid parameter not satisfying: %s", #condition)

#endif /* FOUNDATION_NSEXCEPTION_H */
