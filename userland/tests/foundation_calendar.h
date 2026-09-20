/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_calendar — the two units' shared surface.
 *
 * The support unit imports ONLY <Foundation/Foundation.h>, so this probe is also
 * what proves the three new F7 headers are IN the umbrella: without them, the
 * support unit would not compile.
 */

#ifndef FOUNDATION_CALENDAR_H
#define FOUNDATION_CALENDAR_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

NSDate *foundation_calendar_date(void);
/* nullable: it hands back the constructor's result, and that constructor answers
 * nil for an identifier this library does not implement. */
NSCalendar * _Nullable foundation_calendar_calendar(void);
/* nullable for the same reason: +timeZoneForSecondsFromGMT: goes through an init
 * with a [super init] check. */
NSTimeZone * _Nullable foundation_calendar_zone(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_CALENDAR_H */
