/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nexception.m — the throwable object.
 *
 * MANUAL OWNERSHIP.
 */

#import <foundation/NSException.h>
#import <foundation/NSString.h>
#import <foundation/NSDictionary.h>
#include <objc/objc-exception.h>

NSString *const NSGenericException = @"NSGenericException";
NSString *const NSRangeException = @"NSRangeException";
NSString *const NSInvalidArgumentException = @"NSInvalidArgumentException";
NSString *const NSInternalInconsistencyException = @"NSInternalInconsistencyException";
/* Cocoa raises this when an allocation fails. Nothing here allocates large enough to fail in
 * practice; the name exists because a class that would raise it needs the spelling to match. */
NSString *const NSMallocException = @"NSMallocException";

@implementation NSException

+ (NSException *)exceptionWithName:(NSString *)name
			    reason:(NSString *)reason
			  userInfo:(NSDictionary *)userInfo
{
	return [[self alloc] initWithName:name reason:reason userInfo:userInfo];
}

- (id)initWithName:(NSString *)name
	    reason:(NSString *)reason
	  userInfo:(NSDictionary *)userInfo
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_name = [name copy];
	_reason = [reason copy];
	_userInfo = [userInfo copy];
	return self;
}

- (NSString *)name
{
	return _name;
}

- (NSString *)reason
{
	return _reason;
}

- (NSDictionary *)userInfo
{
	return _userInfo;
}

- (void)raise
{
	/*
	 * THE RUNTIME THROWS IT. objc_exception_throw is the entry point clang's
	 * @try/@catch lowering unwinds through, so a raised NSException is a real
	 * exception rather than a convention — and it does not return.
	 */
	objc_exception_throw(self);
}

+ (void)raise:(NSString *)name format:(NSString *)format, ...
{
	va_list args;

	va_start(args, format);
	[self raise:name format:format arguments:args];
	va_end(args);
}

+ (void)raise:(NSString *)name format:(NSString *)format arguments:(va_list)arguments
{
	va_list copy;
	NSException *exception;

	/* Same rule as the string method: consume a copy, so the caller's list stays
	 * valid for its own va_end. */
	va_copy(copy, arguments);
	exception = [[self alloc] initWithName:name
					reason:[NSString stringWithFormat:format arguments:copy]
				      userInfo:nil];
	va_end(copy);
	[exception raise];
}

- (NSString *)description
{
	if (_reason != nil) {
		return [NSString stringWithFormat:@"%@: %@", _name, _reason];
	}
	return _name;
}

- (id)copy
{
	return self;		/* immutable */
}

- (id)mutableCopy
{
	return self;
}

@end
