/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFileWrapper.m — the tree, read and written (W8 slice 4). MANUAL OWNERSHIP: every allocation is
 * released in -dealloc, and the parent pointer is deliberately NOT retained (a child does not own its
 * parent; the parent owns the child).
 *
 * THE THREE KINDS ARE ONE ENUM AND NOT THREE CLASSES, which is Apple's shape and also the simpler one
 * here: a wrapper's kind decides which of its fields mean anything, and every door that does not apply
 * to a kind either answers nil (the directories' dictionary) or RAISES (the child doors on a
 * non-directory), which is exactly what Apple documents.
 *
 * A LAZY REGULAR FILE KEEPS ITS SOURCE PATH, NOT ITS BYTES: that is what makes
 * NSFileWrapperReadingImmediate mean something, and the option is defined where it is described rather
 * than here. Attributes are read EAGERLY either way, because a stat(2) is not what the option is about.
 */

#import <Foundation/NSFileWrapper.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSURL.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	FNWrapperRegular = 0,
	FNWrapperDirectory = 1,
	FNWrapperSymbolicLink = 2
};

@interface NSFileWrapper (FNPrivate)
- (void)fnAdopt:(nullable NSFileWrapper *)parent;
- (BOOL)fnWriteToPath:(NSString *)path
	      options:(NSFileWrapperWritingOptions)options
   originalContents:(nullable NSString *)originalContents
		error:(NSError ** _Nullable)error;
- (nullable NSDictionary *)fnSerializedTree;
- (nullable instancetype)initWithSerializedTree:(id)tree;
@end

/* AN ERROR, THE WAY THE REST OF THIS LIBRARY MAKES ONE: the code is the errno and the description is
 * that errno's own text. */
static NSError *fn_wrapper_error(int err)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:@{ NSLocalizedDescriptionKey :
					   [NSString stringWithUTF8String:strerror(err)] }];
}

static BOOL fn_wrapper_failed(NSError ** _Nullable error, int err)
{
	if (error != NULL) {
		*error = fn_wrapper_error(err);
	}
	return NO;
}

/* THE BYTES OF A FILE, read with read(2) - and this class never maps anything, which is what makes
 * NSFileWrapperReadingWithoutMapping true by construction rather than by a branch. The buffer is on the
 * HEAP because this system's user stack is twelve kilobytes (§60 slice 3). */
static NSData *fn_wrapper_data(NSString *path)
{
	NSMutableData *data;
	struct stat st;
	char *buffer;
	ssize_t n;
	int fd;

	if (path == nil || stat([path UTF8String], &st) != 0 || !S_ISREG(st.st_mode)) {
		return nil;
	}
	fd = open([path UTF8String], O_RDONLY);
	if (fd < 0) {
		return nil;
	}
	buffer = malloc(8192);
	if (buffer == NULL) {
		close(fd);
		return nil;
	}
	data = [NSMutableData dataWithCapacity:(NSUInteger)(st.st_size > 0 ? st.st_size : 64)];
	while ((n = read(fd, buffer, 8192)) > 0) {
		[data appendBytes:buffer length:(NSUInteger)n];
	}
	close(fd);
	free(buffer);
	return n < 0 ? nil : data;
}

static BOOL fn_wrapper_write_data(NSData *bytes, NSString *path)
{
	const void *raw = [bytes bytes];
	size_t left = [bytes length];
	int fd = open([path UTF8String], O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd < 0) {
		return NO;
	}
	while (left > 0) {
		ssize_t step = write(fd, (const char *)raw, left);

		if (step <= 0) {
			close(fd);
			return NO;
		}
		raw = (const char *)raw + step;
		left -= (size_t)step;
	}
	close(fd);
	return YES;
}

/* A LINK'S TARGET, heap-buffered for the same measured reason. */
static NSString *fn_wrapper_link_target(NSString *path)
{
	char *buffer = malloc(4096);
	NSString *target;
	ssize_t n;

	if (path == nil || buffer == NULL) {
		free(buffer);
		return nil;
	}
	n = readlink([path UTF8String], buffer, 4095);
	if (n < 0) {
		free(buffer);
		return nil;
	}
	buffer[n] = '\0';
	target = [NSString stringWithUTF8String:buffer];
	free(buffer);
	return target;
}

/* A KEY THAT IS NOT TAKEN YET: Apple says the key is "a UNIQUE filename... the same as the passed-in
 * file wrapper's preferred filename UNLESS that name is already in use as a key in the directory's
 * dictionary of children", and does not say what the substituted name looks like - so the substitution
 * is OURS and it is a rule: `<name> 2`, `<name> 3`, ... until one is free. */
