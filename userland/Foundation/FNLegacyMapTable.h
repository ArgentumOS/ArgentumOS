/*
 * FNLegacyMapTable.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE LEGACY CALL-BACK TABLE, IN ONE PLACE (§62.45). NSMapTable's pre-10.5 C API needs a table that carries the
 * caller's call-backs and hands itself to every one of them; NSHashTable's legacy API needs THE SAME SCAN with no
 * values. So the scan lives here and the hash table's legacy mode is a thin wrapper over it, rather than a second
 * implementation of "is this key here" that could disagree with the first.
 *
 * IT IS NOT PUBLIC API AND IS NOT IN Foundation.h.
 */

#ifndef FOUNDATION_FNLEGACYMAPTABLE_H
#define FOUNDATION_FNLEGACYMAPTABLE_H

#import <Foundation/NSMapTable.h>
#import <Foundation/NSObject.h>

NS_ASSUME_NONNULL_BEGIN

@interface FNLegacyMapTable : NSMapTable
{
	NSMapTableKeyCallBacks _keyCallBacks;
	NSMapTableValueCallBacks _valueCallBacks;
	void **_legacyKeys;
	void **_legacyValues;
	NSUInteger _legacyCount;
	NSUInteger _legacyCapacity;
}

- (instancetype)fnInitWithKeyCallBacks:(NSMapTableKeyCallBacks)keyCallBacks
			valueCallBacks:(NSMapTableValueCallBacks)valueCallBacks
			     capacity:(NSUInteger)capacity;
- (NSUInteger)fnIndexForKey:(const void *)key;
- (void *)fnKeyAtIndex:(NSUInteger)index;
- (void *)fnValueAtIndex:(NSUInteger)index;
- (void)fnAppendKey:(const void *)key value:(const void *)value;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNLEGACYMAPTABLE_H */
