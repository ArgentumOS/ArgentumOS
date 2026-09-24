/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDirectoryEnumerator.m — the deep walk (W8 slice 1). MANUAL OWNERSHIP: every allocation below is
 * released in -dealloc, and the two doors that build a walk are NSFileManager's, not this class's.
 *
 * THE WALK IS A STACK, NOT A RECURSION, because the caller drives it one item at a time: three
 * parallel arrays hold one frame per OPEN directory - its absolute path, its relative prefix, and the
 * names still to visit at that level. The name list is kept REVERSED so the next name is
 * `-lastObject` and taking it is `-removeLastObject`, which is O(1) and, more to the point, makes
 * "where the cursor is in a level" a fact of the storage rather than an index to keep in step.
 *
 * TWO QUESTIONS ARE ANSWERED BY CALLING THE SHIPPED CLASS RATHER THAN BY REPEATING IT: the NAMES in a
 * directory come from -contentsOfDirectoryAtPath:error:, and an item's ATTRIBUTES from
 * -attributesOfItemAtPath:error:. Each costs one extra syscall per item, and each is worth it: the
 * enumerator's `-fileAttributes` is then the SAME DICTIONARY a caller gets by asking the manager
 * directly - one definition of "an item's attributes" in the tree instead of two that can drift - and
 * both awkward cases get one rule each: a directory that cannot be listed pushes an EMPTY frame (a
 * level with nothing in it), and an item that cannot be lstat(2)ed is skipped.
 */

#import <Foundation/NSDirectoryEnumerator.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSString.h>
#include <sys/stat.h>
#include <unistd.h>

/* THE BLOCK RUNTIME, declared here rather than included, which is how NSFileHandle does it too. */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

/* A JOINED PATH with ONE rule, so the absolute side and the relative side cannot disagree: an empty
 * prefix means "the name itself", which is exactly what keeps the root level's answers relative. */
static NSString *fn_join(NSString *prefix, NSString *name)
{
	if ([prefix length] == 0) {
		return name;
	}
	return [NSString stringWithFormat:@"%@/%@", prefix, name];
}

@implementation NSDirectoryEnumerator

- (id)initWithPath:(NSString *)path options:(NSUInteger)options
{
	return [self initWithPath:path options:options prefetchKeys:nil yieldsURLs:NO errorHandler:NULL];
}

- (id)initWithPath:(NSString *)path
	   options:(NSUInteger)options
      prefetchKeys:(NSArray *)keys
	yieldsURLs:(BOOL)yieldsURLs
      errorHandler:(BOOL (^)(NSURL *url, NSError *error))handler
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (path == nil) {
		[self release];
		return nil;
	}
	_root = [path copy];
	_dirStack = [[NSMutableArray alloc] init];
	_relStack = [[NSMutableArray alloc] init];
	_namesStack = [[NSMutableArray alloc] init];
	_options = options;
	_postStack = [[NSMutableArray alloc] init];
	_prefetchKeys = [keys retain];
	_yieldsURLs = yieldsURLs;
	if (handler != NULL) {
		_errorHandler = (BOOL (^)(NSURL *, NSError *))_Block_copy(handler);
	}
	_directoryAttributes = [[[NSFileManager defaultManager] attributesOfItemAtPath:_root
									       error:NULL] retain];
	[self fnOpenLevel:_root relative:@""];
	return self;
}

- (void)dealloc
{
	[_root release];
	[_dirStack release];
	[_relStack release];
	[_namesStack release];
	[_directoryAttributes release];
	[_prefetchKeys release];
	[_postStack release];
	if (_errorHandler != NULL) {
		_Block_release(_errorHandler);
	}
	[_currentPath release];
	[super dealloc];
}

/* PUSH ONE LEVEL: the names in `absolute`, the relative prefix they are answered under, and the
 * absolute path a name is joined to. A directory that cannot be listed pushes an EMPTY frame rather
 * than failing: the level closes on its own on the next pass, so an unreadable directory is a level
 * with nothing in it - which is what "an enumerator that enumerates no files" means. */