static NSString *fn_wrapper_free_key(NSDictionary *taken, NSString *preferred)
{
	NSUInteger counter;

	if ([taken objectForKey:preferred] == nil) {
		return preferred;
	}
	for (counter = 2; counter < 1000000; counter++) {
		NSString *candidate = [NSString stringWithFormat:@"%@ %lu", preferred, (unsigned long)counter];

		if ([taken objectForKey:candidate] == nil) {
			return candidate;
		}
	}
	return preferred;
}

@implementation NSFileWrapper

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_kind = FNWrapperRegular;
	}
	return self;
}

- (void)dealloc
{
	[_fileWrappers release];
	[_contents release];
	[_linkDestination release];
	[_attributes release];
	[_filename release];
	[_preferredFilename release];
	[_sourcePath release];
	[super dealloc];
}

/* ---- WHAT IT IS --------------------------------------------------------------------------------- */

- (BOOL)isRegularFile
{
	return _kind == FNWrapperRegular ? YES : NO;
}

- (BOOL)isDirectory
{
	return _kind == FNWrapperDirectory ? YES : NO;
}

- (BOOL)isSymbolicLink
{
	return _kind == FNWrapperSymbolicLink ? YES : NO;
}

/* ---- CREATING ----------------------------------------------------------------------------------- */

- (instancetype)initRegularFileWithContents:(NSData *)contents
{
	self = [self init];
	if (self != nil) {
		_kind = FNWrapperRegular;
		_contents = [contents copy];
	}
	return self;
}

- (instancetype)initDirectoryWithFileWrappers:(nullable NSDictionary *)childrenByPreferredName
{
	self = [self init];
	if (self != nil) {
		_kind = FNWrapperDirectory;
		_fileWrappers = [[NSMutableDictionary alloc] init];
		if (childrenByPreferredName != nil) {
			NSArray *keys = [childrenByPreferredName allKeys];
			NSUInteger i;

			for (i = 0; i < [keys count]; i++) {
				NSString *name = [keys objectAtIndex:i];
				NSFileWrapper *child = [childrenByPreferredName objectForKey:name];

				/* THE KEY IS THE CHILD'S PREFERRED NAME, which is what Apple's own argument label says
				 * the dictionary is keyed by - and the name it lands under is then settled by the same
				 * uniqueness rule every other door uses. */
				if ([child isKindOfClass:[NSFileWrapper class]]) {
					if ([child preferredFilename] == nil) {
						[child setPreferredFilename:name];
					}
					[self addFileWrapper:child];
				}
			}
		}
	}
	return self;
}

- (instancetype)initSymbolicLinkWithDestination:(NSString *)path
{
	self = [self init];
	if (self != nil) {
		_kind = FNWrapperSymbolicLink;
		_linkDestination = [path copy];
	}
	return self;
}

- (instancetype)initSymbolicLinkWithDestinationURL:(NSURL *)url
{
	NSString *path = [url isKindOfClass:[NSURL class]] ? [(NSURL *)url path] : nil;

	[self release];
	if (path == nil) {
		return nil;
	}
	return [[NSFileWrapper alloc] initSymbolicLinkWithDestination:path];
}

- (nullable instancetype)initWithURL:(NSURL *)url
			     options:(NSFileWrapperReadingOptions)options
			       error:(NSError ** _Nullable)error
{
	NSString *path = [url isKindOfClass:[NSURL class]] ? [(NSURL *)url path] : nil;

	self = [self init];
	if (self == nil) {
		return nil;
	}
	_readOptions = options;
	if (path == nil || ![self readFromURL:url options:options error:error]) {
		[self release];
		if (path == nil) {
			fn_wrapper_failed(error, EINVAL);
		}
		return nil;
	}
	return self;
}

- (nullable instancetype)initWithPath:(NSString *)path
{
	if (path == nil) {
		return nil;
	}
	return [self initWithURL:[NSURL fileURLWithPath:path] options:0 error:NULL];
}

/* ---- READING ------------------------------------------------------------------------------------ */

/* THE ONE READER, AND EVERY DOOR THAT READS GOES THROUGH IT: -initWithURL:options:error:,
 * -readFromURL:options:error: and -updateFromPath: are three ways of asking one question, so the tree
 * is built in one place. `options` carries the immediate/lazy choice into the recursion, so a
 * directory's descendants are built the same way its root was. */
