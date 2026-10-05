/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_calendar, unit 1 of 2 — the support unit (MRR).
 *
 * It imports ONLY <Foundation/Foundation.h>, which is what proves the umbrella
 * is complete: if NSDateComponents, NSTimeZone or NSCalendar were missing from
 * it, this unit would not compile.
 */

#import "foundation_calendar.h"

NSDate *foundation_calendar_date(void)
{
	return [NSDate dateWithTimeIntervalSince1970:1700000000.0];
}

NSCalendar *foundation_calendar_calendar(void)
{
	return [NSCalendar calendarWithIdentifier:NSCalendarIdentifierGregorian];
}

NSTimeZone *foundation_calendar_zone(void)
{
	return [NSTimeZone timeZoneForSecondsFromGMT:19800];		/* +05:30 */
}
