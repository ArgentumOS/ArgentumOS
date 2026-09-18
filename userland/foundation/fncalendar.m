/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fncalendar.m — the shared identifier -> ICU calendar keyword bridge. §10, F13.7e.
 *
 * The comparison is against the CONSTANTS rather than against string literals, so a caller cannot
 * smuggle in a keyword this library has not promised to support.
 */

#import <foundation/fncalendar.h>
#import <foundation/NSCalendar.h>
#import <foundation/NSString.h>

const char *fn_calendar_keyword(NSString *identifier)
{
	if (identifier == nil) {
		return NULL;
	}
	if ([identifier isEqualToString:NSCalendarIdentifierGregorian]) return "gregorian";
	if ([identifier isEqualToString:NSCalendarIdentifierISO8601]) return "iso8601";
	if ([identifier isEqualToString:NSCalendarIdentifierBuddhist]) return "buddhist";
	if ([identifier isEqualToString:NSCalendarIdentifierChinese]) return "chinese";
	if ([identifier isEqualToString:NSCalendarIdentifierCoptic]) return "coptic";
	if ([identifier isEqualToString:NSCalendarIdentifierEthiopicAmeteMihret]) return "ethiopic";
	if ([identifier isEqualToString:NSCalendarIdentifierEthiopicAmeteAlem]) return "ethioaa";
	if ([identifier isEqualToString:NSCalendarIdentifierHebrew]) return "hebrew";
	if ([identifier isEqualToString:NSCalendarIdentifierIndian]) return "indian";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamic]) return "islamic";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicCivil]) return "islamic-civil";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicTabular]) return "islamic-tbla";
	if ([identifier isEqualToString:NSCalendarIdentifierIslamicUmmAlQura]) return "islamic-umalqura";
	if ([identifier isEqualToString:NSCalendarIdentifierJapanese]) return "japanese";
	if ([identifier isEqualToString:NSCalendarIdentifierPersian]) return "persian";
	if ([identifier isEqualToString:NSCalendarIdentifierRepublicOfChina]) return "roc";
	return NULL;
}
