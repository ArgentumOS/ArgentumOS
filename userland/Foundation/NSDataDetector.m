/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDataDetector.m — the three detectors, MANUAL OWNERSHIP. docs/design/foundation-plan.md §62.19.
 *
 * THE RULES ARE SCANNERS RATHER THAN A BIGGER REGULAR EXPRESSION, and that is a decision rather than a habit:
 * a link's trailing punctuation, a date's validity and a phone number's bounding all depend on what surrounds
 * the match, and a POSIX ERE has no look-around to express that with. So each detector is a loop over the
 * UTF-16 units with the rules the header states, and each one produces CANDIDATES that are then resolved
 * against each other in one place (`fn_resolve`) — so the overlap rule exists once instead of three times.
 *
 * THE CHARACTER CLASSES ARE ASCII, DELIBERATELY. A full-width digit is not a digit to these scanners and a
 * Cyrillic 'а' is not the letter 'a'. That is stated here because it is the kind of thing a reader would
 * otherwise assume the other way round, and because the alternative (ICU's properties) would answer differently
 * from the rules the header publishes.
 */

#import <Foundation/NSDataDetector.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSURL.h>

#include <stdlib.h>
#include <string.h>

NSErrorDomain const NSDataDetectorErrorDomain = @"NSDataDetectorErrorDomain";

/* ---- CHARACTER CLASSES --------------------------------------------------------------------------- */

static BOOL fn_digit(unichar c)
{
	return c >= '0' && c <= '9';
}

static BOOL fn_letter(unichar c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static BOOL fn_lower(unichar c)
{
	return c >= 'A' && c <= 'Z' ? (unichar)(c + 32) : c;
}

/* READS DIGITS INTO A VALUE: how many it took, bounded by `most` (so a two-digit field cannot eat four), and
 * never more than the run in front of it. */
static NSUInteger fn_read_digits(NSString *text, NSUInteger from, NSUInteger end, NSUInteger most, NSInteger *value)
{
	NSUInteger n = 0;
	NSInteger v = 0;

	while (from + n < end && n < most) {
		unichar c = [text characterAtIndex:from + n];

		if (!fn_digit(c)) {
			break;
		}
		v = v * 10 + (NSInteger)(c - '0');
		n++;
	}
	*value = v;
	return n;
}

#define FN_WORD_MAX 16

/* A RUN OF LETTERS, LOWERCASED AND NUL-TERMINATED. The cap is what keeps a long word from being read as a
 * month or a meridiem: a block of text with forty letters in it is not a date field. */
static NSUInteger fn_read_word(NSString *text, NSUInteger from, NSUInteger end, unichar *out)
{
	NSUInteger n = 0;

	while (from + n < end && n + 1 < FN_WORD_MAX) {
		unichar c = [text characterAtIndex:from + n];

		if (!fn_letter(c)) {
			break;
		}
		out[n] = fn_lower(c);
		n++;
	}
	out[n] = 0;
	return n;
}

static const char *const FN_MONTH_NAMES[12] = {
	"january", "february", "march", "april", "may", "june",
	"july", "august", "september", "october", "november", "december"
};

/* A PREFIX OF AT LEAST THREE LETTERS IS THE MONTH, which is what makes `Sep`, `Sept` and `September` one month
 * without a table of abbreviations (and what keeps `June`/`July` apart, since they differ at the third letter). */
static NSInteger fn_month_for_word(const unichar *word, NSUInteger length)
{
	NSInteger month;

	if (length < 3) {
		return 0;
	}
	for (month = 0; month < 12; month++) {
		const char *name = FN_MONTH_NAMES[month];
		NSUInteger full = strlen(name);
		NSUInteger k;

		if (length > full) {
			continue;
		}
		for (k = 0; k < length; k++) {
			if (word[k] != (unichar)name[k]) {
				break;
			}
		}
		if (k == length) {
			return month + 1;
		}
	}
	return 0;
}

/* ---- CANDIDATES ----------------------------------------------------------------------------------
 *
 * ONE PLACE DECIDES WHAT TWO DETECTORS FOUND, and it is here rather than in the callers. The sort is total
 * (start, then length descending, then the detector's rank), so it does not depend on qsort being stable; and
 * the overlap test is a single comparison against the furthest end accepted so far, which is sound because
 * every accepted range starts at or before the candidate's own start. */

typedef struct {
	NSUInteger location;
	NSUInteger length;
	int rank;
	NSTextCheckingResult *result;
} fn_candidate;

typedef struct {
	fn_candidate *items;
	NSUInteger count;
	NSUInteger capacity;
} fn_candidate_list;

static void fn_candidates_add(fn_candidate_list *list, NSRange range, int rank, NSTextCheckingResult *result)
{
	if (range.length == 0 || result == nil) {
		return;
	}
	if (list->count == list->capacity) {
		NSUInteger next = list->capacity > 0 ? list->capacity * 2 : 16;
		fn_candidate *grown = realloc(list->items, next * sizeof(fn_candidate));

		if (grown == NULL) {
			return;
		}
		list->items = grown;
		list->capacity = next;
	}
	list->items[list->count].location = range.location;
	list->items[list->count].length = range.length;
	list->items[list->count].rank = rank;
	list->items[list->count].result = result;
	list->count++;
}

static int fn_candidate_compare(const void *left, const void *right)
{
	const fn_candidate *a = (const fn_candidate *)left;
	const fn_candidate *b = (const fn_candidate *)right;

	if (a->location != b->location) {
		return a->location < b->location ? -1 : 1;
	}
	if (a->length != b->length) {
		return a->length > b->length ? -1 : 1;
	}
	if (a->rank != b->rank) {
		return a->rank < b->rank ? -1 : 1;
	}
	return 0;
}

/* THE ACCEPTED SET, IN DOCUMENT ORDER, NONE OF ITEM OVERLAPPING ANOTHER. */
static NSArray *fn_resolve(fn_candidate_list *list)
{
	NSMutableArray *accepted = [NSMutableArray array];
	NSUInteger furthest = 0;
	NSUInteger i;

	if (list->count == 0) {
		return accepted;
	}
	qsort(list->items, list->count, sizeof(fn_candidate), fn_candidate_compare);
	for (i = 0; i < list->count; i++) {
		NSUInteger end = list->items[i].location + list->items[i].length;

		if (list->items[i].location < furthest) {
			continue;
		}
		[accepted addObject:list->items[i].result];
		furthest = end;
	}
	return accepted;
}

/* ---- LINKS --------------------------------------------------------------------------------------- */

static BOOL fn_is_uri_char(unichar c)
{
	if (fn_digit(c) || fn_letter(c)) {
		return YES;
	}
	switch (c) {
	case '-': case '.': case '_': case '~': case ':': case '/': case '?':
	case '#': case '[': case ']': case '@': case '!': case '$': case '&':
	case '\'': case '(': case ')': case '*': case '+': case ',': case ';':
	case '=': case '%':
		return YES;
	default:
		return NO;
	}
}

/* TRAILING PUNCTUATION IS NOT PART OF A LINK, and a closing bracket is part of one only when the link opened
 * it: `…/Foo_(bar)` keeps its parenthesis while `(see http://x.com)` does not keep the one that closed the
 * sentence. */
static NSRange fn_trim_link(NSString *text, NSRange range)
{
	BOOL again = YES;

	while (again && range.length > 1) {
		unichar last = [text characterAtIndex:range.location + range.length - 1];

		again = NO;
		switch (last) {
		case '.': case ',': case ';': case ':': case '!': case '?':
		case '"': case '\'': case '*': case '_':
			range.length--;
			again = YES;
			break;
		case ')': case ']': {
			unichar open = (last == ')') ? '(' : '[';
			NSUInteger opens = 0;
			NSUInteger closes = 0;
			NSUInteger k;

			for (k = 0; k < range.length; k++) {
				unichar c = [text characterAtIndex:range.location + k];

				if (c == open) {
					opens++;
				} else if (c == last) {
					closes++;
				}
			}
			if (closes > opens) {
				range.length--;
				again = YES;
			}
			break;
		}
		default:
			break;
		}
	}
	return range;
}

/* RANK 0: A LINK CLAIMS A SPAN BEFORE A DATE OR A PHONE DOES. */
static void fn_scan_links(NSString *text, NSRange limit, fn_candidate_list *out)
{
	NSUInteger end = limit.location + limit.length;
	NSUInteger i = limit.location;

	while (i < end) {
		NSUInteger start = i;
		NSUInteger after = 0;
		BOOL hasScheme = NO;
		BOOL isLink = NO;

		/* A TOKEN BOUNDARY BEFORE THE MATCH, or the scheme of one word would be the tail of another. */
		if (i > 0) {
			unichar prev = [text characterAtIndex:i - 1];

			if (fn_digit(prev) || fn_letter(prev) || prev == '.' || prev == '-') {
				i++;
				continue;
			}
		}
		if (fn_letter([text characterAtIndex:i])) {
			NSUInteger k = i + 1;

			while (k < end) {
				unichar c = [text characterAtIndex:k];

				if (fn_digit(c) || fn_letter(c) || c == '+' || c == '.' || c == '-') {
					k++;
					continue;
				}
				break;
			}
			if (k + 3 <= end && [text characterAtIndex:k] == ':' &&
			    [text characterAtIndex:k + 1] == '/' && [text characterAtIndex:k + 2] == '/') {
				isLink = YES;
				hasScheme = YES;
				after = k + 3;
			}
		}
		if (!isLink && start + 4 <= end) {
			/* A BARE `www.` HOST, which is how a person writes one. */
			BOOL www = YES;
			NSUInteger k;

			for (k = 0; k < 4; k++) {
				if (fn_lower([text characterAtIndex:start + k]) != (unichar)"www."[k]) {
					www = NO;
					break;
				}
			}
			if (www && start + 5 <= end) {
				unichar host = [text characterAtIndex:start + 4];

				if (fn_digit(host) || fn_letter(host)) {
					isLink = YES;
					hasScheme = NO;
					after = start + 4;
				}
			}
		}
		if (isLink) {
			NSUInteger k = after;
			NSRange range;
			NSString *matched;
			NSString *target;
			NSURL *url;

			while (k < end && fn_is_uri_char([text characterAtIndex:k])) {
				k++;
			}
			range = fn_trim_link(text, NSMakeRange(start, k - start));
			matched = [text substringWithRange:range];
			/* A SCHEME-LESS MATCH IS GIVEN `http` WHEN THE URL IS BUILT: a bare host is not something this
			 * library's NSURL can parse, and a link result whose URL is nil would be no use to a caller. */
			target = hasScheme ? matched : [@"http://" stringByAppendingString:matched];
			url = [NSURL URLWithString:target];
			if (url != nil) {
				fn_candidates_add(out, range, 0,
						  [NSTextCheckingResult linkCheckingResultWithRange:range
												URL:url]);
			}
			i = start + 1;		/* a scheme inside a link is not a second link */
			continue;
		}
		i++;
	}
}

/* ---- PHONE NUMBERS ------------------------------------------------------------------------------- */

static BOOL fn_is_phone_separator(unichar c)
{
	return c == ' ' || c == '-' || c == '.' || c == '(' || c == ')' || c == '\t';
}

/* RANK 2: A PHONE NUMBER IS THE LAST CLAIM ON A SPAN, because its rule is the loosest - `2026-09-26` is a
 * hyphenated eight-digit run as well as a date, and the date has more structure to stand on. */
static void fn_scan_phones(NSString *text, NSRange limit, fn_candidate_list *out)
{
	NSUInteger end = limit.location + limit.length;
	NSUInteger i = limit.location;

	while (i < end) {
		unichar c = [text characterAtIndex:i];
		BOOL plus = (c == '+');
		NSUInteger start = i;
		NSUInteger k;
		NSUInteger digits = 0;
		NSUInteger lastDigit = 0;
		BOOL separator = NO;
		BOOL pendingSeparator = NO;

		if (!plus && !fn_digit(c)) {
			i++;
			continue;
		}
		/* A LEADING OPENING PARENTHESIS IS PART OF THE NUMBER: `(555) 123-4567` is one token, and starting the
		 * match at the `5` would answer a range that does not cover the text a caller would highlight. The
		 * parenthesis is taken only when nothing alphanumeric precedes it, so a `(` that closes a word is not
		 * pulled in. */
		if (plus == NO && i > limit.location && [text characterAtIndex:i - 1] == '(' &&
		    (i - 1 == 0 || (!fn_digit([text characterAtIndex:i - 2]) &&
				    !fn_letter([text characterAtIndex:i - 2])))) {
			start = i - 1;
		}
		if (start > 0) {
			unichar prev = [text characterAtIndex:start - 1];

			if (fn_digit(prev) || fn_letter(prev) || prev == '+' || prev == '-' || prev == '.') {
				i++;
				continue;
			}
		}
		k = i + (plus ? 1 : 0);
		while (k < end) {
			unichar x = [text characterAtIndex:k];

			if (fn_digit(x)) {
				/* A SEPARATOR COUNTS ONLY WHEN A DIGIT FOLLOWS IT. The first version set the flag on any
				 * space, so the space AFTER a bare run of digits made `20260926` a "separated" number - which
				 * is exactly the integer this rule exists to refuse. */
				if (pendingSeparator) {
					separator = YES;
					pendingSeparator = NO;
				}
				digits++;
				k++;
				lastDigit = k;
				continue;
			}
			if (fn_is_phone_separator(x)) {
				pendingSeparator = YES;
				k++;
				continue;
			}
			break;
		}
		/* THE RUN ENDS AT THE LAST DIGIT, so a trailing separator is not part of the number. */
		if (digits >= 7 && digits <= 15) {
			/* THE BOUNDARY AFTER IT: a run inside a longer number or inside a word is not a number. */
			BOOL bounded = YES;

			if (lastDigit < end) {
				unichar next = [text characterAtIndex:lastDigit];

				if (fn_digit(next) || fn_letter(next)) {
					bounded = NO;
				}
			}
			/* AND A BARE SEVEN-DIGIT INTEGER IS NOT ONE: with no separator and no `+`, the run has to be at
			 * least ten digits before this class will call it a phone number. */
			if (bounded && (separator || plus || digits >= 10)) {
				NSRange range = NSMakeRange(start, lastDigit - start);

				fn_candidates_add(out, range, 2,
						  [NSTextCheckingResult phoneNumberCheckingResultWithRange:range
												   phoneNumber:[text substringWithRange:range]]);
				i = lastDigit;
				continue;
			}
		}
		i++;
	}
}

/* ---- DATES AND TIMES ----------------------------------------------------------------------------- */

/* A TIME OF DAY, WHICH IS `H:MM[:SS]` OR AN HOUR WITH A MERIDIEM - and a bare hour is never a time, or every
 * number in a sentence would be one. Returns the end of the match and an hour in 0..23. */
static BOOL fn_read_time(NSString *text, NSUInteger from, NSUInteger end,
			 NSUInteger *outEnd, NSInteger *outHour, NSInteger *outMinute, NSInteger *outSecond)
{
	NSInteger hour = 0;
	NSInteger minute = 0;
	NSInteger second = 0;
	NSUInteger units = fn_read_digits(text, from, end, 2, &hour);
	NSUInteger k = from + units;
	BOOL hasColon = NO;

	if (units == 0) {
		return NO;
	}
	if (k < end && [text characterAtIndex:k] == ':') {
		NSInteger mm;
		NSUInteger mn = fn_read_digits(text, k + 1, end, 2, &mm);

		if (mn != 2) {
			return NO;
		}
		hasColon = YES;
		minute = mm;
		k += 1 + mn;
		if (k < end && [text characterAtIndex:k] == ':') {
			NSInteger ss;
			NSUInteger sn = fn_read_digits(text, k + 1, end, 2, &ss);

			if (sn == 2) {
				second = ss;
				k += 1 + sn;
			}
		}
	}
	{
		NSUInteger mer = k;
		NSInteger meridiem = 0;		/* 0 none, 1 am, 2 pm */

		if (mer < end && [text characterAtIndex:mer] == ' ') {
			mer++;
		}
		if (mer + 2 <= end) {
			unichar a = fn_lower([text characterAtIndex:mer]);
			unichar b = fn_lower([text characterAtIndex:mer + 1]);

			if ((a == 'a' || a == 'p') && b == 'm' &&
			    (mer + 2 >= end || !fn_letter([text characterAtIndex:mer + 2]))) {
				meridiem = (a == 'p') ? 2 : 1;
				k = mer + 2;
			}
		}
		if (meridiem == 0 && !hasColon) {
			return NO;		/* an hour on its own is not a time */
		}
		if (meridiem == 2) {
			if (hour < 12) {
				hour += 12;
			}
		} else if (meridiem == 1 && hour == 12) {
			hour = 0;
		}
	}
	if (hour > 23 || minute > 59 || second > 59) {
		return NO;
	}
	*outEnd = k;
	*outHour = hour;
	*outMinute = minute;
	*outSecond = second;
	return YES;
}

/* THE DATE, BUILT FROM COMPONENTS AND THEN ASKED WHETHER IT EXISTS. `-isValidDateInCalendar:` is a round trip
 * through the calendar, so `February 30` fails here for the calendar's own reason rather than for a second
 * notion of validity written in this file. Nil means "not a date", and the caller then makes no result. */
static NSDate *fn_build_date(NSCalendar *calendar, NSInteger year, NSInteger month, NSInteger day,
			     NSInteger hour, NSInteger minute, NSInteger second)
{
	NSDateComponents *components = [[NSDateComponents alloc] init];
	NSDate *date = nil;

	[components setYear:year];
	[components setMonth:month];
	[components setDay:day];
	[components setHour:hour];
	[components setMinute:minute];
	[components setSecond:second];
	if ([components isValidDateInCalendar:calendar]) {
		date = [calendar dateFromComponents:components];
	}
	[components release];
	return date;
}

typedef struct {
	NSInteger year;
	NSInteger month;
	NSInteger day;
	NSUInteger dateEnd;
	BOOL hasDate;
} fn_date_match;

/* THE FOUR DATE FORMS, EACH ANCHORED AT `from` AND EACH REQUIRING ITS FIELDS TO BE BOUNDED. Returns whether
 * one matched; a match carries the fields and the end of the DATE part only (the caller may extend it with a
 * time). */
static BOOL fn_read_date(NSString *text, NSUInteger from, NSUInteger end, fn_date_match *out)
{
	NSInteger a = 0;
	NSInteger b = 0;
	NSInteger c = 0;
	NSUInteger n1;
	NSUInteger n2;
	NSUInteger n3;
	unichar word[FN_WORD_MAX];

	/* YYYY-MM-DD */
	n1 = fn_read_digits(text, from, end, 4, &a);
	if (n1 == 4 && from + n1 < end && [text characterAtIndex:from + n1] == '-') {
		n2 = fn_read_digits(text, from + n1 + 1, end, 2, &b);
		if (n2 == 2 && from + n1 + 1 + n2 < end &&
		    [text characterAtIndex:from + n1 + 1 + n2] == '-') {
			n3 = fn_read_digits(text, from + n1 + 1 + n2 + 1, end, 2, &c);
			if (n3 == 2) {
				out->year = a;
				out->month = b;
				out->day = c;
				out->dateEnd = from + n1 + 1 + n2 + 1 + n3;
				out->hasDate = YES;
				return YES;
			}
		}
	}
	/* M/D/YYYY */
	n1 = fn_read_digits(text, from, end, 2, &a);
	if (n1 >= 1 && from + n1 < end && [text characterAtIndex:from + n1] == '/') {
		n2 = fn_read_digits(text, from + n1 + 1, end, 2, &b);
		if (n2 >= 1 && from + n1 + 1 + n2 < end &&
		    [text characterAtIndex:from + n1 + 1 + n2] == '/') {
			n3 = fn_read_digits(text, from + n1 + 1 + n2 + 1, end, 4, &c);
			NSUInteger after = from + n1 + 1 + n2 + 1 + n3;

			if (n3 == 4 && (after >= end || !fn_digit([text characterAtIndex:after]))) {
				out->year = c;
				out->month = a;
				out->day = b;
				out->dateEnd = after;
				out->hasDate = YES;
				return YES;
			}
		}
	}
	/* Month D, YYYY */
	{
		NSUInteger n = fn_read_word(text, from, end, word);
		NSInteger month = fn_month_for_word(word, n);

		if (month != 0) {
			NSUInteger k = from + n;

			if (k < end && [text characterAtIndex:k] == '.') {
				k++;
			}
			while (k < end && [text characterAtIndex:k] == ' ') {
				k++;
			}
			n1 = fn_read_digits(text, k, end, 2, &a);
			if (n1 >= 1) {
				k += n1;
				if (k < end && [text characterAtIndex:k] == ',') {
					k++;
				}
				while (k < end && [text characterAtIndex:k] == ' ') {
					k++;
				}
				n2 = fn_read_digits(text, k, end, 4, &c);
				if (n2 == 4) {
					out->year = c;
					out->month = month;
					out->day = a;
					out->dateEnd = k + n2;
					out->hasDate = YES;
					return YES;
				}
			}
		}
	}
	/* D Month YYYY */
	{
		NSUInteger n = fn_read_digits(text, from, end, 2, &a);

		if (n >= 1 && from + n < end && [text characterAtIndex:from + n] == ' ') {
			NSUInteger k = from + n;

			while (k < end && [text characterAtIndex:k] == ' ') {
				k++;
			}
			{
				NSUInteger w = fn_read_word(text, k, end, word);
				NSInteger month = fn_month_for_word(word, w);

				if (month != 0) {
					k += w;
					if (k < end && [text characterAtIndex:k] == ',') {
						k++;
					}
					while (k < end && [text characterAtIndex:k] == ' ') {
						k++;
					}
					n2 = fn_read_digits(text, k, end, 4, &c);
					if (n2 == 4) {
						out->year = c;
						out->month = month;
						out->day = a;
						out->dateEnd = k + n2;
						out->hasDate = YES;
						return YES;
					}
				}
			}
		}
	}
	return NO;
}

/* RANK 1: A DATE CLAIMS A SPAN BEFORE A PHONE NUMBER DOES. */
static void fn_scan_dates(NSString *text, NSRange limit, NSCalendar *calendar, NSTimeZone *zone,
			  fn_candidate_list *out)
{
	NSUInteger end = limit.location + limit.length;
	NSUInteger i = limit.location;

	while (i < end) {
		fn_date_match date;
		NSUInteger matchEnd = 0;
		NSInteger hour = 0;
		NSInteger minute = 0;
		NSInteger second = 0;
		BOOL hasTime = NO;

		date.hasDate = NO;
		date.dateEnd = 0;
		if (i > 0 && fn_digit([text characterAtIndex:i - 1])) {
			i++;
			continue;		/* a date does not start inside a number */
		}
		if (fn_read_date(text, i, end, &date)) {
			/* A DATE AND A TIME ARE ONE RESULT WHEN A SPACE, A COMMA OR THE WORD `at` JOINS THEM, and only
			 * then: `2026-09-26 14:30` is one thing while `2026-09-26 14` is a date and a number. */
			NSUInteger k = date.dateEnd;

			if (k < end && ([text characterAtIndex:k] == ' ' || [text characterAtIndex:k] == ',')) {
				NSUInteger j = k;

				while (j < end && ([text characterAtIndex:j] == ' ' ||
						   [text characterAtIndex:j] == ',')) {
					j++;
				}
				if (j + 2 <= end && fn_lower([text characterAtIndex:j]) == 'a' &&
				    fn_lower([text characterAtIndex:j + 1]) == 't' &&
				    (j + 2 >= end || !fn_letter([text characterAtIndex:j + 2]))) {
					j += 2;
				}
				while (j < end && [text characterAtIndex:j] == ' ') {
					j++;
				}
				hasTime = fn_read_time(text, j, end, &matchEnd, &hour, &minute, &second);
			}
			if (!hasTime) {
				matchEnd = date.dateEnd;
			}
			{
				NSDate *made = fn_build_date(calendar, date.year, date.month, date.day,
							     hour, minute, second);

				if (made != nil) {
					NSRange range = NSMakeRange(i, matchEnd - i);

					fn_candidates_add(out, range, 1,
							  [NSTextCheckingResult dateCheckingResultWithRange:range
												       date:made
												   timeZone:zone
												   duration:0.0]);
					i = matchEnd;
					continue;
				}
			}
			i++;
			continue;
		}
		/* A TIME ON ITS OWN IS TODAY'S DATE AT THAT TIME. */
		if (fn_read_time(text, i, end, &matchEnd, &hour, &minute, &second)) {
			NSDateComponents *today = [calendar components:(NSCalendarUnitYear | NSCalendarUnitMonth |
								      NSCalendarUnitDay)
							       fromDate:[NSDate date]];

			if (today != nil) {
				NSDate *made = fn_build_date(calendar, [today year], [today month], [today day],
							     hour, minute, second);

				if (made != nil) {
					NSRange range = NSMakeRange(i, matchEnd - i);

					fn_candidates_add(out, range, 1,
							  [NSTextCheckingResult dateCheckingResultWithRange:range
												       date:made
												   timeZone:zone
												   duration:0.0]);
					i = matchEnd;
					continue;
				}
			}
		}
		i++;
	}
}

@implementation NSDataDetector

+ (nullable NSDataDetector *)dataDetectorWithTypes:(NSTextCheckingTypes)checkingTypes
					     error:(NSError ** _Nullable)error
{
	return [[[self alloc] initWithTypes:checkingTypes error:error] autorelease];
}

- (nullable instancetype)initWithTypes:(NSTextCheckingTypes)checkingTypes
				 error:(NSError ** _Nullable)error
{
	NSTextCheckingTypes detectors = NSTextCheckingTypeDate | NSTextCheckingTypeLink |
					NSTextCheckingTypePhoneNumber;
	NSTextCheckingTypes noSubstrate = NSTextCheckingTypeAddress |
					  NSTextCheckingTypeTransitInformation;

	if ((checkingTypes & ~(detectors | noSubstrate)) != 0) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSDataDetectorErrorDomain
						     code:NSDataDetectorTypeNotADataDetectorCode
						 userInfo:@{ NSLocalizedDescriptionKey :
							     @"that checking type is not one a data detector makes" }];
		}
		[self release];
		return nil;
	}
	if ((checkingTypes & noSubstrate) != 0) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSDataDetectorErrorDomain
						     code:NSDataDetectorTypeNeedsSubstrateCode
						 userInfo:@{ NSLocalizedDescriptionKey :
							     @"that checking type needs address or transit data "
							     @"this system does not have" }];
		}
		[self release];
		return nil;
	}
	if (checkingTypes == 0) {
		if (error != NULL) {
			*error = [NSError errorWithDomain:NSDataDetectorErrorDomain
						     code:NSDataDetectorTypeNotADataDetectorCode
						 userInfo:@{ NSLocalizedDescriptionKey :
							     @"no checking type was asked for" }];
		}
		[self release];
		return nil;
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_checkingTypes = checkingTypes;
	return self;
}