- (BOOL)readFromURL:(NSURL *)url
	    options:(NSFileWrapperReadingOptions)options
	      error:(NSError ** _Nullable)error
{
	NSString *path = [url isKindOfClass:[NSURL class]] ? [(NSURL *)url path] : nil;
	NSFileManager *manager = [NSFileManager defaultManager];
	struct stat st;

	if (path == nil) {
		return fn_wrapper_failed(error, EINVAL);
	}
	/* lstat(2), because a LINK IS A LINK HERE and not what it points at - the same rule the rest of
	 * this unit follows, and the reason a dangling link can be wrapped at all. */
	if (lstat([path UTF8String], &st) != 0) {
		return fn_wrapper_failed(error, errno);
	}
	if (S_ISLNK(st.st_mode)) {
		_kind = FNWrapperSymbolicLink;
		[_linkDestination release];
		_linkDestination = [fn_wrapper_link_target(path) copy];
	} else if (S_ISDIR(st.st_mode)) {
		NSArray *names;

		_kind = FNWrapperDirectory;
		[_fileWrappers release];
		_fileWrappers = [[NSMutableDictionary alloc] init];
		names = [manager contentsOfDirectoryAtPath:path error:NULL];
		if (names != nil) {
			NSUInteger i;

			for (i = 0; i < [names count]; i++) {
				NSString *name = [names objectAtIndex:i];
				NSString *childPath = [path hasSuffix:@"/"] ?
					[NSString stringWithFormat:@"%@%@", path, name] :
					[NSString stringWithFormat:@"%@/%@", path, name];
				NSFileWrapper *child = [[NSFileWrapper alloc]
					initWithURL:[NSURL fileURLWithPath:childPath]
					    options:options
					      error:NULL];

				if (child != nil) {
					[self addFileWrapper:child];
					[child release];
				}
			}
		}
	} else {
		_kind = FNWrapperRegular;
		if ((options & NSFileWrapperReadingImmediate) != 0) {
			/* THE EAGER FORM: the bytes are taken NOW, which is what "read files immediately after
			 * creating a file wrapper" means - and what makes the wrapper a snapshot of them. */
			[_contents release];
			_contents = [fn_wrapper_data(path) retain];
			[_sourcePath release];
			_sourcePath = nil;
		} else {
			/* THE LAZY FORM, WHICH IS WHAT THE OPTION'S ABSENCE MEANS: only the place the bytes live. */
			[_sourcePath release];
			_sourcePath = [path retain];
		}
	}
	/* THE NAMES AND THE ATTRIBUTES ARE TAKEN EITHER WAY: a stat(2) is not what the reading option is
	 * about, and Apple's rule that preferred filenames "are not preserved when you write a file wrapper
	 * to disk and then later instantiate another file wrapper by reading the file" is this - reading
	 * names the wrapper after the node it read. */
	[_filename release];
	_filename = [[path lastPathComponent] copy];
	[_preferredFilename release];
	_preferredFilename = [_filename copy];
	[_attributes release];
	_attributes = [[manager attributesOfItemAtPath:path error:NULL] retain];
	return YES;
}

- (BOOL)needsToBeUpdatedFromPath:(NSString *)path
{
	struct stat st;
	id recorded;

	if (path == nil || lstat([path UTF8String], &st) != 0) {
		return YES;
	}
	/* WHAT THE WRAPPER REMEMBERS ABOUT THE NODE IS ITS ATTRIBUTES DICTIONARY, so the comparison is
	 * against the fields a change would move: the modification date, the size, and the kind. A wrapper
	 * that was never read has nothing to compare and therefore needs updating. */
	recorded = [_attributes objectForKey:NSFileModificationDate];
	if (recorded == nil) {
		return YES;
	}
	{
		double mine = [recorded timeIntervalSince1970];
		double theirs = (double)st.st_mtime;
		double gap = mine < theirs ? theirs - mine : mine - theirs;

		if (gap > 0.5) {
			return YES;
		}
	}
	if ([[_attributes objectForKey:NSFileSize] unsignedLongLongValue] !=
	    (unsigned long long)st.st_size) {
		return YES;
	}
	{
		NSString *type = [_attributes objectForKey:NSFileType];
		BOOL wasDir = [type isEqualToString:NSFileTypeDirectory];
		BOOL wasLink = [type isEqualToString:NSFileTypeSymbolicLink];

		if (wasDir != (S_ISDIR(st.st_mode) ? YES : NO) ||
		    wasLink != (S_ISLNK(st.st_mode) ? YES : NO)) {
			return YES;
		}
	}
	/* AND A DIRECTORY IS ALSO STALE WHEN ITS LIST OF CHILDREN MOVED: a name the directory has and the
	 * wrapper does not, or the other way round, which a count plus a membership test settles. */
	if (S_ISDIR(st.st_mode) && _fileWrappers != nil) {
		NSArray *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:path error:NULL];
		NSUInteger i;

		if (names == nil || [names count] != [_fileWrappers count]) {
			return YES;
		}
		for (i = 0; i < [names count]; i++) {
			if ([_fileWrappers objectForKey:[names objectAtIndex:i]] == nil) {
				return YES;
			}
		}
	}
	return NO;
}

