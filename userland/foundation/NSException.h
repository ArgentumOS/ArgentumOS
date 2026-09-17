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

#import <foundation/NSObject.h>
#include <stdarg.h>

@class NSString;
@class NSDictionary;

/* NULLABILITY (F6, slice 3): NONNULL by default. -name is required (the init takes
 * it nonnull and nothing clears it), while the other two are optional — both are
 * `[<arg> copy]` of an argument Cocoa allows to be nil, nexception.m passes
 * userInfo:nil itself when raising from a format, and -description branches on
 * `_reason != nil`. The two constructs are nullable for the same reason as
 * NSError's: -initWithName:... is a measured `return nil;` site and the factory
 * returns its result. */
NS_ASSUME_NONNULL_BEGIN

/* Cocoa's standard names; code that catches by name expects these spellings. */
extern NSString *const NSGenericException;
extern NSString *const NSRangeException;
extern NSString *const NSInvalidArgumentException;
extern NSString *const NSInternalInconsistencyException;

@interface NSException : NSObject <NSCopying>
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
+ (void)raise:(NSString *)name format:(NSString *)format, ...;
+ (void)raise:(NSString *)name format:(NSString *)format arguments:(va_list)arguments;

NS_ASSUME_NONNULL_END

@end

#endif /* FOUNDATION_NSEXCEPTION_H */