- (NSTextCheckingTypes)checkingTypes
{
	return _checkingTypes;
}

/* THE ONE METHOD EVERY OTHER MATCHING DOOR GOES THROUGH. The detectors run in the fixed order the header
 * states (link, date, phone), their candidates are resolved in one place, and the results come back in
 * document order. */
- (NSArray *)matchesInString:(NSString *)string
		     options:(NSMatchingOptions)options
		       range:(NSRange)range
{
	fn_candidate_list list;
	NSArray *accepted;
	NSUInteger length;
	NSCalendar *calendar = nil;
	NSTimeZone *zone = nil;

	(void)options;
	if (string == nil) {
		return [NSArray array];
	}
	length = [string length];
	if (range.location > length) {
		range.location = length;
	}
	if (range.location + range.length > length) {
		range.length = length - range.location;
	}
	if (range.length == 0) {
		return [NSArray array];
	}
	list.items = NULL;
	list.count = 0;
	list.capacity = 0;

	/* A LINE IS A LINK, AND A DATE NEEDS A CALENDAR AND A ZONE: one of each, made once, so every result of this
	 * call is read in the same zone. */
	if ((_checkingTypes & NSTextCheckingTypeLink) != 0) {
		fn_scan_links(string, range, &list);
	}
	if ((_checkingTypes & NSTextCheckingTypeDate) != 0) {
		zone = [NSTimeZone systemTimeZone];
		calendar = [[[NSCalendar alloc] initWithCalendarIdentifier:NSCalendarIdentifierGregorian] autorelease];
		if (calendar != nil && zone != nil) {
			[calendar setTimeZone:zone];
			fn_scan_dates(string, range, calendar, zone, &list);
		}
	}
	if ((_checkingTypes & NSTextCheckingTypePhoneNumber) != 0) {
		fn_scan_phones(string, range, &list);
	}
	accepted = fn_resolve(&list);
	free(list.items);
	return accepted;
}

/* A DETECTOR IS IMMUTABLE, so a copy is the receiver (the rule this library keeps for every immutable class,
 * §62.18) - and the parent's `-copy` would be wrong here twice over: it recompiles from `_pattern`, which a
 * detector does not have. */
- (id)copy
{
	return [self retain];
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSDataDetector class]]) {
		return NO;
	}
	return ((NSDataDetector *)other)->_checkingTypes == _checkingTypes;
}

- (NSUInteger)hash
{
	return (NSUInteger)(_checkingTypes ^ (_checkingTypes >> 32));
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSDataDetector types=0x%llx>",
					  (unsigned long long)_checkingTypes];
}

@end