- (BOOL)matchesContentsOfURL:(NSURL *)url
{
	NSString *path = [url isKindOfClass:[NSURL class]] ? [(NSURL *)url path] : nil;

	if (path == nil) {
		return NO;
	}
	return [self needsToBeUpdatedFromPath:path] ? NO : YES;
}

- (BOOL)updateFromPath:(NSString *)path
{
	if (path == nil) {
		return NO;
	}
	return [self readFromURL:[NSURL fileURLWithPath:path] options:_readOptions error:NULL];
}

/* ---- ITS CHILDREN ------------------------------------------------------------------------------- */

- (nullable NSDictionary *)fileWrappers
{
	return _fileWrappers;
}

/* THE RAISE IS APPLE'S OWN SENTENCE TWICE OVER: "this method raises <an exception> if the receiver is
 * not a directory file wrapper" and "... if the child file wrapper doesn't have a preferred name". A
 * door that silently did nothing would make a caller's mistake invisible. */
- (NSString *)addFileWrapper:(NSFileWrapper *)child
{
	NSString *key;

	if (child == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"-addFileWrapper: needs a file wrapper"];
	}
	if (![self isDirectory]) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"-addFileWrapper: the receiver is not a directory file wrapper"];
	}
	if ([child preferredFilename] == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"-addFileWrapper: the child has no preferred filename"];
	}
	key = fn_wrapper_free_key(_fileWrappers, [child preferredFilename]);
	[child fnAdopt:self];
	[_fileWrappers setObject:child forKey:key];
	return key;
}

- (void)removeFileWrapper:(NSFileWrapper *)child
{
	NSString *key = [self keyForChildFileWrapper:child];

	if (key != nil) {
		[_fileWrappers removeObjectForKey:key];
		[child fnAdopt:nil];
	}
}

/* THE PARENT POINTER IS WHAT THE RE-ADD RULE NEEDS, and it is UNRETAINED: Apple's `-preferredFilename`
 * page says a change "causes the existing parent directory file wrappers to remove and re-add the child
 * to accommodate the change", and re-adding is what re-keys the child under a name that may be free now.
 * A child holding its parent would be a cycle, so this one reference is the caller's to keep alive -
 * which is the same bargain Apple's assigns make. */
- (void)fnAdopt:(nullable NSFileWrapper *)parent
{
	_parent = parent;
}

- (nullable NSString *)keyForChildFileWrapper:(NSFileWrapper *)child
{
	NSArray *keys;
	NSUInteger i;

	if (_fileWrappers == nil || child == nil) {
		return nil;
	}
	keys = [_fileWrappers allKeys];
	for (i = 0; i < [keys count]; i++) {
		NSString *key = [keys objectAtIndex:i];

		if ([_fileWrappers objectForKey:key] == child) {
			return key;
		}
	}
	return nil;
}

- (NSString *)addFileWithPath:(NSString *)path
{
	NSFileWrapper *child;
	NSString *key;

	if (![self isDirectory]) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"-addFileWithPath: the receiver is not a directory file wrapper"];
	}
	child = path != nil ? [[NSFileWrapper alloc] initWithPath:path] : nil;
	if (child == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"-addFileWithPath: there is nothing to wrap at %@", path];
	}
	key = [self addFileWrapper:child];
	[child release];
	return key;
}

- (NSString *)addRegularFileWithContents:(NSData *)contents
		       preferredFilename:(NSString *)preferredFilename
{
	NSFileWrapper *child = [[NSFileWrapper alloc] initRegularFileWithContents:contents];
	NSString *key;

	[child setPreferredFilename:preferredFilename];
	key = [self addFileWrapper:child];
	[child release];
	return key;
}

- (NSString *)addSymbolicLinkWithDestination:(NSString *)path
			   preferredFilename:(NSString *)preferredFilename
{
	NSFileWrapper *child = [[NSFileWrapper alloc] initSymbolicLinkWithDestination:path];
	NSString *key;

	[child setPreferredFilename:preferredFilename];
	key = [self addFileWrapper:child];
	[child release];
	return key;
}

