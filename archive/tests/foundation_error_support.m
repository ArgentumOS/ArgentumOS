/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_error, unit 1 of 2 — the support unit (MRR).
 *
 * It imports ONLY <Foundation/Foundation.h>, so the umbrella's completeness is
 * part of the check, and it hands back values built in another unit.
 */

#import "foundation_error.h"

NSError *foundation_error_built(void)
{
	return [NSError errorWithDomain:@"FNXSupportDomain"
				   code:7
			       userInfo:[NSDictionary dictionaryWithObject:@"from the support unit"
								    forKey:NSLocalizedFailureReasonKey]];
}

NSException *foundation_error_exception(void)
{
	return [NSException exceptionWithName:NSGenericException
				       reason:@"raised across units"
				     userInfo:nil];
}