- (void)fnOpenLevel:(NSString *)absolute relative:(NSString *)relative
{
	NSError *failed = nil;
	NSArray *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:absolute
									     error:&failed];
	NSMutableArray *reversed = [NSMutableArray array];

	/* A DIRECTORY THAT CANNOT BE OPENED IS WHAT THE HANDLER IS FOR, and its answer is the difference
	 * between continuing and stopping (Apple: "return true if you want the enumeration to continue or
	 * false if you want the enumeration to stop"). Only the ROOT's failure is silent, because the door
	 * above promises an enumerator that enumerates NOTHING rather than a callback for a URL that was
	 * never a directory to begin with. */
	if (names == nil && failed != nil && ![absolute isEqual:_root] && _errorHandler != NULL) {
		NSURL *where = [[NSURL alloc] initFileURLWithPath:absolute];
		BOOL keepGoing = _errorHandler(where, failed);

		[where release];
		if (!keepGoing) {
			[_dirStack removeAllObjects];
			[_relStack removeAllObjects];
			[_namesStack removeAllObjects];
			return;
		}
	}

	if (names != nil) {
		NSUInteger i;

		for (i = [names count]; i > 0; i--) {
			NSString *name = [names objectAtIndex:i - 1];

			/* THE OPTIONS ARE HONOURED HERE: a hidden name is one whose OWN first character is a
			 * dot - this system has no hidden bit, so that is the rule, and it is the same one the
			 * resource-value door answers (NSURLIsHiddenKey) and the listing door filters by. */
			if ((_options & NSDirectoryEnumerationSkipsHiddenFiles) != 0 &&
			    [name hasPrefix:@"."]) {
				continue;
			}
			[reversed addObject:name];
		}
	}
	[_dirStack addObject:absolute];
	[_relStack addObject:relative];
	[_namesStack addObject:reversed];
	[_postStack addObject:[NSNull null]];
}