/* ---- WHAT IT IS CALLED -------------------------------------------------------------------------- */

- (nullable NSString *)filename
{
	return _filename;
}

- (void)setFilename:(nullable NSString *)filename
{
	NSString *copy = [filename copy];

	[_filename release];
	_filename = copy;
}

- (nullable NSString *)preferredFilename
{
	return _preferredFilename;
}

- (void)setPreferredFilename:(nullable NSString *)preferredFilename
{
	NSFileWrapper *parent = _parent;
	NSString *copy = [preferredFilename copy];

	[_preferredFilename release];
	_preferredFilename = copy;
	/* THE RE-ADD RULE, AND ONLY WHEN THERE IS A NAME TO BE RE-ADDED UNDER: a child whose parent is nil
	 * simply carries the name until it is added, and a nil name would make the re-add raise. */
	if (parent != nil && _preferredFilename != nil) {
		[parent removeFileWrapper:self];
		[parent addFileWrapper:self];
	}
}

- (NSDictionary *)fileAttributes
{
	return _attributes != nil ? _attributes : [NSDictionary dictionary];
}

- (void)setFileAttributes:(NSDictionary *)fileAttributes
{
	NSDictionary *copy = [fileAttributes copy];

	[_attributes release];
	_attributes = copy;
}

/* ---- A LINK'S TARGET, AND A FILE'S BYTES -------------------------------------------------------- */

- (nullable NSString *)symbolicLinkDestination
{
	return _linkDestination;
}

- (nullable NSURL *)symbolicLinkDestinationURL
{
	return _linkDestination != nil ? [NSURL fileURLWithPath:_linkDestination] : nil;
}

- (nullable NSData *)regularFileContents
{
	if (_kind != FNWrapperRegular) {
		return nil;
	}
	/* THE LAZY HALF OF THE READING OPTION: a wrapper built without NSFileWrapperReadingImmediate
	 * remembers where its bytes live and takes them HERE - which is also why the answer may be nil, since
	 * the file can be gone by the time anyone asks. */
	if (_contents == nil && _sourcePath != nil) {
		_contents = [fn_wrapper_data(_sourcePath) retain];
	}
	return _contents;
}

/* ---- WRITING ------------------------------------------------------------------------------------ */

- (BOOL)fnWriteToPath:(NSString *)path
	      options:(NSFileWrapperWritingOptions)options
   originalContents:(nullable NSString *)originalContents
		error:(NSError ** _Nullable)error
{
	if (_kind == FNWrapperSymbolicLink) {
		if (_linkDestination == nil) {
			return fn_wrapper_failed(error, EINVAL);
		}
		if (symlink([_linkDestination UTF8String], [path UTF8String]) != 0) {
			return fn_wrapper_failed(error, errno);
		}
	} else if (_kind == FNWrapperDirectory) {
		NSArray *keys;
		NSUInteger i;

		if (mkdir([path UTF8String], 0755) != 0 && errno != EEXIST) {
			return fn_wrapper_failed(error, errno);
		}
		keys = [_fileWrappers allKeys];
		for (i = 0; i < [keys count]; i++) {
			NSString *key = [keys objectAtIndex:i];
			NSFileWrapper *child = [_fileWrappers objectForKey:key];
			NSString *childPath = [NSString stringWithFormat:@"%@/%@", path, key];
			NSString *childOriginal = originalContents != nil ?
				[NSString stringWithFormat:@"%@/%@", originalContents, key] : nil;

			if (![child fnWriteToPath:childPath
					  options:options
				   originalContents:childOriginal
					    error:error]) {
				return NO;
			}
			if ((options & NSFileWrapperWritingWithNameUpdating) != 0) {
				/* "DESCENDANT FILE WRAPPERS' PROPERTIES ARE SET IF THE WRITING SUCCEEDS" - each child
				 * learns the name it was written under, which is what makes -filename readable at all
				 * after a first write. */
				[child setFilename:key];
			}
		}
	} else {
		NSData *bytes = [self regularFileContents];
		NSData *original = originalContents != nil ? fn_wrapper_data(originalContents) : nil;

		if (bytes == nil) {
			return fn_wrapper_failed(error, ENOENT);
		}
		/* THE ORIGINAL IS FOR NOT REWRITING WHAT DID NOT CHANGE, which is what Apple's argument is
		 * for: a file whose bytes are IDENTICAL to the original's is HARDLINKED into place instead of
		 * copied, so the two names share an inode - which is observable, and therefore asserted. */
		if (original != nil && [original isEqualToData:bytes] &&
		    link([originalContents UTF8String], [path UTF8String]) == 0) {
			return YES;
		}
		if (!fn_wrapper_write_data(bytes, path)) {
			return fn_wrapper_failed(error, errno);
		}
	}
	if ((options & NSFileWrapperWritingWithNameUpdating) != 0) {
		[self setFilename:[path lastPathComponent]];
	}
	return YES;
}

