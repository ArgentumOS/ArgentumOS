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
#import <foundation/NSThread.h>	/* the handler lives in the thread dictionary */
#import <foundation/NSDictionary.h>
#import <foundation/NSString.h>
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
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}

- (id)mutableCopy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) */
}


@end

/*
 * THE ASSERTION HANDLER (W2d). The store is the THREAD's dictionary, which is why this family waited
 * for `-threadDictionary`: Apple files the key under NSThread's thread properties, and a handler that
 * was global would be one program-wide object where Cocoa gives every thread its own.
 */
NSString *NSAssertionHandlerKey = @"NSAssertionHandler";

@implementation NSAssertionHandler

+ (NSAssertionHandler *)currentHandler
{
	NSMutableDictionary *properties = [[NSThread currentThread] threadDictionary];
	NSAssertionHandler *handler = [properties objectForKey:NSAssertionHandlerKey];

	if (handler == nil) {
		handler = [[NSAssertionHandler alloc] init];
		[properties setObject:handler forKey:NSAssertionHandlerKey];
		/* THE DICTIONARY OWNS IT, and the caller does not — which is what makes this the
		 * per-thread singleton a program replaces through the same key. */
		[handler release];
	}
	return handler;
}

/*
 * THE DEFAULT HANDLER RAISES, and that is the whole contract: an assertion that fires is a failure,
 * not a line in a log. A program that wants the other thing replaces the handler in the thread
 * dictionary — which the probe does, and asserts that the replacement is the one consulted.
 *
 * THE MESSAGE'S SHAPE IS THIS LIBRARY'S (see NSException.h): Apple documents the exception's NAME and
 * that the description is carried, not the exact string.
 */
- (void)handleFailureInMethod:(SEL)selector object:(id)object file:(NSString *)fileName
		   lineNumber:(NSInteger)line description:(NSString *)format, ...
{
	va_list arguments;
	NSString *text;

	va_start(arguments, format);
	text = [[NSString alloc] initWithFormat:format arguments:arguments];
	va_end(arguments);
	[NSException raise:NSInternalInconsistencyException
		    format:@"*** Assertion failure in -[%@ %@], %@:%d — %@",
			   (object != nil) ? (id)[object class] : (id)@"?",
			   NSStringFromSelector(selector), fileName, (int)line, text];
}

- (void)handleFailureInFunction:(NSString *)functionName file:(NSString *)fileName
		     lineNumber:(NSInteger)line description:(NSString *)format, ...
{
	va_list arguments;
	NSString *text;

	va_start(arguments, format);
	text = [[NSString alloc] initWithFormat:format arguments:arguments];
	va_end(arguments);
	[NSException raise:NSInternalInconsistencyException
		    format:@"*** Assertion failure in %@(), %@:%d — %@",
			   functionName, fileName, (int)line, text];
}

@end

