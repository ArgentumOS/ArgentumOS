/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDirectoryEnumerator — the deep walk (W8 slice 1; docs/design/foundation-plan.md §60).
 * MANUAL OWNERSHIP.
 *
 * THREE OF ITS RULES ARE MEASURED FROM APPLE'S PUBLISHED PAGES RATHER THAN REMEMBERED, because each
 * one is observable and each one is easy to get wrong:
 *
 *   THE PATHNAMES ARE RELATIVE TO THE ENUMERATED DIRECTORY. Apple's own Objective-C example appends
 *   what `-nextObject` answered to the directory it passed the factory, and the class page says it in
 *   words: "These pathnames are relative to the directory." A walk of `/a/b` therefore answers `c`
 *   and `c/d`, NOT `/a/b/c`.
 *
 *   `-level` COUNTS THE ENUMERATED DIRECTORY AS ZERO, so the items this class returns start at 1:
 *   "the number of levels, with the directory passed to -enumeratorAtURL:...errorHandler: considered
 *   to be level 0".
 *
 *   THE WALK DOES NOT RESOLVE SYMBOLIC LINKS AND DOES NOT RECURSE THROUGH THEM when they point to a
 *   directory (Apple's own sentence for -enumeratorAtPath:). That is why every item is lstat(2)ed and
 *   only a REAL directory opens a level underneath it - while a symlink given AS the path is still
 *   traversed, because the first opendir(2) follows it. (Apple says -subpathsAtPath: traverses a link
 *   given as the path, which is the same admission from the other door.)
 */

#ifndef FOUNDATION_NSDIRECTORYENUMERATOR_H
#define FOUNDATION_NSDIRECTORYENUMERATOR_H

#import <Foundation/NSEnumerator.h>

@class NSDictionary;
@class NSMutableArray;
@class NSString;

/* NULLABILITY (F6): NONNULL by default, and the two exceptions are the attribute dictionaries, which
 * answer nil before the first item (and, for -directoryAttributes, whenever the starting directory
 * cannot be stated at all). */
NS_ASSUME_NONNULL_BEGIN

@interface NSDirectoryEnumerator : NSEnumerator
{
	NSString *_root;			/* the path the door was given: the WALK's prefix */
	NSMutableArray *_dirStack;		/* NSString: one ABSOLUTE directory per open level */
	NSMutableArray *_relStack;		/* NSString: that level's RELATIVE prefix, parallel to it */
	NSMutableArray *_namesStack;		/* NSMutableArray of names per level, REVERSED so we pop the end */
	NSDictionary *_directoryAttributes;	/* the STARTING directory's, built once */
	NSString *_currentPath;			/* ABSOLUTE, of the item most recently returned */
	NSUInteger _currentLevel;
	NSUInteger _options;
	BOOL _currentIsDirectory;
	BOOL _returned;
	BOOL _pushedForCurrent;			/* did returning it open a level underneath? */
}

/* THE CURRENT ITEM AND THE STARTING DIRECTORY, and the two are NOT the same question: Apple abstracts
 * -fileAttributes as "the attributes of the most recently returned file or subdirectory (as
 * referenced by the pathname)" and -directoryAttributes as "the attributes of the directory at which
 * enumeration started". */
- (nullable NSDictionary *)fileAttributes;
- (nullable NSDictionary *)directoryAttributes;
- (NSUInteger)level;

/* "Causes the receiver to skip recursion into the most recently obtained subdirectory" - and the two
 * spellings are ONE method: Apple's page for the modern one says "this method is identical to
 * -skipDescendents: except for the spelling", and NEITHER carries a deprecation flag, so both ship. */
- (void)skipDescendents;
- (void)skipDescendants;

/* Whether this enumerator was made in post-order mode. This slice's walk is PRE-ORDER and its doors
 * pass no options, so it answers NO today; §60's slice 6 is what will pass the bit in, and the walk
 * gains its post-order arm then. */
- (BOOL)isEnumeratingDirectoryPostOrder;

/* OURS, NOT COCOA'S, and the same asymmetry NSEnumerator's -initWithSequence:reverse: has: the class
 * that constructs an enumerator is the one that knows its own storage, which here is NSFileManager.
 * `options` is a PARAMETER rather than a property because the path doors this slice lands have none
 * to give; it is carried so the post-order bit has one home when slice 6 arrives. */
- (id)initWithPath:(NSString *)path options:(NSUInteger)options;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSDIRECTORYENUMERATOR_H */
