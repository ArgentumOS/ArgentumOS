/*
 * CGColorNames — the names `CGColorGetConstantColor` is asked for.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THESE ARE NAMES AND NOT COLOURS: in the 10.6 surface (which is the surface this library targets) they are
 * `CFStringRef` constants, spelled here as `NSString *` like every other CF type, and a caller hands one to
 * `CGColorGetConstantColor` to get the colour back. Apple's own heading says so: "Names of colors for use
 * with `CGColorGetConstantColor'".
 *
 * THE VALUE IS THIS LIBRARY'S OWN, AND APPLE PUBLISHES NONE — the constant's *contents* are not part of any
 * published contract, only the fact that these three names are recognised. So each one's value is its own
 * name, which is the property that makes it useless to guess at: the door below recognises exactly what the
 * header declares, and nothing else gets a colour.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGColor.h>

NSString *const kCGColorWhite = @"kCGColorWhite";
NSString *const kCGColorBlack = @"kCGColorBlack";
NSString *const kCGColorClear = @"kCGColorClear";
