/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDateComponentsFormatter.m — OUR RULES over ICU'S DATA.
 * docs/design/foundation-plan.md §29 and §29.1.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is — mk/20-userland.mk says so in as many words).
 *
 * NO ICU HANDLE IS CACHED, and the difference from its siblings is deliberate rather than sloppy: every
 * other formatter in W11 bakes its settings into ONE handle at open time, while this class needs a
 * different skeleton PER UNIT AND STYLE (7 units x 3 widths). Caching would mean 21 handles and a
 * rebuild rule for each setting; opening per field costs one `unumf_openForSkeletonAndLocale` per unit
 * in the output, which is the same order of work as the formatting itself. The settings here are read
 * FRESH at every call, so there is nothing to invalidate.
 *
 * THE COMPOSITION, IN ONE PLACE: `-stringFromDateComponents:` builds the field list (which units, what
 * values), applies the option rules to decide which fields are KEPT, and renders each kept field through
 * ICU. The three date-shaped doors all reduce to that function, which is why they cannot disagree.
 *
 * THE ICU SKELETON GRAMMAR IS A TRAP AND IT IS WRITTEN DOWN HERE SO IT IS NOT RE-LEARNED: named options
 * are BARE (`unit-width-narrow`) and only precision stems take a dot (`percent .00`). The design pass's
 * first probe wrote `measure-unit/duration-hour .unit-width-narrow` and read the resulting
 * U_NUMBER_SKELETON_SYNTAX_ERROR as "the widths are unreachable" — which was wrong about ICU and right
 * about the probe.
 */

#import <Foundation/NSDateComponentsFormatter.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSString.h>
#import <Foundation/NSLocale.h>

#include <unicode/unumberformatter.h>
#include <unicode/unum.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>

#include <stdio.h>
#include <string.h>

#define FN_DCF_MAX_UNITS 7

typedef struct {
	NSCalendarUnit unit;
	const char *icu;	/* ICU's duration unit name, as the skeleton spells it */
	NSInteger value;
	BOOL keep;
} fn_dcf_field;

/* THE ORDER IS THE RULE: the field list is built largest-first, and everything downstream (collapsing,
 * the zero behaviours, positional padding) reads it in this order. */
static const fn_dcf_field fn_dcf_template[FN_DCF_MAX_UNITS] = {
	{ NSCalendarUnitYear,		"year",		0, YES },
	{ NSCalendarUnitMonth,		"month",	0, YES },
	{ NSCalendarUnitWeekOfYear,	"week",		0, YES },
	{ NSCalendarUnitDay,		"day",		0, YES },
	{ NSCalendarUnitHour,		"hour",		0, YES },
	{ NSCalendarUnitMinute,		"minute",	0, YES },
	{ NSCalendarUnitSecond,		"second",	0, YES }
};

/* THE DEFAULT UNIT SET when `allowedUnits` is zero (Apple's page says a mask of zero means the default
 * units appropriate to the platform). The week bit covers BOTH week spellings, because `NSDateComponents`
 * can carry either and they are one duration unit. */
static NSCalendarUnit fn_dcf_default_units(void)
{
	return (NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitWeekOfYear |
		NSCalendarUnitWeekOfMonth | NSCalendarUnitDay | NSCalendarUnitHour |
		NSCalendarUnitMinute | NSCalendarUnitSecond);
}

static int fn_dcf_allows(NSCalendarUnit allowed, NSCalendarUnit unit)
{
	if (unit == NSCalendarUnitWeekOfYear) {
		return (allowed & (NSCalendarUnitWeekOfYear | NSCalendarUnitWeekOfMonth)) != 0;
	}
	return (allowed & unit) != 0;
}

/* THE SYSTEM LOCALE, underscored for ICU. This class has NO locale property — Apple's surface does not
 * give it one — so the system's is not a fallback, it is the only answer there is. */
static const char *fn_dcf_locale(void)
{
	static char name[64];
	/* THE HOUSE'S ANSWER, NOT THE PROCESS'S — AND THIS WAS A REAL DEFECT (found 2026-09-20 by the host
	 * tier disagreeing with a direct run of the same binary). The first version asked `uloc_getDefault()`,
	 * which is the PROCESS ENVIRONMENT, so the same probe printed "2 hours, 3 minutes" from a shell whose
	 * LANG was set and "2 h, 3 min" under `make host-foundation-run`, whose environment is minimal. Every
	 * other formatter in this family resolves its locale through `+[NSLocale currentLocale]` — which reads
	 * LC_ALL then LANG and FALLS BACK to en_US_POSIX (NSLocale.h states it) — and a class with no locale
	 * property of its own is exactly the class that must use that, rather than the environment it happens
	 * to be launched in. */
	const char *identifier = [[[NSLocale currentLocale] localeIdentifier] UTF8String];
	size_t i;

	if (identifier == NULL) {
		identifier = uloc_getDefault();
	}
	for (i = 0; identifier[i] != '\0' && i + 2 < sizeof name; i++) {
		name[i] = (identifier[i] == '-') ? '_' : identifier[i];
	}
	name[i] = '\0';
	return name;
}

