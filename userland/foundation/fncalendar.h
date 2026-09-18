/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * fncalendar.h — the PRIVATE bridge two classes share: the ICU calendar keyword for one of the
 * NS* identifier constants. docs/design/foundation-plan.md §10, F13.7e.
 *
 * It exists because TWO classes need the same answer and the answer is ICU's: NSCalendar opens a
 * calendar with it, and NSDateFormatter puts it in its locale so that `-calendar:` can name any
 * calendar the library supports. One mapping, so the two cannot disagree about what "hebrew" is.
 *
 * THE MAPPING IS MOSTLY THE IDENTITY, which is worth knowing before reading it: the identifier
 * constants ARE ICU's keywords for thirteen of the sixteen, and only three are spelled differently
 * (`ethioaa`, `islamic-tbla`, `roc`). What the function really buys is VALIDATION — the keyword for
 * an identifier this library does not know is NULL, which is how an unknown name becomes nil.
 *
 * NOTE, recorded rather than hidden: nscalendar.m still carries its own static copy of this
 * mapping. Collapsing the two is a small change that wants nscalendar.m open anyway; until then
 * this file is the one the formatter uses, and the duplication is deliberate and visible.
 */

#ifndef FOUNDATION_FNCALENDAR_H
#define FOUNDATION_FNCALENDAR_H

#import <foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

/* The ICU keyword, or NULL for an identifier this library does not know. A C string rather than an
 * object because every caller feeds it straight into a locale string. */
const char * _Nullable fn_calendar_keyword(NSString *identifier);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNCALENDAR_H */
