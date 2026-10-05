/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSISO8601DateFormatter.m — the options become a pattern, and ICU does the arithmetic.
 * docs/design/foundation-plan.md §12.3 W11.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words).
 *
 * THE WHOLE CLASS IS ONE TRANSLATION: a bitmask of FIELDS becomes an ICU date PATTERN, and then
 * udat does what udat already does. That is not a shortcut — the fields are the specification (ISO
 * 8601 says a date is year-month-day and a time is hour:minute:second), and the two things this file
 * must not do are (a) decide a locale's conventions, because ISO 8601 HAS none, and (b) hand-roll the
 * calendar arithmetic, which ICU already does correctly for every offset and for the week-year case.
 *
 * THE LOCALE IS PINNED TO en_US_POSIX, and that is a decision rather than a default: ICU's patterns
 * are interpreted against a locale's own digits and calendar, so leaving the current locale in place
 * would make "2026-09-20" print with Arabic-Indic digits in an Arabic locale — correct for a
 * locale-formatted date and WRONG for ISO 8601, which is a fixed interchange format. en_US_POSIX is
 * the identifier ICU itself designates for exactly this purpose.
 *
 * THE PATTERN IS ASSEMBLED IN A FIXED ORDER — date, then the join, then time, then zone — because that
 * is the grammar's order, and each separator exists as its own option precisely so the caller decides
 * it rather than us.
 */

#import <Foundation/NSISO8601DateFormatter.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSString.h>
#import <Foundation/NSCharacterSet.h>
/* For -encodeInteger:forKey: and its siblings: NSCoding.h forward-declares the CLASS, which is not
 * the same as declaring its methods, and the four -Wobjc-method-access warnings this file produced
 * were exactly that difference. */
#import <Foundation/NSCoder.h>

#include <unicode/udat.h>
#include <unicode/ustring.h>

#include <string.h>

#define FN_ISO_MAX 128

/*
 * THE BITMASK, AS A PATTERN. Written as an explicit sequence of appends rather than a table, because
 * the ORDER is the point and a table would hide it.
 *
 * "XXXX"/"XXXXX" ARE ICU'S ISO 8601 ZONE FIELDS, and the choice between them is the
 * ColonSeparatorInTimeZone option: XXX(X) renders "+0530", XXXXX renders "+05:30" and — the reason
 * this class's default output ends in "Z" — renders UTC as a bare "Z", which is what the grammar
 * asks for.
 */
static void fn_iso_pattern(NSISO8601DateFormatOptions options, char *out, size_t cap)
{
	size_t n = 0;
	int haveDate = 0;
	int colonDate = (options & NSISO8601DateFormatWithDashSeparatorInDate) ? 1 : 0;
	int colonTime = (options & NSISO8601DateFormatWithColonSeparatorInTime) ? 1 : 0;

#define FN_ISO_ADD(text) \
	do { \
		size_t len = strlen(text); \
		if (n + len + 1 < cap) { memcpy(out + n, text, len); n += len; out[n] = '\0'; } \
	} while (0)

	out[0] = '\0';

	if (options & NSISO8601DateFormatWithFullDate) {
		FN_ISO_ADD("yyyy");
		if (colonDate) FN_ISO_ADD("-");
		FN_ISO_ADD("MM");
		if (colonDate) FN_ISO_ADD("-");
		FN_ISO_ADD("dd");
		haveDate = 1;
	} else {
		if (options & NSISO8601DateFormatWithYear) {
			FN_ISO_ADD("yyyy");
			haveDate = 1;
		}
		if (options & NSISO8601DateFormatWithMonth) {
			FN_ISO_ADD("MM");
			haveDate = 1;
		}
		if (options & NSISO8601DateFormatWithWeekOfYear) {
			/* QUOTED so ICU reads the W as a literal: an unquoted letter is a field. */
			FN_ISO_ADD("'W'ww");
			haveDate = 1;
		}
		if (options & NSISO8601DateFormatWithDay) {
			FN_ISO_ADD("dd");
			haveDate = 1;
		}
	}

	if (options & NSISO8601DateFormatWithTime) {
		if (haveDate) {
			FN_ISO_ADD((options & NSISO8601DateFormatWithSpaceBetweenDateAndTime) ? " " : "'T'");
		}
		FN_ISO_ADD("HH");
		if (colonTime) FN_ISO_ADD(":");
		FN_ISO_ADD("mm");
		if (colonTime) FN_ISO_ADD(":");
		FN_ISO_ADD("ss");
		if (options & NSISO8601DateFormatWithFractionalSeconds) {
			FN_ISO_ADD(".SSS");
		}
	}

	if (options & NSISO8601DateFormatWithTimeZone) {
		FN_ISO_ADD((options & NSISO8601DateFormatWithColonSeparatorInTimeZone) ? "XXXXX" : "XXXX");
	}
#undef FN_ISO_ADD
}