- (BOOL)writeToURL:(NSURL *)url
	   options:(NSFileWrapperWritingOptions)options
originalContentsURL:(nullable NSURL *)originalContentsURL
	     error:(NSError ** _Nullable)error
{
	NSString *path = [url isKindOfClass:[NSURL class]] ? [(NSURL *)url path] : nil;
	NSString *original = originalContentsURL != nil ? [originalContentsURL path] : nil;
	NSString *target;

	if (path == nil) {
		return fn_wrapper_failed(error, EINVAL);
	}
	/* ATOMIC, AND FOR A TREE THAT MEANS THE OBVIOUS THING: build it under a temporary name BESIDE the
	 * destination and rename it into place, so a reader sees either the old tree or the new one and
	 * never a half-written one. A destination that is already there is REMOVED first, because rename(2)
	 * will not replace a non-empty directory - a choice, stated here rather than discovered. */
	target = (options & NSFileWrapperWritingAtomic) != 0 ?
		[NSString stringWithFormat:@"%@.fnw-tmp-%d", path, (int)getpid()] : path;
	if (![self fnWriteToPath:target options:options originalContents:original error:error]) {
		if (target != path) {
			[[NSFileManager defaultManager] removeItemAtPath:target error:NULL];
		}
		return NO;
	}
	if (target != path) {
		[[NSFileManager defaultManager] removeItemAtPath:path error:NULL];
		if (rename([target UTF8String], [path UTF8String]) != 0) {
			int err = errno;

			[[NSFileManager defaultManager] removeItemAtPath:target error:NULL];
			return fn_wrapper_failed(error, err);
		}
	}
	return YES;
}

/* ---- SERIALIZING THE WHOLE TREE (W8 slice 4b) ----------------------------------------------------
 *
 * THE FORMAT IS OURS AND THE HEADER SAYS SO IN FULL; what belongs HERE is the two halves and the one
 * thing that can fail. The recursion mirrors the class: a directory's children are a dictionary of
 * key -> nested plist, a regular file carries its bytes, and a link carries its target. THE FAILURE IS
 * APPLE'S OWN SENTENCE about the lazy form - "this property may be nil if the user modifies the contents
 * of the file system node after you call -readFromURL:options:error: or -initWithURL:options:error:, but
 * before -serializedRepresentation has read the contents of the file" - so a regular file whose bytes
 * cannot be read makes the WHOLE answer nil rather than a tree with a hole in it.
 */

static NSString *const FNWrapperTypeKey = @"Type";
static NSString *const FNWrapperPreferredNameKey = @"PreferredFileName";
static NSString *const FNWrapperFileNameKey = @"FileName";
static NSString *const FNWrapperAttributesKey = @"FileAttributes";
static NSString *const FNWrapperContentsKey = @"RegularFileContents";
static NSString *const FNWrapperLinkKey = @"SymbolicLinkDestination";
static NSString *const FNWrapperChildrenKey = @"FileWrappers";

static NSString *const FNWrapperTypeRegular = @"Regular";
static NSString *const FNWrapperTypeDirectory = @"Directory";
static NSString *const FNWrapperTypeSymbolicLink = @"SymbolicLink";

/* THE TREE AS A PROPERTY LIST - one half of the round trip, and the only place the keys above are
 * written. It answers nil when a regular file's bytes cannot be read, which is what makes the door below
 * nil in exactly the case Apple names. */
