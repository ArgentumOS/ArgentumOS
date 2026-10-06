/*
 * CGGeometryFoundation — the geometric primitives as dictionaries.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * SIX DOORS AND ONE SHAPE: three `Create` forms that turn a struct into a dictionary and three `Make` forms
 * that turn it back, all six built on two helpers so that the keys and the ownership rule live in ONE place
 * each. A DOOR THAT WROTE ITS OWN KEY NAMES WOULD BE A DOOR THAT COULD DISAGREE WITH THE OTHER FIVE.
 *
 * `CGFloat` IS `double` HERE (the tree is 64-bit only), which is why the numbers go in and come out as
 * doubles: a float in the middle would round a value the caller never rounded.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGGeometry.h>

/* THE KEY NAMES, IN ONE PLACE, and the reason they are these names is in CGGeometry.h: Apple publishes the
 * representation and not its keys, and every other implementation of these doors uses this spelling. */
#define CG_KEY_X @"X"
#define CG_KEY_Y @"Y"
#define CG_KEY_W @"Width"
#define CG_KEY_H @"Height"

/* THE `Create` SIDE RETURNS A REFERENCE THE CALLER OWNS: `alloc`/`init`, not `autorelease`, because Apple's
 * documentation says to release the CFDictionary its own version returns and this is the same door. */
static NSDictionary *fn_dict2(NSString *k0, CGFloat v0, NSString *k1, CGFloat v1)
{
	return [[NSDictionary alloc] initWithObjectsAndKeys:
		[NSNumber numberWithDouble:(double)v0], k0,
		[NSNumber numberWithDouble:(double)v1], k1,
		nil];
}

static NSDictionary *fn_dict4(NSString *k0, CGFloat v0, NSString *k1, CGFloat v1, NSString *k2, CGFloat v2,
			      NSString *k3, CGFloat v3)
{
	return [[NSDictionary alloc] initWithObjectsAndKeys:
		[NSNumber numberWithDouble:(double)v0], k0,
		[NSNumber numberWithDouble:(double)v1], k1,
		[NSNumber numberWithDouble:(double)v2], k2,
		[NSNumber numberWithDouble:(double)v3], k3,
		nil];
}

/* A NUMBER, OR NOTHING: a dictionary whose value for a key is a string, an array or absent is NOT a
 * representation of a geometric primitive, and the boolean these doors return is how that is said. */
static int fn_number(NSDictionary *dict, NSString *key, CGFloat *out)
{
	id v = [dict objectForKey:key];

	if (v == nil || ![v isKindOfClass:[NSNumber class]]) {
		return 0;
	}
	*out = (CGFloat)[v doubleValue];
	return 1;
}

NSDictionary *CGPointCreateDictionaryRepresentation(CGPoint point)
{
	return fn_dict2(CG_KEY_X, point.x, CG_KEY_Y, point.y);
}

bool CGPointMakeWithDictionaryRepresentation(NSDictionary *dict, CGPoint *point)
{
	CGFloat x, y;

	if (dict == nil || point == NULL) {
		return false;
	}
	if (!fn_number(dict, CG_KEY_X, &x) || !fn_number(dict, CG_KEY_Y, &y)) {
		return false;
	}
	point->x = x;
	point->y = y;
	return true;
}

NSDictionary *CGSizeCreateDictionaryRepresentation(CGSize size)
{
	return fn_dict2(CG_KEY_W, size.width, CG_KEY_H, size.height);
}

bool CGSizeMakeWithDictionaryRepresentation(NSDictionary *dict, CGSize *size)
{
	CGFloat w, h;

	if (dict == nil || size == NULL) {
		return false;
	}
	if (!fn_number(dict, CG_KEY_W, &w) || !fn_number(dict, CG_KEY_H, &h)) {
		return false;
	}
	size->width = w;
	size->height = h;
	return true;
}

NSDictionary *CGRectCreateDictionaryRepresentation(CGRect rect)
{
	return fn_dict4(CG_KEY_X, rect.origin.x, CG_KEY_Y, rect.origin.y, CG_KEY_W, rect.size.width,
			CG_KEY_H, rect.size.height);
}

bool CGRectMakeWithDictionaryRepresentation(NSDictionary *dict, CGRect *rect)
{
	CGFloat x, y, w, h;

	if (dict == nil || rect == NULL) {
		return false;
	}
	if (!fn_number(dict, CG_KEY_X, &x) || !fn_number(dict, CG_KEY_Y, &y)
	    || !fn_number(dict, CG_KEY_W, &w) || !fn_number(dict, CG_KEY_H, &h)) {
		return false;
	}
	rect->origin.x = x;
	rect->origin.y = y;
	rect->size.width = w;
	rect->size.height = h;
	return true;
}
