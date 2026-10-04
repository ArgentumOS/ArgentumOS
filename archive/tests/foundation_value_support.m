/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_value, unit 1 of 2 — the support unit (MRR).
 *
 * It imports ONLY <Foundation/Foundation.h>, which is also what proves the
 * umbrella header is complete: if a type were missing from it, this unit would
 * not compile.
 */

#import "foundation_value.h"

NSNumber *foundation_value_number(void)
{
	return [NSNumber numberWithInt:42];
}

NSData *foundation_value_data(void)
{
	static const unsigned char bytes[] = { 1, 2, 3, 4 };

	return [NSData dataWithBytes:bytes length:sizeof bytes];
}

NSDate *foundation_value_date(void)
{
	return [NSDate dateWithTimeIntervalSince1970:1000.0];
}