- (nullable NSDictionary *)fnSerializedTree
{
	NSMutableDictionary *tree = [NSMutableDictionary dictionary];

	if (_kind == FNWrapperDirectory) {
		NSMutableDictionary *children = [NSMutableDictionary dictionary];
		NSArray *keys = [_fileWrappers allKeys];
		NSUInteger i;

		[tree setObject:FNWrapperTypeDirectory forKey:FNWrapperTypeKey];
		for (i = 0; i < [keys count]; i++) {
			NSString *key = [keys objectAtIndex:i];
			NSFileWrapper *child = [_fileWrappers objectForKey:key];
			NSDictionary *nested = [child fnSerializedTree];

			if (nested == nil) {
				return nil;
			}
			[children setObject:nested forKey:key];
		}
		[tree setObject:children forKey:FNWrapperChildrenKey];
	} else if (_kind == FNWrapperSymbolicLink) {
		if (_linkDestination == nil) {
			return nil;
		}
		[tree setObject:FNWrapperTypeSymbolicLink forKey:FNWrapperTypeKey];
		[tree setObject:_linkDestination forKey:FNWrapperLinkKey];
	} else {
		NSData *bytes = [self regularFileContents];

		if (bytes == nil) {
			return nil;
		}
		[tree setObject:FNWrapperTypeRegular forKey:FNWrapperTypeKey];
		[tree setObject:bytes forKey:FNWrapperContentsKey];
	}
	if (_preferredFilename != nil) {
		[tree setObject:_preferredFilename forKey:FNWrapperPreferredNameKey];
	}
	if (_filename != nil) {
		[tree setObject:_filename forKey:FNWrapperFileNameKey];
	}
	if (_attributes != nil) {
		[tree setObject:_attributes forKey:FNWrapperAttributesKey];
	}
	return tree;
}

/* AND BACK, WITH THE SAME KEYS, and with a REFUSAL rather than a guess for anything that is not one of
 * ours: a plist that is not a dictionary, a dictionary with no Type, and a Type this class does not know
 * all answer nil - which is what a caller holding a damaged document needs to hear. */
- (nullable instancetype)initWithSerializedTree:(id)tree
{
	id type;

	if (![tree isKindOfClass:[NSDictionary class]]) {
		[self release];
		return nil;
	}
	self = [self init];
	if (self == nil) {
		return nil;
	}
	type = [tree objectForKey:FNWrapperTypeKey];
	_preferredFilename = [[tree objectForKey:FNWrapperPreferredNameKey] copy];
	_filename = [[tree objectForKey:FNWrapperFileNameKey] copy];
	_attributes = [[tree objectForKey:FNWrapperAttributesKey] copy];
	if ([type isEqual:FNWrapperTypeDirectory]) {
		id children = [tree objectForKey:FNWrapperChildrenKey];

		_kind = FNWrapperDirectory;
		_fileWrappers = [[NSMutableDictionary alloc] init];
		if ([children isKindOfClass:[NSDictionary class]]) {
			NSArray *keys = [children allKeys];
			NSUInteger i;

			for (i = 0; i < [keys count]; i++) {
				NSString *key = [keys objectAtIndex:i];
				NSFileWrapper *child = [[NSFileWrapper alloc]
					initWithSerializedTree:[children objectForKey:key]];

				if (child == nil) {
					[self release];
					return nil;
				}
				/* THE KEY THE CHILD WAS STORED UNDER IS THE KEY IT GOES BACK UNDER: re-deriving it from
				 * the preferred name would RENAME a child that was deliberately given another key. */
				if ([child preferredFilename] == nil) {
					[child setPreferredFilename:key];
				}
				[child fnAdopt:self];
				[_fileWrappers setObject:child forKey:key];
				[child release];
			}
		}
	} else if ([type isEqual:FNWrapperTypeSymbolicLink]) {
		id destination = [tree objectForKey:FNWrapperLinkKey];

		if (![destination isKindOfClass:[NSString class]]) {
			[self release];
			return nil;
		}
		_kind = FNWrapperSymbolicLink;
		_linkDestination = [destination copy];
	} else if ([type isEqual:FNWrapperTypeRegular]) {
		id bytes = [tree objectForKey:FNWrapperContentsKey];

		if (![bytes isKindOfClass:[NSData class]]) {
			[self release];
			return nil;
		}
		_kind = FNWrapperRegular;
		_contents = [bytes copy];
	} else {
		[self release];
		return nil;
	}
	return self;
}

- (nullable NSData *)serializedRepresentation
{
	NSDictionary *tree = [self fnSerializedTree];

	if (tree == nil) {
		return nil;
	}
	return [NSPropertyListSerialization dataWithPropertyList:tree
							  format:NSPropertyListXMLFormat_v1_0
							 options:0
							   error:NULL];
}

- (nullable instancetype)initWithSerializedRepresentation:(NSData *)data
{
	id tree;

	if (data == nil) {
		return nil;
	}
	tree = [NSPropertyListSerialization propertyListWithData:data
							 options:0
							  format:NULL
							   error:NULL];
	if (tree == nil) {
		return nil;
	}
	return [[NSFileWrapper alloc] initWithSerializedTree:tree];
}

