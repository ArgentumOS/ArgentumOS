/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSException.m — the throwable object.
 *
 * MANUAL OWNERSHIP.
 */

#import <Foundation/NSException.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSThread.h>
#include <objc/objc-exception.h>	/* the runtime's uncaught hook: the only code that KNOWS */	/* the handler lives in the thread dictionary */
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>
#include <objc/objc-exception.h>

NSString *const NSGenericException = @"NSGenericException";
NSString *const NSRangeException = @"NSRangeException";
NSString *const NSInvalidArgumentException = @"NSInvalidArgumentException";
NSString *const NSInternalInconsistencyException = @"NSInternalInconsistencyException";
/* Cocoa raises this when an allocation fails. Nothing here allocates large enough to fail in
 * practice; the name exists because a class that would raise it needs the spelling to match. */
NSString *const NSMallocException = @"NSMallocException";

@implementation NSException

NSExceptionName const NSDestinationInvalidException = @"NSDestinationInvalidException";
NSExceptionName const NSInconsistentArchiveException = @"NSInconsistentArchiveException";
NSExceptionName const NSInvalidArchiveOperationException = @"NSInvalidArchiveOperationException";
NSExceptionName const NSInvalidReceivePortException = @"NSInvalidReceivePortException";
NSExceptionName const NSInvalidSendPortException = @"NSInvalidSendPortException";
NSExceptionName const NSInvalidUnarchiveOperationException = @"NSInvalidUnarchiveOperationException";
NSExceptionName const NSInvocationOperationCancelledException = @"NSInvocationOperationCancelledException";
NSExceptionName const NSInvocationOperationVoidResultException = @"NSInvocationOperationVoidResultException";
NSExceptionName const NSObjectInaccessibleException = @"NSObjectInaccessibleException";
NSExceptionName const NSObjectNotAvailableException = @"NSObjectNotAvailableException";
NSExceptionName const NSOldStyleException = @"NSOldStyleException";
NSExceptionName const NSPortReceiveException = @"NSPortReceiveException";
NSExceptionName const NSPortSendException = @"NSPortSendException";
NSExceptionName const NSPortTimeoutException = @"NSPortTimeoutException";
NSExceptionName const NSUndefinedKeyException = @"NSUndefinedKeyException";

NSExceptionName const NSCharacterConversionException = @"NSCharacterConversionException";
NSExceptionName const NSParseErrorException = @"NSParseErrorException";

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

/* --- THE CODER DOORS (§62.91: they were missing, and the DO reply needs them) --------------------- */

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_name forKey:@"name"];
	[coder encodeObject:_reason forKey:@"reason"];
	[coder encodeObject:_userInfo forKey:@"userInfo"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	id name = [coder decodeObjectForKey:@"name"];
	id reason = [coder decodeObjectForKey:@"reason"];
	id userInfo = [coder decodeObjectForKey:@"userInfo"];

	if (name == nil) {
		/* AN EXCEPTION WITHOUT A NAME IS NOT ONE — the initialiser refuses that — so an archive carrying none
		 * is refused here rather than handed on as an object every reader would trip over. */
		[self release];
		return nil;
	}
	return [self initWithName:name reason:reason userInfo:userInfo];
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

/*
 * THE UNCAUGHT-EXCEPTION HANDLER (the gap §62.3 named). -raise calls objc_exception_throw, so an uncaught
 * exception never returns into Foundation and there is no place here that could NOTICE one - the runtime
 * notices, and libobjc exposes the hook for it (objc/objc-exception.h). Both of Apple's functions are here:
 * the setter installs into the runtime AND keeps its own copy, because the runtime's setter ANSWERS the
 * previous handler while having no getter, and Apple's getter must answer the CURRENT one.
 */
static NSUncaughtExceptionHandler fn_uncaught_handler = NULL;

NSUncaughtExceptionHandler NSGetUncaughtExceptionHandler(void)
{
	return fn_uncaught_handler;
}

void NSSetUncaughtExceptionHandler(NSUncaughtExceptionHandler handler)
{
	/* The two handler TYPES name the same call: the runtime's takes an `id`, Apple's an NSException *, and a
	 * handler written to Apple's signature is the one a caller hands over. */
	fn_uncaught_handler = handler;
	(void)objc_setUncaughtExceptionHandler((objc_uncaught_exception_handler)handler);
}
