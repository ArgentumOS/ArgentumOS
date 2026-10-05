/*
 * NSException.m — the throwable object: ported from the archived library, pruned to this substrate, and used.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * MANUAL OWNERSHIP, like the rest of this library. What was pruned from the archived implementation is listed
 * in the header, with a reason per family; this file implements what is left.
 *
 * THE ONE THING WORTH READING TWICE IS THE FORMAT PATH, because it looks like it could have been simpler and
 * cannot: CF's formatter wants a REAL CFString, and a compile-time literal is not one — `@"…"` is a four-word
 * __CFConstantString whose second word is a FLAG word where a CFString keeps its info word (0x7C8 was measured
 * for a C-string literal), so handing the struct to CF's formatter would have it read a flag as a length. The
 * two doors that answer for BOTH kinds of string are -length and -characterAtIndex:, so the format is rebuilt
 * from its characters into a genuine CFString and CF then formats that. It costs one walk of the format, on an
 * exception path, and it is the difference between the format pair working and being a trap.
 */

#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#include <objc/objc-exception.h>	/* objc_exception_throw, objc_setUncaughtExceptionHandler */
#include <CoreFoundation/CoreFoundation.h>
#include <stdlib.h>

NSString *const NSGenericException = @"NSGenericException";
NSString *const NSRangeException = @"NSRangeException";
NSString *const NSInvalidArgumentException = @"NSInvalidArgumentException";
NSString *const NSInternalInconsistencyException = @"NSInternalInconsistencyException";
/* Cocoa raises this when an allocation fails. Nothing here allocates large enough to fail in practice; the
 * name exists because a class that would raise it needs the spelling to match. */
NSString *const NSMallocException = @"NSMallocException";

/* SEE THE FILE HEADER: a literal is not a CFString, and CF's formatter needs one. */
static CFStringRef fn_cf_string_from(NSString *string)
{
	unsigned long n;
	unsigned long i;
	UniChar *buffer;
	CFStringRef result;

	if (string == nil) {
		return NULL;
	}
	n = (unsigned long)[string length];
	buffer = (UniChar *)malloc((n == 0 ? 1 : n) * sizeof(UniChar));
	if (buffer == NULL) {
		return NULL;
	}
	for (i = 0; i < n; i++) {
		buffer[i] = (UniChar)[string characterAtIndex:i];
	}
	result = CFStringCreateWithCharacters(kCFAllocatorDefault, buffer, (CFIndex)n);
	free(buffer);
	return result;
}

@implementation NSException

/*
 * +1 RATHER THAN AUTORELEASED, WHICH IS APPLE'S CONTRACT DEVIATED FROM ON PURPOSE AND FOR THE SAME REASON
 * NSArray's factories are absent: this library has no autorelease pool. A factory that answered a +0 it never
 * gave up would be a lie a caller could act on, so the deviation is written down instead.
 */
+ (NSException *)exceptionWithName:(NSExceptionName)name
                            reason:(NSString *)reason
                          userInfo:(NSDictionary *)userInfo
{
	return [[self alloc] initWithName:name reason:reason userInfo:userInfo];
}

- (instancetype)initWithName:(NSExceptionName)name
                      reason:(NSString *)reason
                    userInfo:(NSDictionary *)userInfo
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* RETAINED, NOT COPIED — and the difference is unobservable here rather than tolerated. Apple's
	 * initialiser copies all three, which needs `-copy`/`NSCopying`; this library has neither, and every
	 * string it can hold is IMMUTABLE (a compile-time literal, or a CFString, which is immutable by CF's own
	 * API — there is no NSMutableString). So there is nothing a copy could protect against, and the day a
	 * mutable string exists these three lines are what must change. */
	_name = [name retain];
	_reason = [reason retain];
	_userInfo = [userInfo retain];
	return self;
}

- (NSExceptionName)name
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

/* THE RUNTIME THROWS IT. objc_exception_throw is the entry point clang's @try/@catch lowering unwinds
 * through, so a raised exception is a real one rather than a convention — and it does not return. */
- (void)raise
{
	objc_exception_throw(self);
}

+ (void)raise:(NSExceptionName)name format:(NSString *)format, ...
{
	va_list arguments;

	va_start(arguments, format);
	[self raise:name format:format arguments:arguments];
	va_end(arguments);
}

+ (void)raise:(NSExceptionName)name format:(NSString *)format arguments:(va_list)arguments
{
	va_list copy;
	CFStringRef cfFormat;
	CFStringRef text = NULL;

	cfFormat = fn_cf_string_from(format);
	if (cfFormat != NULL) {
		/* THE CALLER'S LIST IS CONSUMED FROM A COPY, so its own va_end stays valid — the same rule the
		 * archive recorded, and the same one CFString's own -stringWithFormat:arguments: follows. */
		va_copy(copy, arguments);
		text = CFStringCreateWithFormatAndArguments(kCFAllocatorDefault, NULL, cfFormat, copy);
		va_end(copy);
		CFRelease(cfFormat);
	}

	/* NOT REACHED. The +1 the exception carries is never released because -raise unwinds or terminates the
	 * process; Apple's own +raise: has the same shape. */
	[[[self alloc] initWithName:name reason:(NSString *)text userInfo:nil] raise];
}

- (NSString *)description
{
	CFStringRef cfFormat;
	CFStringRef text;

	if (_reason == nil) {
		return _name;
	}
	cfFormat = fn_cf_string_from(@"%@: %@");
	if (cfFormat == NULL) {
		return _name;
	}
	/* %@ IS ANSWERED BY THIS LIBRARY, not by a conversion: NSObject implements -description and
	 * -copyDescription and CF's formatter reaches them, which is what the object probe already tests. */
	text = CFStringCreateWithFormat(kCFAllocatorDefault, NULL, cfFormat, _name, _reason);
	CFRelease(cfFormat);

	return (NSString *)text;
}

@end

/*
 * THE UNCAUGHT-EXCEPTION HANDLER. -raise calls objc_exception_throw, so an uncaught exception never returns
 * into Foundation and there is no place here that could NOTICE one — the runtime notices, and libobjc exposes
 * the hook for it. Both of Apple's functions are here: the setter installs into the runtime AND keeps this
 * library's own copy, because the runtime's setter ANSWERS the previous handler while having no getter, and
 * Apple's getter must answer the CURRENT one.
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