- (nullable id)nextObject
{
	while ([_dirStack count] > 0) {
		NSMutableArray *names = [_namesStack lastObject];
		NSString *absoluteDir = [_dirStack lastObject];
		NSString *relativeDir = [_relStack lastObject];
		NSString *name;
		NSString *absolute;
		NSString *relative;
		struct stat st;

		/* THE LEVEL IS SPENT: close it, and let the loop look one level up. That closure is what
		 * makes the walk depth-first without a recursive call. */
		if ([names count] == 0) {
			/* THE LEVEL IS CLOSED, AND IN POST-ORDER MODE THAT IS WHERE ITS DIRECTORY IS ANSWERED:
			 * the slot filled when the level was opened holds the item, and the answer happens once,
			 * on the way OUT - which is what "returns directories after their contents" means. */
			id deferred = [_postStack lastObject];

			[_postStack removeLastObject];
			[_dirStack removeLastObject];
			[_relStack removeLastObject];
			[_namesStack removeLastObject];
			if (deferred != nil && deferred != [NSNull null]) {
				/* THE SLOT CARRIES BOTH SPELLINGS: {absolute, relative}. The URL answer is built
				 * from the ABSOLUTE path (a URL made from a relative path is not a file URL at all,
				 * and answering nil there would END THE WALK - which is exactly what the first
				 * version did, measured as "items=5" with the walk stopping at the first level
				 * close), and the path answer is the relative one every other answer uses. */
				NSString *asAbsolute = [deferred objectAtIndex:0];
				NSString *asRelative = [deferred objectAtIndex:1];

				if (_yieldsURLs) {
					NSURL *item = [[NSURL alloc] initFileURLWithPath:asAbsolute];

					if (item != nil) {
						return [item autorelease];
					}
				}
				return asRelative;
			}
			continue;
		}
		name = [names lastObject];
		absolute = fn_join(absoluteDir, name);
		relative = fn_join(relativeDir, name);
		if (lstat([absolute UTF8String], &st) != 0) {
			/* REMOVED BETWEEN THE LISTING AND HERE: it is not an item any more, so there is
			 * nothing to answer about it - and the rest of the level is still there. */
			[names removeLastObject];
			continue;
		}
		[names removeLastObject];
		[_currentPath release];
		_currentPath = [absolute retain];
		_currentLevel = [_dirStack count];	/* the ROOT is level 0, so its children are 1 */
		_currentIsDirectory = S_ISDIR(st.st_mode) ? YES : NO;
		_returned = YES;
		_pushedForCurrent = NO;
		if (_currentIsDirectory) {
			/* A REAL DIRECTORY, which is what the lstat above established: a SYMLINK to one is
			 * answered and not entered, per the class's own rule - AND AN ENCOUNTERED MOUNT POINT IS
			 * NOT ENTERED EITHER, which is this door's own sentence ("does not resolve symbolic links
			 * or mount points encountered in the enumeration process, nor does it recurse through them
			 * if they point to a directory"): a walk must not cross into another file system by
			 * accident. The test is the DEVICE NUMBER of the child against the directory holding it,
			 * and it applies to CHILDREN only - a mount point GIVEN AS THE PATH is traversed, because
			 * the walk's first opendir(2) is not a comparison. */
			struct stat parentSt;
			BOOL sameDevice = lstat([absoluteDir UTF8String], &parentSt) != 0 ||
					 parentSt.st_dev == st.st_dev;

			if (sameDevice) {
				[self fnOpenLevel:absolute relative:relative];
				_pushedForCurrent = YES;
				if ((_options & NSDirectoryEnumerationIncludesDirectoriesPostOrder) != 0) {
					/* REMEMBERED IN THE SLOT fnOpenLevel JUST PUSHED - the one belonging to THIS
					 * directory's own level - because that is the level whose close answers it. Both
					 * halves of that were learned the hard way, and each is written down where it was
					 * measured: putting the item in the PARENT's slot meant two sibling directories
					 * overwrote each other (a tree of eight answered seven), and not stopping here
					 * meant every descended directory was also answered on the way in (ten for
					 * eight). */
					[_postStack removeLastObject];
					[_postStack addObject:[NSArray arrayWithObjects:absolute, relative, nil]];
					continue;	/* its answer is the level's close, not this turn */
				}
			}
		}
		if (_yieldsURLs) {
			/* THE URL ANSWER: built once per item and PRIMED with the values the caller asked for, so
			 * the object answers from the enumeration's moment rather than from the disk at question
			 * time. */
			NSURL *item = [[NSURL alloc] initFileURLWithPath:absolute];

			if (item == nil) {
				return nil;
			}
			if (_prefetchKeys != nil) {
				NSUInteger k;

				for (k = 0; k < [_prefetchKeys count]; k++) {
					NSURLResourceKey key = [_prefetchKeys objectAtIndex:k];
					id value = nil;

					[item getResourceValue:&value forKey:key error:NULL];
					[item fnPrefetchValue:value forKey:key];
				}
			}
			return [item autorelease];
		}
		return relative;
	}
	return nil;
}

- (nullable NSDictionary *)fileAttributes
{
	if (!_returned || _currentPath == nil) {
		return nil;
	}
	return [[NSFileManager defaultManager] attributesOfItemAtPath:_currentPath error:NULL];
}

- (nullable NSDictionary *)directoryAttributes
{
	return _directoryAttributes;
}

- (NSUInteger)level
{
	return _currentLevel;
}

- (void)skipDescendents
{
	/* "CAUSES THE RECEIVER TO SKIP RECURSION INTO THE MOST RECENTLY OBTAINED SUBDIRECTORY" - so it
	 * UNDOES the level that item opened. When the most recent item was a FILE (or a symlink, or
	 * nothing has been returned yet) there is no such level and this is a no-op, which is the same
	 * sentence read literally. */
	if (!_pushedForCurrent) {
		return;
	}
	[_dirStack removeLastObject];
	[_relStack removeLastObject];
	[_namesStack removeLastObject];
	_pushedForCurrent = NO;
}

- (void)skipDescendants
{
	[self skipDescendents];
}

- (BOOL)isEnumeratingDirectoryPostOrder
{
	return (_options & NSDirectoryEnumerationIncludesDirectoriesPostOrder) != 0 ? YES : NO;
}

@end
