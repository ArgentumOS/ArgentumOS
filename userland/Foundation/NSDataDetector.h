/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDataDetector — natural-language text, searched for the things a person would point at. F13.16 built
 * `NSRegularExpression` and §62.18 made `NSTextCheckingResult` the general class; this is the subclass that
 * produces its natural-language kinds. docs/design/foundation-plan.md §62.19.
 *
 * IT IS A SUBCLASS OF NSRegularExpression BECAUSE THAT IS WHAT APPLE DECLARES, and the inheritance is real
 * rather than decorative: the matching methods a detector answers with are the parent's contract (`-matches…`,
 * `-numberOfMatches…`, `-firstMatch…`, `-rangeOfFirstMatch…`, `-enumerateMatchesInString:options:range:usingBlock:`),
 * and this class overrides the ONE method all of them funnel through. What it is NOT is a pattern: `-pattern`
 * answers nil and `-numberOfCaptureGroups` answers 0, because a detector is asked for KINDS and not for a
 * regular expression. Apple documents no pattern for one either.
 *
 * WHICH TYPES ARE SUPPORTED, AND WHICH TWO ARE NOT, AND WHY THE FOURTH CATEGORY DOES NOT EXIST.
 * Apple's own page for `+dataDetectorWithTypes:error:` states that the supported detectors are Date, Address,
 * Link, PhoneNumber and TransitInformation, and its `NSDataDetector` overview says the same in prose. That
 * splits the thirteen checking types into two groups, and this class keeps Apple's group exactly:
 *
 *   * ASKING FOR ANY OF THE OTHER EIGHT — Spelling, Orthography, Grammar, Correction, Quote, Dash,
 *     Replacement, RegularExpression — is an ERROR, which is Apple's contract rather than our boundary: those
 *     are the kinds `NSSpellChecker` and `NSRegularExpression` produce, and a detector is not the object to
 *     ask. No deviation is registered for them because there is no difference to register;
 *   * TWO OF APPLE'S FIVE NEED DATA THIS SYSTEM DOES NOT HAVE, and they are REFUSED WITH AN ERROR NAMING THE
 *     GROUND (§11.6 gate 2, ground (i) — "a dependency this system lacks"), rather than silently accepting an
 *     option that can never match:
 *       - **Address** needs a postal-address grammar and its per-country data (the thing that tells a detector
 *         that "CA" is a state here and a province there, and that an address has an end);
 *       - **TransitInformation** needs airline and flight-schedule data (its two keys are `NSTextCheckingAirlineKey`
 *         and `NSTextCheckingFlightKey`);
 *     both are register entries D15/D16 in the plan, and both are honest to REFUSE because Apple's own contract
 *     for this door is "if an error was encountered, returns nil and error contains the error".
 *
 * WHAT THE THREE SUPPORTED DETECTORS ACTUALLY RECOGNISE IS THE CONTRACT, SO IT IS WRITTEN DOWN HERE. Apple
 * publishes no acceptance rule for any of them - its pages say a detector "matches natural language text for
 * predefined data patterns" and stop - so which strings match is a PERMITTED VARIATION (§11.6 gate 1: where
 * Apple leaves behaviour undefined, any choice conforms) and a caller has to be able to read ours:
 *
 *   * LINK   — `scheme://…` where the scheme is `[A-Za-z][A-Za-z0-9+.-]*`, or a bare `www.` host. The run
 *              continues over the characters a URI may contain, then TRAILING PUNCTUATION IS TRIMMED (`.`,
 *              `,`, `;`, `:`, `!`, `?`, quotes, and a closing bracket only when it is unbalanced inside the
 *              match, so `…/Foo_(bar)` keeps its parenthesis). A bare `www.` host is given the `http` scheme
 *              when the NSURL is built, because a scheme-less string is not a URL this library can parse;
 *   * DATE   — four forms, and NOT a general natural-language date parser (relative phrases, durations and
 *              most locale formats are named as absent rather than half-done): `YYYY-MM-DD`; `M/D/YYYY`;
 *              `Month D, YYYY` and `D Month YYYY` (month names are accepted case-insensitively and by any
 *              PREFIX of at least three letters, which is what makes `Sep`, `Sept` and `September` the same
 *              month); and a TIME of `H:MM[:SS]` with an optional `AM`/`PM`. A date and a time joined by a
 *              space, a comma or the word `at` are ONE result. The date is built from NSDateComponents through
 *              NSCalendar in the system time zone and is REJECTED WHEN THE CALENDAR SAYS IT DOES NOT EXIST
 *              (`February 30` is not a match — `-isValidDateInCalendar:` is the test); a bare date means
 *              midnight and a bare time means today;
 *   * PHONE  — a bounded run of 7 to 15 digits with optional `+`, spaces, `-`, `.` and parentheses. It must be
 *              bounded (a run inside a longer number or a word is not one), it must not be a bare run of fewer
 *              than 10 digits with NO separator and no `+` (so a plain seven-digit integer is not a phone
 *              number), and its trailing separators are trimmed. THIS SYSTEM HAS NO NUMBERING-PLAN DATABASE,
 *              so what is detected is a SHAPE; whether a shape is a working number is not a question this
 *              class can answer, and Apple's detector is where that knowledge lives.
 *
 * OVERLAPS ARE RESOLVED BY ONE STATED RULE, because two detectors can claim the same text (`2026-09-26` is
 * both a date and a hyphenated eight-digit run): among matches starting at the same place the LONGER wins, and
 * when two are the same length the LINK claims first, then the DATE, then the PHONE. A match that overlaps one
 * already accepted is DROPPED. A caller therefore gets results in document order and never two that overlap.
 *
 * `NSDataDetectorErrorDomain` AND ITS TWO CODES ARE OURS (§11.6.1 D2): Apple documents that this door reports
 * an error and publishes no domain, no code and no message for it.
 */

#ifndef FOUNDATION_NSDATADETECTOR_H
#define FOUNDATION_NSDATADETECTOR_H

#import <Foundation/NSError.h>		/* NSErrorDomain, the type of the domain below */
#import <Foundation/NSRegularExpression.h>
#import <Foundation/NSTextCheckingResult.h>

NS_ASSUME_NONNULL_BEGIN

/* THE DOMAIN A REFUSED TYPE IS REPORTED IN. Ours (D2): Apple publishes none for this door. */
extern NSErrorDomain const NSDataDetectorErrorDomain;

/* THE TWO REASONS A TYPE IS REFUSED, both ours for the reason just given. They are kept apart because they are
 * different facts: one is "no detector makes that kind of result" (Apple's contract), the other is "the
 * detector exists in Apple's Foundation and its DATA does not exist here" (a registered deviation). */
enum {
	NSDataDetectorTypeNotADataDetectorCode = 1,
	NSDataDetectorTypeNeedsSubstrateCode = 2
};

@interface NSDataDetector : NSRegularExpression
{
	NSTextCheckingTypes _checkingTypes;
}

+ (nullable NSDataDetector *)dataDetectorWithTypes:(NSTextCheckingTypes)checkingTypes
					     error:(NSError ** _Nullable)error;
- (nullable instancetype)initWithTypes:(NSTextCheckingTypes)checkingTypes
				 error:(NSError ** _Nullable)error;

/* THE TYPES THIS DETECTOR LOOKS FOR, exactly as it was built with. */
@property (readonly) NSTextCheckingTypes checkingTypes;

- (BOOL)isEqual:(nullable id)other;
- (NSUInteger)hash;
- (NSString *)description;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDATADETECTOR_H */
