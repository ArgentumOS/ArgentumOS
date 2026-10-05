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
#import <CoreFoundation/CFCharacterSet.h>

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

/* ===================================================================================================
 * NSMutableString: THE CASING AND TRIMMING DOORS (CFString.c 5329-5583).
 *
 * Five sites, all `void`, all dispatched to an NSMutableString - CF mutates a string IN PLACE for these
 * rather than answering a new one:
 *
 *     _cfUppercase:(const void *)locale      CFStringUppercase   (5489)
 *     _cfLowercase:(const void *)locale      CFStringLowercase   (5398)
 *     _cfCapitalize:(const void *)locale     CFStringCapitalize  (5583)
 *     _cfTrimWS                              CFStringTrimWhitespace (5362)
 *     _cfTrim:(const void *)trimString       CFStringTrim        (5329)
 *
 * THE LOCALE ARGUMENT IS IGNORED, AND THAT IS A STATED DEVIATION rather than an oversight: this library's
 * own casing is not locale-sensitive (its transforms are its own, ICU-backed where it has them), so
 * honouring CF's locale would mean a second casing engine. CF's caller passes a CFLocaleRef or NULL, and
 * CF's own fallback path uses the locale for special-case handling only.
 *
 * `_cfTrim:` READS ITS ARGUMENT WITH CF'S OWN API, which is the honest way now that this library may call
 * CF: the trim string arrives as a `const void *` that may be a CF-native string OR a bridged one, and
 * CFStringGetLength / CFStringGetCharacterAtIndex answer either kind. Messaging it as an NSString would be
 * the same mistake in the other direction that cost this bridge its first bug.
 */
static void fn_replace_whole_string(NSMutableString *self, NSString *replacement)
{
	[self replaceCharactersInRange:NSMakeRange(0, [self length])
			    withString:(replacement != nil ? replacement : @"")];
}

@implementation NSMutableString (FNCoreFoundationBridge)

- (void)_cfUppercase:(const void *)locale
{
	(void)locale;	/* see the note above: this library's casing is not locale-sensitive */
	fn_replace_whole_string(self, [self uppercaseString]);
}

- (void)_cfLowercase:(const void *)locale
{
	(void)locale;
	fn_replace_whole_string(self, [self lowercaseString]);
}

- (void)_cfCapitalize:(const void *)locale
{
	(void)locale;
	fn_replace_whole_string(self, [self capitalizedString]);
}

/* THE TRIM PAIR USES CF'S OWN CHARACTER-SET API, WHICH IS BOTH SIMPLER AND MORE FAITHFUL THAN THE FIRST
 * ATTEMPT. That attempt reached for this library's NSCharacterSet and its "whitespace and newline" set -
 * which would have been a DEVIATION from CF's whitespace definition, and pulled in another Foundation
 * header for it. With CF callable there is no reason to approximate: kCFCharacterSetWhitespaceAndNewline
 * IS the set CF's own CFStringTrimWhitespace means, and a CFMutableCharacterSet built from the trim
 * string's characters is exactly what CFStringTrim means. Both then scan the two ends and replace the
 * whole string with what is left, which is in-place from the caller's side. */
static void fn_trim_ends(NSMutableString *self, CFCharacterSetRef set)
{
	NSUInteger start = 0, end = [self length];

	if (set == NULL) {
		return;
	}
	while (start < end && CFCharacterSetIsCharacterMember(set, (UniChar)[self characterAtIndex:start])) {
		start++;
	}
	while (end > start && CFCharacterSetIsCharacterMember(set, (UniChar)[self characterAtIndex:end - 1])) {
		end--;
	}
	fn_replace_whole_string(self, [self substringWithRange:NSMakeRange(start, end - start)]);
}

- (void)_cfTrimWS
{
	fn_trim_ends(self, CFCharacterSetGetPredefined(kCFCharacterSetWhitespaceAndNewline));
}

- (void)_cfTrim:(const void *)trimString
{
	CFStringRef trim = (CFStringRef)trimString;
	CFMutableCharacterSetRef set;

	if (trim == NULL) {
		return;
	}
	set = CFCharacterSetCreateMutable(kCFAllocatorDefault);
	if (set == NULL) {
		return;
	}
	CFCharacterSetAddCharactersInString(set, trim);
	fn_trim_ends(self, set);
	CFRelease(set);
}

@end