/*
 * ONE UNIT, THROUGH ICU. `width` is one of ICU's own option names and is written BARE — see the file
 * header. Answers nil when ICU refuses the skeleton, which the doors turn into a nil result rather than
 * a half-formatted string.
 */
static NSString *fn_dcf_icu_text(const char *width, const char *unit, double value)
{
	char skeleton[128];
	UChar uskeleton[128];
	UChar out[192];
	char utf8[512];
	UErrorCode status = U_ZERO_ERROR;
	UNumberFormatter *formatter = NULL;
	UFormattedNumber *result = NULL;
	int32_t skeletonLength = 0;
	int32_t length = 0;
	int32_t used = 0;
	NSString *answer = nil;

	snprintf(skeleton, sizeof skeleton, "%s measure-unit/duration-%s", width, unit);
	u_strFromUTF8(uskeleton, 128, &skeletonLength, skeleton, -1, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	status = U_ZERO_ERROR;
	formatter = unumf_openForSkeletonAndLocale(uskeleton, skeletonLength, fn_dcf_locale(), &status);
	if (U_FAILURE(status) || formatter == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	result = unumf_openResult(&status);
	if (U_FAILURE(status) || result == NULL) {
		unumf_close(formatter);
		return nil;
	}
	status = U_ZERO_ERROR;
	unumf_formatDouble(formatter, value, result, &status);
	if (U_FAILURE(status)) {
		goto done;
	}
	status = U_ZERO_ERROR;
	length = unumf_resultToString(result, out, 192, &status);
	if (U_FAILURE(status) || length <= 0) {
		goto done;
	}
	status = U_ZERO_ERROR;
	u_strToUTF8(utf8, (int32_t)sizeof utf8, &used, out, length, &status);
	if (U_FAILURE(status)) {
		goto done;
	}
	utf8[used] = '\0';
	answer = [NSString stringWithUTF8String:utf8];

done:
	unumf_closeResult(result);
	unumf_close(formatter);
	return answer;
}

/* A SPELLED-OUT QUANTITY ("two"), which is what Apple's SpellOut case adds to Full's unit spelling. This
 * is the same ICU call W11's relative formatter uses for its own SpellOut style. */
static NSString *fn_dcf_spelled_number(double value)
{
	UErrorCode status = U_ZERO_ERROR;
	UNumberFormat *format = unum_open(UNUM_SPELLOUT, NULL, 0, fn_dcf_locale(), NULL, &status);
	UChar out[128];
	char utf8[512];
	int32_t length = 0;
	int32_t used = 0;
	NSString *answer = nil;

	if (U_FAILURE(status) || format == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	length = unum_formatDouble(format, value, out, 128, NULL, &status);
	if (U_SUCCESS(status) && length > 0) {
		status = U_ZERO_ERROR;
		u_strToUTF8(utf8, (int32_t)sizeof utf8, &used, out, length, &status);
		if (U_SUCCESS(status)) {
			utf8[used] = '\0';
			answer = [NSString stringWithUTF8String:utf8];
		}
	}
	unum_close(format);
	return answer;
}

/* THE UNIT NAME ALONE, taken from the wide form of the SAME value: "2 hours" -> "hours" (and "1 hour" ->
 * "hour" at 1, so the plural still comes from ICU rather than from an "s" appended here). THE BOUNDARY:
 * this assumes the quantity PRECEDES the unit, which ICU's duration patterns do in the locales this
 * system ships; a locale that put the unit first would need a pattern, not a substring. */
static NSString *fn_dcf_unit_name(const char *unit, double value)
{
	NSString *whole = fn_dcf_icu_text("unit-width-full-name", unit, value);
	NSRange space;

	if (whole == nil) {
		return nil;
	}
	space = [whole rangeOfString:@" "];
	if (space.location == NSNotFound) {
		return whole;
	}
	return [whole substringFromIndex:space.location + space.length];
}

@implementation NSDateComponentsFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE DEFAULTS. Apple publishes two of them: `allowedUnits` (its page says the default is the
	 * platform's units, which is what a zero mask means here) and `referenceDate` ("Defaults to ... at
	 * the time of the ... call if not set", i.e. nil means now). The rest are this file's choice and are
	 * named as such in the header: full unit names, the default zero behaviour, no limit, no phrases. */
	_allowedUnits = 0;
	_unitsStyle = NSDateComponentsFormatterUnitsStyleFull;
	_zeroFormattingBehavior = NSDateComponentsFormatterZeroFormattingBehaviorDefault;
	_maximumUnitCount = 0;
	_calendar = nil;
	_referenceDate = nil;
	_formattingContext = NSFormattingContextUnknown;
	_allowsFractionalUnits = NO;
	_collapsesLargestUnit = NO;
	_includesApproximationPhrase = NO;
	_includesTimeRemainingPhrase = NO;
	return self;
}

- (void)dealloc
{
	[_calendar release];
	[_referenceDate release];
	[super dealloc];
}

- (NSCalendar *)fnCalendar { return _calendar != nil ? _calendar : [NSCalendar currentCalendar]; }
- (NSDate *)fnReferenceDate { return _referenceDate != nil ? _referenceDate : [NSDate date]; }

/*
 * THE ONE PLACE A DURATION BECOMES TEXT, and therefore the one place all five doors pass through.
 */
- (nullable NSString *)fnStringForFields:(fn_dcf_field *)fields count:(int)count
{
	int i;
	int last = -1;			/* the smallest KEPT field, which is the only one that may be fractional */
	BOOL positional = (_unitsStyle == NSDateComponentsFormatterUnitsStylePositional);
	BOOL fractionalAllowed = _allowsFractionalUnits;
	NSMutableString *answer;

	/* WHICH FIELDS ARE KEPT — the zero behaviours, each implemented from Apple's own definition. */
	if (_zeroFormattingBehavior == NSDateComponentsFormatterZeroFormattingBehaviorNone ||
	    _zeroFormattingBehavior == NSDateComponentsFormatterZeroFormattingBehaviorPad) {
		/* None keeps every zero; Pad also keeps them, and pads them later. */
	} else if (_zeroFormattingBehavior == NSDateComponentsFormatterZeroFormattingBehaviorDefault) {
		/* DEFAULT, and the POSITIONAL case is why it is here rather than beside the drop flags below.
		 * Apple's sentence for it — "when using positional units, this behavior drops leading zeroes but
		 * pads middle and trailing values with zeros as needed" — means the trailing zeros STAY when the
		 * output is a clock: one hour is "1:00:00" and not "1". For the other styles the same sentence
		 * read across gives drop-leading AND drop-trailing, which is stated as ours in the header. */
		for (i = 0; i < count && fields[i].value == 0; i++) {
			fields[i].keep = NO;
		}
		if (!positional) {
			for (i = count - 1; i >= 0 && fields[i].value == 0; i--) {
				fields[i].keep = NO;
			}
		}
	} else {
		/*
		 * THE THREE DROP FLAGS, EACH ON ITS OWN — AND `DropAll` IS NOT TESTED FIRST, WHICH WAS THIS
		 * FILE'S FIRST BUG. `DropAll` is DEFINED as the OR of the other three, so a test for "is DropAll
		 * set" (`behavior & DropAll`) is TRUE FOR ANY ONE OF THEM, and the first version therefore
		 * dropped every zero for all four behaviours: the probe measured `1h 30s` from leading, middle,
		 * trailing AND all, where leading should have kept the middle zero. Testing the flags
		 * individually needs no DropAll case at all — the three together ARE DropAll.
		 */
		if (_zeroFormattingBehavior & NSDateComponentsFormatterZeroFormattingBehaviorDropLeading) {
			for (i = 0; i < count && fields[i].value == 0; i++) {
				fields[i].keep = NO;
			}
		}
		if (_zeroFormattingBehavior & NSDateComponentsFormatterZeroFormattingBehaviorDropTrailing) {
			for (i = count - 1; i >= 0 && fields[i].value == 0; i--) {
				fields[i].keep = NO;
			}
		}
		if (_zeroFormattingBehavior & NSDateComponentsFormatterZeroFormattingBehaviorDropMiddle) {
			/* A zero is in the MIDDLE when a KEPT non-zero lies on either side of it — Apple's
			 * "dropped from anywhere in the middle of a sequence". */
			for (i = 0; i < count; i++) {
				int before = 0, after = 0, j;
				if (fields[i].value != 0) {
					continue;
				}
				for (j = 0; j < i; j++) {
					if (fields[j].keep && fields[j].value != 0) { before = 1; break; }
				}
				for (j = i + 1; j < count; j++) {
					if (fields[j].keep && fields[j].value != 0) { after = 1; break; }
				}
				if (before && after) {
					fields[i].keep = NO;
				}
			}
		}
	}

	/* THE UNIT LIMIT, applied AFTER the zero rules because it limits what is DISPLAYED: "the maximum
	 * number of time units to include in the output string". */
	if (_maximumUnitCount > 0) {
		NSInteger kept = 0;

		for (i = 0; i < count; i++) {
			if (!fields[i].keep) {
				continue;
			}
			kept++;
			if (kept > _maximumUnitCount) {
				fields[i].keep = NO;
			}
		}
	}

	/* NOTHING LEFT is not an empty string: a duration with no displayed unit has no text, which is how
	 * this family reports "there was nothing to say" everywhere else. */
	{
		int any = 0;

		for (i = 0; i < count; i++) {
			if (fields[i].keep) {
				any = 1;
				break;
			}
		}
		if (!any) {
			return nil;
		}
	}

	answer = [NSMutableString string];
	/* THE LAST KEPT FIELD is the only one that may carry a fraction (`allowsFractionalUnits`), because a
	 * fraction belongs to the smallest unit shown. */
	for (i = count - 1; i >= 0; i--) {
		if (fields[i].keep) {
			last = i;
			break;
		}
	}
	if (last < 0) {
		last = count - 1;
	}

	for (i = 0; i < count; i++) {
		NSString *text = nil;
		double value;

		if (!fields[i].keep) {
			continue;
		}
		value = (fractionalAllowed && i == last) ? (double)fields[i].value
							: (double)(long long)fields[i].value;
		if (positional) {
			/* THE CLOCK FORM IS OURS: the kept values joined by ":", every field after the first padded
			 * to two digits so 1 hour 3 minutes 37 seconds reads "1:03:37". The FIRST field is the one
			 * that may exceed two digits (days, or years of hours), so it is printed as it is. */
			char piece[32];

			if ([answer length] == 0) {
				snprintf(piece, sizeof piece, "%lld", (long long)fields[i].value);
			} else {
				snprintf(piece, sizeof piece, "%02lld", (long long)fields[i].value);
			}
			text = [NSString stringWithUTF8String:piece];
		} else if (_unitsStyle == NSDateComponentsFormatterUnitsStyleSpellOut) {
			NSString *spelled = fn_dcf_spelled_number(value);
			NSString *name = fn_dcf_unit_name(fields[i].icu, value);

			if (spelled != nil && name != nil) {
				text = [NSString stringWithFormat:@"%@ %@", spelled, name];
			}
		} else {
			const char *width;

			switch (_unitsStyle) {
			case NSDateComponentsFormatterUnitsStyleShort:
				width = "unit-width-short";
				break;
			case NSDateComponentsFormatterUnitsStyleBrief:
			case NSDateComponentsFormatterUnitsStyleAbbreviated:
				width = "unit-width-narrow";
				break;
			default:
				width = "unit-width-full-name";
				break;
			}
			text = fn_dcf_icu_text(width, fields[i].icu, value);
		}
		if (text == nil) {
			return nil;
		}
		if ([answer length] > 0) {
			/* THE JOINER IS OURS: a comma between units. The design pass's first proposal — the locale's
			 * list pattern through ulistfmt — would produce "1 hour AND 2 minutes", because ulistfmt is
			 * the CONJUNCTION formatter. See the header.
			 *
			 * AND THE SEPARATOR DEPENDS ON THE STYLE, WHICH THE PROBE MEASURED RATHER THAN ASSUMED: Apple's
			 * own worked examples give the ABBREVIATED form as "1h 0m 30s" — SPACES — while the word
			 * styles read as a list. So the abbreviated styles join with a space, the spelled ones with a
			 * comma, and the clock form with a colon. */
			if (positional) {
				[answer appendString:@":"];
			} else if (_unitsStyle == NSDateComponentsFormatterUnitsStyleBrief ||
				   _unitsStyle == NSDateComponentsFormatterUnitsStyleAbbreviated) {
				[answer appendString:@" "];
			} else {
				[answer appendString:@", "];
			}
		}
		[answer appendString:text];
	}

	/* THE TWO PHRASES. Apple publishes the EFFECT and not the words; the words are Apple's own localized
	 * resources, which this build cannot reach, so they are ENGLISH and that is the registered deviation
	 * the header names. */
	if (_includesApproximationPhrase) {
		NSString *prefixed = [NSString stringWithFormat:@"about %@", answer];

		return prefixed;
	}
	if (_includesTimeRemainingPhrase) {
		NSString *suffixed = [NSString stringWithFormat:@"%@ left", answer];

		return suffixed;
	}
	return answer;
}

/* THE FIELDS, FROM A COMPONENTS BAG: which units the mask allows, and what each one holds. */
- (nullable NSString *)fnStringForComponents:(NSDateComponents *)components
{
	fn_dcf_field fields[FN_DCF_MAX_UNITS];
	NSCalendarUnit allowed = (_allowedUnits != 0) ? _allowedUnits : fn_dcf_default_units();
	int count = 0;
	int i;

	for (i = 0; i < FN_DCF_MAX_UNITS; i++) {
		if (!fn_dcf_allows(allowed, fn_dcf_template[i].unit)) {
			continue;
		}
		fields[count] = fn_dcf_template[i];
		switch (fields[count].unit) {
		case NSCalendarUnitYear:	fields[count].value = [components year]; break;
		case NSCalendarUnitMonth:	fields[count].value = [components month]; break;
		case NSCalendarUnitWeekOfYear:
			/* EITHER WEEK SPELLING, since they are one duration unit and NSDateComponents can carry
			 * either. */
			fields[count].value = [components weekOfYear] != 0 ? [components weekOfYear]
									   : [components weekOfMonth];
			break;
		case NSCalendarUnitDay:		fields[count].value = [components day]; break;
		case NSCalendarUnitHour:	fields[count].value = [components hour]; break;
		case NSCalendarUnitMinute:	fields[count].value = [components minute]; break;
		case NSCalendarUnitSecond:	fields[count].value = [components second]; break;
		default:			fields[count].value = 0; break;
		}
		/* An UNSET field is NSDateComponentUndefined (NSIntegerMax), not zero — the sentinel W11's
		 * relative formatter was taught by a probe. Here it means "not stated", which for a duration is
		 * a zero. */
		if (fields[count].value == NSDateComponentUndefined) {
			fields[count].value = 0;
		}
		fields[count].keep = YES;
		count++;
	}
	if (count == 0) {
		return nil;
	}

	/* COLLAPSING THE LARGEST UNIT — Apple's "when a certain threshold is met", with the threshold never
	 * published. THIS FILE'S READING, stated in the header: the largest non-zero field is not printed and
	 * is folded into the next smaller ALLOWED field. Only the fields whose conversion is an exact fixed
	 * factor are folded (week->day, day->hour, hour->minute, minute->second); a year or a month is NOT
	 * folded, because its length depends on the calendar and Apple publishes no rule for it. */
	if (_collapsesLargestUnit) {
		int largest = -1;

		for (i = 0; i < count; i++) {
			if (fields[i].value != 0) {
				largest = i;
				break;
			}
		}
		if (largest >= 0 && largest + 1 < count) {
			long long factor = 0;
			NSInteger v = fields[largest].value;

			switch (fields[largest].unit) {
			case NSCalendarUnitWeekOfYear:	factor = 7; break;
			case NSCalendarUnitDay:		factor = 24; break;
			case NSCalendarUnitHour:	factor = 60; break;
			case NSCalendarUnitMinute:	factor = 60; break;
			default:			factor = 0; break;
			}
			if (factor != 0) {
				fields[largest + 1].value += v * factor;
				fields[largest].value = 0;
			}
		}
	}
	return [self fnStringForFields:fields count:count];
}

/* --------------------------------------------------------------------------------------------------- */

- (nullable NSString *)stringFromDateComponents:(NSDateComponents *)components
{
	if (components == nil) {
		return nil;
	}
	return [self fnStringForComponents:components];
}

- (nullable NSString *)stringFromDate:(NSDate *)startDate toDate:(NSDate *)endDate
{
	NSCalendar *calendar = [self fnCalendar];
	NSDateComponents *difference;

	if (startDate == nil || endDate == nil || calendar == nil) {
		return nil;
	}
	/* THE CALENDAR DOES THE SUBTRACTION, so a month is a month. This is the same choice W11's relative
	 * formatter makes, and for the same reason: this system has the calendar, so approximating would be a
	 * difference Apple does not have. */
	difference = [calendar components:(NSCalendarUnitYear | NSCalendarUnitMonth | NSCalendarUnitDay |
					  NSCalendarUnitHour | NSCalendarUnitMinute | NSCalendarUnitSecond)
				 fromDate:startDate
				   toDate:endDate
				  options:0];
	if (difference == nil) {
		return nil;
	}
	return [self fnStringForComponents:difference];
}

- (nullable NSString *)stringFromTimeInterval:(NSTimeInterval)timeInterval
{
	NSDate *start = [self fnReferenceDate];

	if (start == nil) {
		return nil;
	}
	return [self stringFromDate:start toDate:[start dateByAddingTimeInterval:timeInterval]];
}

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	/* Apple's abstract: "Returns a formatted string based on the date information in the specified
	 * object." A components bag IS that information; a date is measured against the reference date (which
	 * is what a duration formatter can say about a single date). Anything else answers nil. */
	if ([object isKindOfClass:[NSDateComponents class]]) {
		return [self stringFromDateComponents:(NSDateComponents *)object];
	}
	if ([object isKindOfClass:[NSDate class]]) {
		return [self stringFromDate:[self fnReferenceDate] toDate:(NSDate *)object];
	}
	return nil;
}

+ (nullable NSString *)localizedStringFromDateComponents:(NSDateComponents *)components
					      unitsStyle:(NSDateComponentsFormatterUnitsStyle)unitsStyle
{
	NSDateComponentsFormatter *formatter = [[[self alloc] init] autorelease];

	if (components == nil) {
		return nil;
	}
	[formatter setUnitsStyle:unitsStyle];
	return [formatter stringFromDateComponents:components];
}

- (BOOL)getObjectValue:(id _Nullable * _Nullable)object
	     forString:(NSString *)string
      errorDescription:(NSString * _Nullable * _Nullable)error
{
	(void)string;
	if (object != NULL) {
		*object = nil;
	}
	if (error != NULL) {
		*error = nil;
	}
	/* APPLE'S OWN WORDS, from this method's page: "currently only implements formatting, not parsing.
	 * Until it implements parsing, this will always return NO." So it ANSWERS, and it does not raise —
	 * which is what the abstract base does and what this class must not. */
	return NO;
}

- (NSCalendarUnit)allowedUnits { return _allowedUnits; }
- (void)setAllowedUnits:(NSCalendarUnit)units { _allowedUnits = units; }
- (NSDateComponentsFormatterUnitsStyle)unitsStyle { return _unitsStyle; }
- (void)setUnitsStyle:(NSDateComponentsFormatterUnitsStyle)style { _unitsStyle = style; }
- (NSDateComponentsFormatterZeroFormattingBehavior)zeroFormattingBehavior { return _zeroFormattingBehavior; }
- (void)setZeroFormattingBehavior:(NSDateComponentsFormatterZeroFormattingBehavior)behavior
{
	_zeroFormattingBehavior = behavior;
}
- (NSInteger)maximumUnitCount { return _maximumUnitCount; }
- (void)setMaximumUnitCount:(NSInteger)count { _maximumUnitCount = count; }
- (NSCalendar *)calendar { return [self fnCalendar]; }
- (void)setCalendar:(nullable NSCalendar *)value
{
	id copy = [value copy];

	[_calendar release];
	_calendar = copy;
}
- (nullable NSDate *)referenceDate { return _referenceDate; }
- (void)setReferenceDate:(nullable NSDate *)value
{
	id copy = [value copy];

	[_referenceDate release];
	_referenceDate = copy;
}
- (NSFormattingContext)formattingContext { return _formattingContext; }
- (void)setFormattingContext:(NSFormattingContext)context { _formattingContext = context; }
- (BOOL)allowsFractionalUnits { return _allowsFractionalUnits; }
- (void)setAllowsFractionalUnits:(BOOL)value { _allowsFractionalUnits = value; }
- (BOOL)collapsesLargestUnit { return _collapsesLargestUnit; }
- (void)setCollapsesLargestUnit:(BOOL)value { _collapsesLargestUnit = value; }
- (BOOL)includesApproximationPhrase { return _includesApproximationPhrase; }
- (void)setIncludesApproximationPhrase:(BOOL)value { _includesApproximationPhrase = value; }
- (BOOL)includesTimeRemainingPhrase { return _includesTimeRemainingPhrase; }
- (void)setIncludesTimeRemainingPhrase:(BOOL)value { _includesTimeRemainingPhrase = value; }

@end