/* ---- CODING (W8 slice 4c), WHICH IS THE SAME TREE THROUGH A DIFFERENT TRANSPORT ------------------
 *
 * APPLE LISTS THIS CLASS UNDER BOTH NSCoding AND NSSecureCoding, and this tree's policy for the secure
 * half is stated where it belongs (NSCoding.h): `+supportsSecureCoding` ANSWERS, and the unarchiver does
 * not yet ask - the coder's own work item rather than this class's business.
 *
 * THE FIELDS ARE ENCODED ONE BY ONE rather than as one nested blob, because that is what a coder is for:
 * the archive shows the state and a reader of it can see the state. The keys are dotted the way this
 * library's other coding classes spell theirs, and THE KIND GOES FIRST because it decides which of the
 * rest mean anything.
 *
 * AND THE TWO TRANSPORTS DIFFER ON ONE POINT, DELIBERATELY: a regular file whose bytes cannot be read
 * makes -serializedRepresentation answer NIL (Apple's own sentence about the lazy form), while the coder
 * WRITES THE NIL IT WAS GIVEN and reads back a wrapper with no contents. Neither is a guess - one is
 * Apple's documented nil and the other is what NSCoding means - and the probe asserts both, so the
 * difference is a recorded decision rather than an accident.
 */
+ (BOOL)supportsSecureCoding
{
	return YES;
}

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeInteger:(NSInteger)_kind forKey:@"NS.fileWrapperKind"];
	[coder encodeObject:_preferredFilename forKey:@"NS.fileWrapperPreferredFilename"];
	[coder encodeObject:_filename forKey:@"NS.fileWrapperFilename"];
	[coder encodeObject:_attributes forKey:@"NS.fileWrapperAttributes"];
	if (_kind == FNWrapperDirectory) {
		[coder encodeObject:_fileWrappers forKey:@"NS.fileWrapperChildren"];
	} else if (_kind == FNWrapperSymbolicLink) {
		[coder encodeObject:_linkDestination forKey:@"NS.fileWrapperLinkDestination"];
	} else {
		[coder encodeObject:[self regularFileContents] forKey:@"NS.fileWrapperContents"];
	}
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	NSInteger kind;

	if (coder == nil) {
		[self release];
		return nil;
	}
	self = [super init];
	if (self == nil) {
		return nil;
	}
	kind = [coder decodeIntegerForKey:@"NS.fileWrapperKind"];
	/* THE KIND IS CHECKED BEFORE IT IS TRUSTED, and that check IS the class gate the secure half is
	 * about: an archive naming a kind this class does not have is refused rather than guessed at. */
	if (kind < 0 || kind > FNWrapperSymbolicLink) {
		[self release];
		return nil;
	}
	_kind = (NSUInteger)kind;
	_preferredFilename = [[coder decodeObjectForKey:@"NS.fileWrapperPreferredFilename"] copy];
	_filename = [[coder decodeObjectForKey:@"NS.fileWrapperFilename"] copy];
	_attributes = [[coder decodeObjectForKey:@"NS.fileWrapperAttributes"] copy];
	if (_kind == FNWrapperDirectory) {
		id children = [coder decodeObjectForKey:@"NS.fileWrapperChildren"];

		if (![children isKindOfClass:[NSDictionary class]]) {
			[self release];
			return nil;
		}
		_fileWrappers = [[NSMutableDictionary alloc] init];
		{
			NSArray *keys = [children allKeys];
			NSUInteger i;

			for (i = 0; i < [keys count]; i++) {
				NSString *key = [keys objectAtIndex:i];
				NSFileWrapper *child = [children objectForKey:key];

				/* THE CHILD MUST BE ONE OF OURS, which is the same gate one level down. */
				if (![child isKindOfClass:[NSFileWrapper class]]) {
					[self release];
					return nil;
				}
				if ([child preferredFilename] == nil) {
					[child setPreferredFilename:key];
				}
				[child fnAdopt:self];
				[_fileWrappers setObject:child forKey:key];
			}
		}
	} else if (_kind == FNWrapperSymbolicLink) {
		id destination = [coder decodeObjectForKey:@"NS.fileWrapperLinkDestination"];

		if (![destination isKindOfClass:[NSString class]]) {
			[self release];
			return nil;
		}
		_linkDestination = [destination copy];
	} else {
		id bytes = [coder decodeObjectForKey:@"NS.fileWrapperContents"];

		/* NIL IS ALLOWED HERE ON PURPOSE: see the transport note above. */
		if (bytes != nil && ![bytes isKindOfClass:[NSData class]]) {
			[self release];
			return nil;
		}
		_contents = [bytes copy];
	}
	return self;
}

@end
