/*
 * CGDataProvider_internal.h — how the library reads a provider's bytes.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NOT INSTALLED, NOT INCLUDED BY ANY PUBLIC HEADER, and named the way libconfig_internal.h is.
 * Apple hands the bytes back as a CFDataRef (`CGDataProviderCopyData`) and this tree has no
 * CoreFoundation, so the library's own consumers ask here instead: BORROWED BYTES, valid for as
 * long as the provider lives, with the length written back through `size`.
 */
#ifndef CORE_GRAPHICS_CGDATAPROVIDER_INTERNAL_H
#define CORE_GRAPHICS_CGDATAPROVIDER_INTERNAL_H

#include <CoreGraphics/CGDataProvider.h>

const void *cg_dataprovider_bytes(CGDataProviderRef provider, size_t *size);

#endif /* CORE_GRAPHICS_CGDATAPROVIDER_INTERNAL_H */
