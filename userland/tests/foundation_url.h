/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_url — the two units' shared surface.
 *
 * The support unit imports ONLY <foundation/Foundation.h>, so this probe is also
 * what proves NSURL reached the umbrella.
 */

#ifndef FOUNDATION_URL_H
#define FOUNDATION_URL_H

#import <foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/* nullable: it hands back a nullable constructor's result. */
NSURL * _Nullable foundation_url_http(void);
NSURL * _Nullable foundation_url_file(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_URL_H */