/* The zone as ICU wants it: by NAME when it has one (which carries the daylight rules), as a
 * GMT offset otherwise. The same translation NSDateFormatter makes, for the same reason. */
static int32_t fn_iso_zone(NSTimeZone *zone, UChar *dest, int32_t cap)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;
	const char *text;

	if (zone == nil) {
		return 0;
	}
	text = [[zone name] UTF8String];
	if (text == NULL) {
		return 0;
	}
	u_strFromUTF8(dest, cap, &used, text, -1, &status);
	if (U_FAILURE(status)) {
		return 0;
	}
	return used;
}

@implementation NSISO8601DateFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* APPLE'S DEFAULTS: the internet date-time shape, and GMT. Not "nothing set" — a fresh ISO 8601
	 * formatter is immediately useful, which is the difference between this class and a
	 * date-STYLE formatter. */
	_formatOptions = NSISO8601DateFormatWithInternetDateTime;
	_timeZone = nil;		/* nil == GMT, resolved at build */
	_formatter = NULL;
	[self fnRebuild];
	return self;
}

- (void)dealloc
{
	[_timeZone release];
	if (_formatter != NULL) {
		udat_close((UDateFormat *)_formatter);
		_formatter = NULL;
	}
	[super dealloc];
}

/* The effective zone: what was set, or GMT. */
- (NSTimeZone *)fnZone
{
	if (_timeZone != nil) {
		return _timeZone;
	}
	return [NSTimeZone timeZoneForSecondsFromGMT:0];
}

- (void)fnRebuild
{
	UErrorCode status = U_ZERO_ERROR;
	char pattern[FN_ISO_MAX];
	UChar upattern[FN_ISO_MAX];
	UChar zone[64];
	int32_t patternLen = 0;
	int32_t zoneLen;

	if (_formatter != NULL) {
		udat_close((UDateFormat *)_formatter);
		_formatter = NULL;
	}
	fn_iso_pattern(_formatOptions, pattern, sizeof pattern);
	if (pattern[0] == '\0') {
		return;			/* no fields at all: the doors answer nil rather than "" */
	}
	u_strFromUTF8(upattern, FN_ISO_MAX, &patternLen, pattern, -1, &status);
	if (U_FAILURE(status) || patternLen <= 0) {
		return;
	}
	zoneLen = fn_iso_zone([self fnZone], zone, 64);

	status = U_ZERO_ERROR;
	_formatter = udat_open(UDAT_PATTERN, UDAT_NONE, "en_US_POSIX",
			       zoneLen > 0 ? zone : NULL, zoneLen, upattern, patternLen,
			       &status);
	if (U_FAILURE(status)) {
		_formatter = NULL;
		return;
	}
	/* NOT LENIENT: an ISO 8601 string is a specification, and a formatter that "repaired" 2026-13-45
	 * into a date would be answering a question nobody asked with a value the caller cannot trust. */
	udat_setLenient((UDateFormat *)_formatter, (UBool)0);
}

- (NSISO8601DateFormatOptions)formatOptions
{
	return _formatOptions;
}

