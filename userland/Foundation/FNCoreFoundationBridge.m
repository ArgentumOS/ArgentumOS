/*
 * FNCoreFoundationBridge.m — the private protocol CoreFoundation dispatches into, implemented on this
 * tree's Foundation classes.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS FILE EXISTS. With CF's ObjC dispatch live (the plan's M2), CF's fast paths reach into the
 * bridged object and ask it for methods Apple's REAL classes implement — `_getCString:maxLength:encoding:`,
 * `_cfUppercase:`, `_getValue:forType:`, the CFCalendar component-descriptor set, ~50 in all. This tree's
 * Foundation was written independently and answers a different method set, so those sites find nothing.
 * The plan's M2 work list records the whole list; this file is where the answers live, ONE PLACE, so the
 * compatibility surface can be read as a surface rather than hunted for across the classes.
 *
 * FOUNDATION DEPENDS ON COREFOUNDATION HERE, AND THAT DIRECTION IS DELIBERATE. The private protocol speaks
 * CF's vocabulary — `_getCString:maxLength:encoding:` takes a CFStringEncoding, not an NSStringEncoding —
 * so naming those constants honestly means importing <CoreFoundation/CFString.h> rather than copying the
 * values here, where they would drift from the header that owns them. And the direction is now CLEAN:
 * Foundation on CoreFoundation would have been a cycle while CF referenced Foundation symbols, and it no
 * longer does — modification 7 replaced the one class-object reference (`isKindOfClass:[NSMutableArray
 * class]`) with a runtime `objc_getClass` lookup. This is the plan's own direction: M2's gate is the
 * Foundation probes passing against CF-backed classes, which is Foundation standing ON CF.
 *
 * A CATEGORY, so that NSString.m is not the only place these are visible and so a reader can see at a
 * glance how much of Apple's private surface this tree answers.
 */
#import <Foundation/NSString.h>
#import <CoreFoundation/CFString.h>

@implementation NSString (FNCoreFoundationBridge)

/* CFStringGetCString's dispatch site, verbatim from CFString.c:
 *
 *     CF_OBJC_FUNCDISPATCHV(_kCFRuntimeIDCFString, Boolean, (NSString *)str,
 *                           _getCString:buffer maxLength:(NSUInteger)bufferSize - 1 encoding:encoding);
 *
 * TWO CONTRACTS DIFFER BETWEEN CF AND THIS LIBRARY, and both are handled rather than assumed:
 *
 *  - `maxLength` EXCLUDES the terminator on CF's side (its caller passes bufferSize - 1, and CF's own
 *    fallback refuses when `len >= bufferSize`), while this library's public door counts the terminator IN
 *    its maxLength. So the public door is called with maxLength + 1; the +1 cannot overflow because CF
 *    only ever passes bufferSize - 1 with bufferSize >= 1.
 *  - the encoding is a CFStringEncoding, and the map below is the honest one: the four this tree can
 *    actually convert to, and a refusal for everything else rather than a wrong guess. CF's own fallback
 *    NUL-terminates the buffer before refusing, and so does this.
 */
- (BOOL)_getCString:(char *)buffer maxLength:(NSUInteger)maxLength encoding:(CFStringEncoding)cfEncoding
{
	NSStringEncoding encoding;

	if (buffer == NULL) {
		return NO;
	}
	switch (cfEncoding) {
	case kCFStringEncodingUTF8:
		encoding = NSUTF8StringEncoding;
		break;
	case kCFStringEncodingASCII:
		encoding = NSASCIIStringEncoding;
		break;
	case kCFStringEncodingISOLatin1:
		encoding = NSISOLatin1StringEncoding;
		break;
	case kCFStringEncodingMacRoman:
		encoding = NSMacOSRomanStringEncoding;
		break;
	default:
		buffer[0] = 0;
		return NO;
	}
	return [self getCString:buffer maxLength:maxLength + 1 encoding:encoding];
}

@end
