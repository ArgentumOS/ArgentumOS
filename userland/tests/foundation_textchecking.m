/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_textchecking — `NSTextCheckingResult` as the general class, and the vocabulary that goes with it
 * (docs/design/foundation-plan.md §62.18/§62.19). ONE unit, importing only <Foundation/Foundation.h>.
 *
 * WHAT THIS PROBE CAN AND CANNOT ASSERT ABOUT THE VALUES, stated first because it is the whole reason the
 * vocabulary is checked the way it is: APPLE PUBLISHES THE THIRTEEN CASE NAMES AND NO NUMBER FOR ANY OF THEM, so
 * the bits are ours (§11.6.1 D2) and the checks claim only what is TRUE OF OUR OWN: the types are distinct bits,
 * the masks are disjoint, the twelve keys are distinct strings (two keys sharing a value would make a component
 * unaddressable). A check that compared a bit against Apple's would have nothing to compare against and would be
 * asserting a value it had invented.
 *
 * THE REST IS BEHAVIOUR, and each check is here because something can go wrong that is worth catching: a factory
 * that does not set its own type, a payload readable through the wrong door, `-copy` answering a NEW object and
 * dropping the payload (which is what the F13.16 version of this class did, invisibly, while ranges were all it
 * had), an `-isEqual:` that compares ranges and ignores what was found, and — for the regular-expression half —
 * a name map that disagrees with the engine's own numbering.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "FOUNDATION-TEXTCHECKING"

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf(PREFIX " %s ok\n", name);
	} else {
		failc++;
		printf(PREFIX " %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* THE THIRTEEN, IN THE ORDER APPLE'S PAGE LISTS THEM, so the probe walks the same list the header assigns bits
 * from rather than a second copy that could drift. */
static const NSTextCheckingType FN_TYPES[13] = {
	NSTextCheckingTypeOrthography, NSTextCheckingTypeSpelling, NSTextCheckingTypeGrammar,
	NSTextCheckingTypeDate, NSTextCheckingTypeAddress, NSTextCheckingTypeLink,
	NSTextCheckingTypeQuote, NSTextCheckingTypeDash, NSTextCheckingTypeReplacement,
	NSTextCheckingTypeCorrection, NSTextCheckingTypeRegularExpression,
	NSTextCheckingTypePhoneNumber, NSTextCheckingTypeTransitInformation
};


static BOOL fn_power_of_two(NSTextCheckingType value)
{
	return value != 0 && (value & (value - 1)) == 0;
}

int main(void)
{
	/* ---- THE VOCABULARY ------------------------------------------------------------------------- */
	{
		BOOL distinct = YES;
		BOOL single = YES;
		int i;
		int j;

		for (i = 0; i < 13; i++) {
			if (!fn_power_of_two(FN_TYPES[i])) {
				single = NO;
			}
			for (j = i + 1; j < 13; j++) {
				if (FN_TYPES[i] == FN_TYPES[j]) {
					distinct = NO;
				}
			}
		}
		check("checking-types-are-distinct-single-bits", distinct && single,
		      [[NSString stringWithFormat:@"distinct=%d single-bit=%d", (int)distinct, (int)single]
			UTF8String]);
	}
	{
		BOOL inside = YES;
		int i;

		for (i = 0; i < 13; i++) {
			if ((FN_TYPES[i] & NSTextCheckingAllSystemTypes) != FN_TYPES[i]) {
				inside = NO;
			}
		}
		check("the-three-masks-bracket-the-thirteen",
		      inside &&
		      (NSTextCheckingAllSystemTypes & NSTextCheckingAllCustomTypes) == 0 &&
		      (NSTextCheckingAllSystemTypes | NSTextCheckingAllCustomTypes) == NSTextCheckingAllTypes &&
		      NSTextCheckingAllTypes == 0xFFFFFFFFFFFFFFFFULL,
		      [[NSString stringWithFormat:@"system=0x%llx custom=0x%llx all-inside=%d",
			(unsigned long long)NSTextCheckingAllSystemTypes,
			(unsigned long long)NSTextCheckingAllCustomTypes, (int)inside] UTF8String]);
	}
	{
		/* AN NSARRAY AND NOT A C ARRAY: the keys are `NSString *const` VARIABLES rather than compile-time
		 * constants, so a static initializer cannot hold them, and under ARC an array of object pointers is not
		 * a scalar this code would want to hand around anyway. */
		NSArray *keys = @[ NSTextCheckingNameKey, NSTextCheckingJobTitleKey,
				   NSTextCheckingOrganizationKey, NSTextCheckingStreetKey,
				   NSTextCheckingCityKey, NSTextCheckingStateKey, NSTextCheckingZIPKey,
				   NSTextCheckingCountryKey, NSTextCheckingPhoneKey,
				   NSTextCheckingAirlineKey, NSTextCheckingFlightKey ];
		BOOL distinct = [keys count] == 11;
		NSUInteger i;
		NSUInteger j;

		for (i = 0; i < [keys count]; i++) {
			NSString *key = [keys objectAtIndex:i];

			if ([key length] == 0) {
				distinct = NO;
			}
			for (j = i + 1; j < [keys count]; j++) {
				if ([key isEqualToString:[keys objectAtIndex:j]]) {
					distinct = NO;
				}
			}
		}
		check("the-eleven-keys-are-distinct-strings", distinct,
		      distinct ? "eleven distinct non-empty keys"
			       : "two of the component keys share a value");
	}

	/* ---- THE FACTORIES AND THEIR TYPE ----------------------------------------------------------- */
	{
		NSRange range = NSMakeRange(3, 5);
		NSTextCheckingResult *replacement =
			[NSTextCheckingResult replacementCheckingResultWithRange:range
						       replacementString:@"fixed"];
		NSTextCheckingResult *spelling = [NSTextCheckingResult spellCheckingResultWithRange:range];
		NSTextCheckingResult *dash =
			[NSTextCheckingResult dashCheckingResultWithRange:range replacementString:@"-"];
		NSTextCheckingResult *quote =
			[NSTextCheckingResult quoteCheckingResultWithRange:range replacementString:@"\""];
		NSTextCheckingResult *correction =
			[NSTextCheckingResult correctionCheckingResultWithRange:range
							      replacementString:@"correct"];
		/* A LOCAL HOLDS EACH URL, and that is this tree's idiom for a nullable factory feeding a non-nullable
		 * door (foundation_defaults.m does the same): `+URLWithString:` is annotated nullable and these calls
		 * are compiled with -Werror=nullable-to-nonnull-conversion. */
		NSURL *aURL = [NSURL URLWithString:@"http://a.example/"];
		NSTextCheckingResult *link =
			[NSTextCheckingResult linkCheckingResultWithRange:range URL:aURL];
		NSTextCheckingResult *phone =
			[NSTextCheckingResult phoneNumberCheckingResultWithRange:range
								   phoneNumber:@"555-1234"];
		NSTextCheckingResult *date =
			[NSTextCheckingResult dateCheckingResultWithRange:range date:[NSDate date]];
		NSTextCheckingResult *grammar =
			[NSTextCheckingResult grammarCheckingResultWithRange:range
								   details:[NSArray array]];
		BOOL types = [replacement resultType] == NSTextCheckingTypeReplacement &&
			     [spelling resultType] == NSTextCheckingTypeSpelling &&
			     [dash resultType] == NSTextCheckingTypeDash &&
			     [quote resultType] == NSTextCheckingTypeQuote &&
			     [correction resultType] == NSTextCheckingTypeCorrection &&
			     [link resultType] == NSTextCheckingTypeLink &&
			     [phone resultType] == NSTextCheckingTypePhoneNumber &&
			     [date resultType] == NSTextCheckingTypeDate &&
			     [grammar resultType] == NSTextCheckingTypeGrammar;

		check("every-factory-sets-its-own-type", types, "one factory reported the wrong result type");
		check("a-result-carries-only-its-own-payload",
		      [[link URL] isEqual:aURL] &&
		      [[phone phoneNumber] isEqualToString:@"555-1234"] &&
		      [[replacement replacementString] isEqualToString:@"fixed"] &&
		      [dash date] == nil && [link date] == nil && [phone URL] == nil &&
		      [spelling replacementString] == nil && [date grammarDetails] == nil,
		      "a payload was readable through a door that does not carry it, or was missing");
	}

	/* ---- RANGES --------------------------------------------------------------------------------- */
	{
		NSRange two[2];
		NSTextCheckingResult *multi;
		BOOL answered;

		two[0] = NSMakeRange(10, 4);
		two[1] = NSMakeRange(20, 6);
		multi = [NSTextCheckingResult resultWithRanges:two count:2];
		answered = [multi numberOfRanges] == 2 &&
			   [multi range].location == 10 && [multi range].length == 4 &&
			   [multi rangeAtIndex:1].location == 20 && [multi rangeAtIndex:1].length == 6;
		check("ranges-answer-and-a-past-the-end-index-is-not-found",
		      answered && [multi rangeAtIndex:9].location == NSNotFound &&
		      [multi rangeAtIndex:9].length == 0,
		      [[NSString stringWithFormat:@"n=%lu first=(%lu,%lu) second=(%lu,%lu) past=%lu",
			(unsigned long)[multi numberOfRanges], (unsigned long)[multi range].location,
			(unsigned long)[multi range].length, (unsigned long)[multi rangeAtIndex:1].location,
			(unsigned long)[multi rangeAtIndex:1].length,
			(unsigned long)[multi rangeAtIndex:9].location] UTF8String]);
	}
	{
		NSTextCheckingResult *spelling =
			[NSTextCheckingResult spellCheckingResultWithRange:NSMakeRange(5, 3)];
		NSTextCheckingResult *shifted = [spelling resultByAdjustingRangesWithOffset:2];
		BOOL raised = NO;
		BOOL named = NO;
		BOOL answer = shifted != nil && [shifted range].location == 7 &&
			      [shifted range].length == 3 &&
			      [shifted resultType] == NSTextCheckingTypeSpelling;

		@try {
			(void)[spelling resultByAdjustingRangesWithOffset:-10];
		} @catch (NSException *e) {
			raised = YES;
			named = [[e name] isEqualToString:NSInvalidArgumentException];
		}
		check("adjusting-ranges-shifts-and-refuses-a-negative-shift", answer && raised && named,
		      [[NSString stringWithFormat:@"shifted=%d raised=%d named=%d",
			(int)answer, (int)raised, (int)named] UTF8String]);
	}

	/* ---- IDENTITY ------------------------------------------------------------------------------- */
	{
		NSRange range = NSMakeRange(1, 2);
		NSURL *bURL = [NSURL URLWithString:@"http://b.example/"];
		NSTextCheckingResult *link =
			[NSTextCheckingResult linkCheckingResultWithRange:range URL:bURL];
		id copied = [link copy];

		check("copy-is-the-receiver-and-keeps-the-payload",
		      copied == link &&
		      [[(NSTextCheckingResult *)copied URL] isEqual:bURL],
		      "an immutable result must answer itself from -copy, with its payload intact");
	}
	{
		NSRange range = NSMakeRange(1, 2);
		NSURL *cURL = [NSURL URLWithString:@"http://c.example/"];
		NSURL *dURL = [NSURL URLWithString:@"http://d.example/"];
		NSTextCheckingResult *a =
			[NSTextCheckingResult linkCheckingResultWithRange:range URL:cURL];
		NSTextCheckingResult *same =
			[NSTextCheckingResult linkCheckingResultWithRange:range URL:cURL];
		NSTextCheckingResult *other =
			[NSTextCheckingResult linkCheckingResultWithRange:range URL:dURL];
		NSTextCheckingResult *otherType = [NSTextCheckingResult spellCheckingResultWithRange:range];

		check("equality-compares-the-payload",
		      [a isEqual:same] && [a hash] == [same hash] &&
		      ![a isEqual:other] && ![a isEqual:otherType] && ![a isEqual:otherType] &&
		      [a isEqual:a],
		      "two results of one kind and range that found different things must not be equal");
	}
	{
		NSDictionary *address = @{ NSTextCheckingCityKey : @"Springfield" };
		NSTextCheckingResult *result =
			[NSTextCheckingResult addressCheckingResultWithRange:NSMakeRange(0, 4)
								  components:address];

		NSDictionary *general = [result components];
		NSDictionary *addressDoor = [result addressComponents];

		check("components-is-the-general-door",
		      [general isEqual:addressDoor] &&
		      [[[result components] objectForKey:NSTextCheckingCityKey]
			isEqualToString:@"Springfield"],
		      "an address result answers one dictionary through both doors");
	}

	/* ---- THE REGULAR-EXPRESSION HALF, WHERE THE CLASS IS USED AS DESIGNED ------------------------ */
	{
		NSError *error = nil;
		NSRegularExpression *expression =
			[NSRegularExpression regularExpressionWithPattern:@"[0-9]+" options:0 error:&error];
		NSTextCheckingResult *match =
			[expression firstMatchInString:@"abc 123" options:0 range:NSMakeRange(0, 7)];

		check("a-matches-result-is-typed-and-carries-its-expression",
		      match != nil && [match resultType] == NSTextCheckingTypeRegularExpression &&
		      [[match regularExpression] isEqual:expression] &&
		      [match range].location == 4 && [match range].length == 3,
		      [[NSString stringWithFormat:@"match=%s type=0x%llx at=%lu len=%lu",
			match != nil ? "yes" : "no", (unsigned long long)[match resultType],
			(unsigned long)[match range].location, (unsigned long)[match range].length]
			UTF8String]);
	}
	{
		NSError *error = nil;
		NSString *pattern = @"(?<year>[0-9][0-9][0-9][0-9])-(?<month>[0-9][0-9])";
		NSRegularExpression *expression =
			[NSRegularExpression regularExpressionWithPattern:pattern options:0 error:&error];
		NSTextCheckingResult *match =
			[expression firstMatchInString:@"on 2026-09-26" options:0 range:NSMakeRange(0, 13)];
		NSRange year = [match rangeWithName:@"year"];
		NSRange month = [match rangeWithName:@"month"];
		NSRange unknown = [match rangeWithName:@"nothing"];

		check("named-groups-answer-by-name",
		      expression != nil && match != nil &&
		      year.location == 3 && year.length == 4 &&
		      month.location == 8 && month.length == 2 &&
		      unknown.location == NSNotFound,
		      [[NSString stringWithFormat:@"year=(%lu,%lu) month=(%lu,%lu) unknown=%lu",
			(unsigned long)year.location, (unsigned long)year.length,
			(unsigned long)month.location, (unsigned long)month.length,
			(unsigned long)unknown.location] UTF8String]);

		/* THE PATTERN ITSELF IS STILL THE CALLER'S, and the engine's own numbering is unchanged - which is
		 * what makes a name an ADDITION to index addressing rather than a replacement for it. */
		check("the-translation-keeps-index-addressing-and-the-callers-pattern",
		      [[expression pattern] isEqualToString:pattern] &&
		      [expression numberOfCaptureGroups] == 2 &&
		      [match rangeAtIndex:1].location == year.location &&
		      [match rangeAtIndex:1].length == year.length &&
		      [match rangeAtIndex:2].location == month.location,
		      [[NSString stringWithFormat:@"pattern=%@ groups=%lu at1=(%lu,%lu)",
			[expression pattern], (unsigned long)[expression numberOfCaptureGroups],
			(unsigned long)[match rangeAtIndex:1].location,
			(unsigned long)[match rangeAtIndex:1].length] UTF8String]);
	}
	{
		/* THE SCAN MUST NOT MOVE A GROUP BOUNDARY: an escape and a bracket expression are the two places a
		 * blind `(?<` → `(` substitution would, and both are exercised here. */
		NSError *error = nil;
		NSRegularExpression *escaped =
			[NSRegularExpression regularExpressionWithPattern:@"a\\.b" options:0 error:&error];
		NSRegularExpression *classed =
			[NSRegularExpression regularExpressionWithPattern:@"val[(]x[)]" options:0 error:&error];
		NSRegularExpression *both =
			[NSRegularExpression regularExpressionWithPattern:@"(?<a>x)[(]y[)]" options:0 error:&error];
		NSTextCheckingResult *m = [both firstMatchInString:@"x(y)" options:0
							    range:NSMakeRange(0, 4)];
		NSRange named = [m rangeWithName:@"a"];

		check("the-translation-leaves-escapes-and-classes-alone",
		      escaped != nil && classed != nil && both != nil &&
		      [escaped numberOfMatchesInString:@"a.b" options:0 range:NSMakeRange(0, 3)] == 1 &&
		      [escaped numberOfMatchesInString:@"axb" options:0 range:NSMakeRange(0, 3)] == 0 &&
		      [classed numberOfMatchesInString:@"val(x)" options:0 range:NSMakeRange(0, 6)] == 1 &&
		      named.location == 0 && named.length == 1,
		      [[NSString stringWithFormat:@"escaped-hit=%lu escaped-miss=%lu class=%lu named=(%lu,%lu)",
			(unsigned long)[escaped numberOfMatchesInString:@"a.b" options:0
								  range:NSMakeRange(0, 3)],
			(unsigned long)[escaped numberOfMatchesInString:@"axb" options:0
								  range:NSMakeRange(0, 3)],
			(unsigned long)[classed numberOfMatchesInString:@"val(x)" options:0
								  range:NSMakeRange(0, 6)],
			(unsigned long)named.location, (unsigned long)named.length] UTF8String]);
	}
	{
		NSError *error = nil;
		NSRegularExpression *expression =
			[NSRegularExpression regularExpressionWithPattern:@"[0-9]+" options:0 error:&error];
		__block int seen = 0;
		__block NSString *last = nil;
		__block int stopped = 0;

		[expression enumerateMatchesInString:@"a1 b22 c333" options:0
					       range:NSMakeRange(0, 11)
					  usingBlock:^(NSTextCheckingResult *result, NSMatchingFlags flags,
						       BOOL *stop) {
			(void)flags;
			(void)stop;
			seen++;
			last = [@"a1 b22 c333" substringWithRange:[result range]];
		}];
		[expression enumerateMatchesInString:@"a1 b22 c333" options:0
					       range:NSMakeRange(0, 11)
					  usingBlock:^(NSTextCheckingResult *result, NSMatchingFlags flags,
						       BOOL *stop) {
			(void)result;
			(void)flags;
			stopped++;
			*stop = YES;
		}];
		check("the-block-enumerator-walks-matches-and-honours-stop",
		      seen == 3 && [last isEqualToString:@"333"] && stopped == 1,
		      [[NSString stringWithFormat:@"seen=%d last=%@ stopped=%d", seen, last, stopped]
			UTF8String]);
	}

	printf(PREFIX " RESULT ok=%d fail=%d\n", okc, failc);
	printf(PREFIX "-STATUS=%d\n", failc ? 1 : 0);
	printf(PREFIX " DONE\n");
	return failc ? 1 : 0;
}