- (void)setFormatOptions:(NSISO8601DateFormatOptions)options
{
	if (options == _formatOptions) {
		return;
	}
	_formatOptions = options;
	[self fnRebuild];
}

- (NSTimeZone *)timeZone
{
	/* The getter answers GMT rather than nil when nothing was set, matching the documented
	 * "when unspecified, GMT is used" — a caller asking the formatter what zone it uses gets the
	 * zone it uses. */
	return [self fnZone];
}

- (void)setTimeZone:(nullable NSTimeZone *)value
{
	if (value == _timeZone) {
		return;
	}
	[_timeZone release];
	_timeZone = [value retain];
	[self fnRebuild];
}

- (nullable NSString *)stringFromDate:(NSDate *)date
{
	UChar text[FN_ISO_MAX * 2];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length;

	if (date == nil) {
		return nil;
	}
	if (_formatter == NULL) {
		return nil;
	}
	length = udat_format((UDateFormat *)_formatter,
			     (UDate)([date timeIntervalSince1970] * 1000.0),
			     text, FN_ISO_MAX * 2, NULL, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	{
		char bytes[FN_ISO_MAX * 8];
		int32_t used = 0;

		status = U_ZERO_ERROR;
		u_strToUTF8(bytes, (int32_t)sizeof bytes, &used, text, length, &status);
		if (U_FAILURE(status)) {
			return nil;
		}
		bytes[used] = '\0';
		return [NSString stringWithUTF8String:bytes];
	}
}

- (nullable NSDate *)dateFromString:(NSString *)string
{
	UChar text[FN_ISO_MAX * 2];
	UErrorCode status = U_ZERO_ERROR;
	int32_t length = 0;
	int32_t parsed = 0;
	UDate when;

	if (string == nil || _formatter == NULL) {
		return nil;
	}
	u_strFromUTF8(text, FN_ISO_MAX * 2, &length, [string UTF8String], -1, &status);
	if (U_FAILURE(status) || length <= 0) {
		return nil;
	}
	status = U_ZERO_ERROR;
	when = udat_parse((UDateFormat *)_formatter, text, length, &parsed, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	/* A PARSE THAT DID NOT CONSUME THE WHOLE STRING IS NOT A PARSE: "2026-09-20garbage" would
	 * otherwise answer the date of its prefix, which is a value the caller never wrote. */
	if (parsed != length) {
		return nil;
	}
	return [NSDate dateWithTimeIntervalSince1970:(NSTimeInterval)(when / 1000.0)];
}

+ (nullable NSString *)stringFromDate:(NSDate *)date
			     timeZone:(nullable NSTimeZone *)timeZone
			formatOptions:(NSISO8601DateFormatOptions)formatOptions
{
	NSISO8601DateFormatter *formatter = [[[self alloc] init] autorelease];

	if (date == nil) {
		return nil;
	}
	[formatter setFormatOptions:formatOptions];
	[formatter setTimeZone:timeZone];
	return [formatter stringFromDate:date];
}

- (id)copy
{
	NSISO8601DateFormatter *copy = [[[self class] alloc] init];

	[copy setFormatOptions:_formatOptions];
	[copy setTimeZone:_timeZone];
	return copy;
}

+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	/* The bitmask as an integer and the zone by name; both are settings, and neither is a pointer a
	 * coder could not carry. */
	[coder encodeInteger:(NSInteger)_formatOptions forKey:@"NS.iso8601Options"];
	[coder encodeObject:(_timeZone != nil ? [_timeZone name] : nil) forKey:@"NS.iso8601Zone"];
}

- (instancetype)initWithCoder:(NSCoder *)coder
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	[self setFormatOptions:(NSISO8601DateFormatOptions)[coder decodeIntegerForKey:@"NS.iso8601Options"]];
	{
		NSString *zoneName = [coder decodeObjectForKey:@"NS.iso8601Zone"];

		if (zoneName != nil) {
			[self setTimeZone:[NSTimeZone timeZoneWithName:zoneName]];
		}
	}
	return self;
}

@end
