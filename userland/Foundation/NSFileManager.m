/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFileManager.m — the file system as a service (F13.14). MANUAL OWNERSHIP.
 *
 * TWO OPERATIONS ARE SPELLED OUT HERE because POSIX does not have them, and they are the reason this
 * file is longer than a set of one-line wrappers:
 *
 *   -removeItemAtPath:  removes a DIRECTORY AND EVERYTHING UNDER IT, which rmdir(2) refuses to do —
 *                       and which Cocoa's own -removeItemAtPath: does, so a caller that expected
 *                       the service and got rmdir(2) would be surprised in the worst way;
 *   -copyItemAtPath:    copies a tree, recursing, and there is no syscall for that at all.
 *
 * EVERY OWNER OF AN ALLOCATION IS FREED, and every DIR is closed on every path out of the walk: a
 * service that leaks a file descriptor per call is a service a long-running program cannot use.
 */

#import <Foundation/NSFileManager.h>
#import <Foundation/NSDirectoryEnumerator.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>		/* remove(3) and rename(2) live here, not in unistd.h */
#include <string.h>
#include <stdlib.h>

NSString *const NSFileType = @"NSFileType";
NSString *const NSFileSize = @"NSFileSize";
NSString *const NSFileModificationDate = @"NSFileModificationDate";
NSString *const NSFilePosixPermissions = @"NSFilePosixPermissions";
NSString *const NSFileOwnerAccountID = @"NSFileOwnerAccountID";
NSString *const NSFileGroupOwnerAccountID = @"NSFileGroupOwnerAccountID";

NSString *const NSFileTypeRegular = @"NSFileTypeRegular";
NSString *const NSFileTypeDirectory = @"NSFileTypeDirectory";
NSString *const NSFileTypeSymbolicLink = @"NSFileTypeSymbolicLink";
NSString *const NSFileTypeUnknown = @"NSFileTypeUnknown";

static NSFileManager *fn_shared_file_manager = nil;
static unsigned long fn_temporary_counter = 0;

/* AN ERROR, NOT AN ERRNO: the code is the errno and the description is that errno's own text, so a
 * caller who prints the error prints something true. */
static NSError *fn_error_from_errno(int err)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:@{ NSLocalizedDescriptionKey :
					   [NSString stringWithUTF8String:strerror(err)] }];
}

static BOOL fn_failed(NSError ** _Nullable error, int err)
{
	if (error != NULL) {
		*error = fn_error_from_errno(err);
	}
	return NO;
}

static NSString *fn_type_of(mode_t mode)
{
	if (S_ISDIR(mode)) {
		return NSFileTypeDirectory;
	}
	if (S_ISLNK(mode)) {
		return NSFileTypeSymbolicLink;
	}
	if (S_ISREG(mode)) {
		return NSFileTypeRegular;
	}
	return NSFileTypeUnknown;
}

/* THE NAMES IN A DIRECTORY, skipping the two that are not names. On a failure the directory is still
 * closed, because the only way out of the loop is through here. */
static NSArray *fn_directory_names(const char *path, int *outErrno)
{
	DIR *dir = opendir(path);
	NSMutableArray *names = [NSMutableArray array];
	struct dirent *entry;

	if (dir == NULL) {
		*outErrno = errno;
		return nil;
	}
	while ((entry = readdir(dir)) != NULL) {
		NSString *name;

		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		name = [NSString stringWithUTF8String:entry->d_name];
		if (name != nil) {
			[names addObject:name];
		}
	}
	closedir(dir);
	*outErrno = 0;
	return names;
}

/* A JOINED PATH, so the walk does not depend on a buffer's size to be correct. */
static NSString *fn_joined(NSString *directory, NSString *name)
{
	if ([directory hasSuffix:@"/"]) {
		return [NSString stringWithFormat:@"%@%@", directory, name];
	}
	return [NSString stringWithFormat:@"%@/%@", directory, name];
}

@implementation NSFileManager

+ (NSFileManager *)defaultManager
{
	if (fn_shared_file_manager == nil) {
		fn_shared_file_manager = [[NSFileManager alloc] init];
	}
	return fn_shared_file_manager;
}

- (nullable id <NSFileManagerDelegate>)delegate
{
	return _delegate;
}

- (void)setDelegate:(nullable id <NSFileManagerDelegate>)delegate
{
	/* ASSIGN: no retain and no release. Apple's property comes back as `unowned(unsafe)`, and a
	 * delegate that owned its manager (or the reverse) would be a cycle this class has no reason to
	 * create. */
	_delegate = delegate;
}

/* ---- THE TWO QUESTIONS (W8 slice 2, foundation-plan.md §60) ---------------------------------------
 *
 * WHICH OPERATION IS BEING ASKED ABOUT is this file-private enum's whole job: the veto question and
 * the error question differ ONLY in which pair of delegate selectors they reach for, so each is ONE
 * switch instead of four near-identical methods that can drift apart.
 */
typedef enum {
	FNOperationCopy = 0,
	FNOperationMove,
	FNOperationRemove,
	FNOperationLink
} FNOperationKind;

/* THE PREFERENCE RULE, IN ONE LINE THAT CAN BE POINTED AT: "the file manager always prefers methods
 * that take an NSURL object over those that take an NSString object" - so a family asks its URL
 * selector FIRST and its path selector only when the URL one is absent. */
- (BOOL)fnDelegatePrefersURL:(SEL)urlSelector
{
	return _delegate != nil && [_delegate respondsToSelector:urlSelector];
}

/* "SHOULD IT BEGIN AT ALL" - and YES when there is nobody to ask, which is Apple's own answer for an
 * unimplemented method: "if the delegate does not implement the appropriate methods, the file manager
 * copies the given file or directory". */
- (BOOL)fnDelegateShould:(FNOperationKind)kind from:(NSString *)source to:(nullable NSString *)destination
{
	if (_delegate == nil) {
		return YES;
	}
	if (kind == FNOperationRemove) {
		if ([self fnDelegatePrefersURL:@selector(fileManager:shouldRemoveItemAtURL:)]) {
			return [_delegate fileManager:self shouldRemoveItemAtURL:[NSURL fileURLWithPath:source]];
		}
		if ([_delegate respondsToSelector:@selector(fileManager:shouldRemoveItemAtPath:)]) {
			return [_delegate fileManager:self shouldRemoveItemAtPath:source];
		}
		return YES;
	}
	/* THE OTHER THREE ALL HAVE A DESTINATION, so from here it is not optional and the compiler can
	 * see that. */
	if (destination == nil) {
		return YES;
	}
	{
		NSString *to = destination;

		switch (kind) {
		case FNOperationCopy:
			if ([self fnDelegatePrefersURL:@selector(fileManager:shouldCopyItemAtURL:toURL:)]) {
				return [_delegate fileManager:self
			      shouldCopyItemAtURL:[NSURL fileURLWithPath:source]
					      toURL:[NSURL fileURLWithPath:to]];
			}
			if ([_delegate respondsToSelector:@selector(fileManager:shouldCopyItemAtPath:toPath:)]) {
				return [_delegate fileManager:self shouldCopyItemAtPath:source toPath:to];
			}
			break;
		case FNOperationMove:
			if ([self fnDelegatePrefersURL:@selector(fileManager:shouldMoveItemAtURL:toURL:)]) {
				return [_delegate fileManager:self
			      shouldMoveItemAtURL:[NSURL fileURLWithPath:source]
					      toURL:[NSURL fileURLWithPath:to]];
			}
			if ([_delegate respondsToSelector:@selector(fileManager:shouldMoveItemAtPath:toPath:)]) {
				return [_delegate fileManager:self shouldMoveItemAtPath:source toPath:to];
			}
			break;
		case FNOperationLink:
			if ([self fnDelegatePrefersURL:@selector(fileManager:shouldLinkItemAtURL:toURL:)]) {
				return [_delegate fileManager:self
			      shouldLinkItemAtURL:[NSURL fileURLWithPath:source]
					      toURL:[NSURL fileURLWithPath:to]];
			}
			if ([_delegate respondsToSelector:@selector(fileManager:shouldLinkItemAtPath:toPath:)]) {
				return [_delegate fileManager:self shouldLinkItemAtPath:source toPath:to];
			}
			break;
		case FNOperationRemove:
			break;		/* answered above, before a destination could matter */
		}
	}
	return YES;
}

/* "SHOULD IT PROCEED AFTER AN ERROR" - and NO when there is nobody to ask, which is the half of
 * Apple's wording that matters most: "if you do not implement this method, the file manager assumes a
 * response of YES" describes a consultation that only HAPPENS when there is someone to consult (the
 * operation pages say the manager "may also call" it), so a manager whose delegate implements nothing
 * must fail exactly as it did before this slice. */
- (BOOL)fnDelegateProceedAfterError:(int)err
			      kind:(FNOperationKind)kind
			      from:(NSString *)source
				to:(nullable NSString *)destination
{
	NSError *error;

	if (_delegate == nil) {
		return NO;
	}
	error = fn_error_from_errno(err);
	if (kind == FNOperationRemove) {
		if ([self fnDelegatePrefersURL:@selector(fileManager:shouldProceedAfterError:removingItemAtURL:)]) {
			return [_delegate fileManager:self shouldProceedAfterError:error
				     removingItemAtURL:[NSURL fileURLWithPath:source]];
		}
		if ([_delegate respondsToSelector:@selector(fileManager:shouldProceedAfterError:removingItemAtPath:)]) {
			return [_delegate fileManager:self shouldProceedAfterError:error
				    removingItemAtPath:source];
		}
		return NO;
	}
	if (destination == nil) {
		return NO;
	}
	{
		NSString *to = destination;

		switch (kind) {
		case FNOperationCopy:
			if ([self fnDelegatePrefersURL:@selector(fileManager:shouldProceedAfterError:copyingItemAtURL:toURL:)]) {
				return [_delegate fileManager:self shouldProceedAfterError:error
					      copyingItemAtURL:[NSURL fileURLWithPath:source]
							toURL:[NSURL fileURLWithPath:to]];
			}
			if ([_delegate respondsToSelector:@selector(fileManager:shouldProceedAfterError:copyingItemAtPath:toPath:)]) {
				return [_delegate fileManager:self shouldProceedAfterError:error
					     copyingItemAtPath:source
							toPath:to];
			}
			break;
		case FNOperationMove:
			if ([self fnDelegatePrefersURL:@selector(fileManager:shouldProceedAfterError:movingItemAtURL:toURL:)]) {
				return [_delegate fileManager:self shouldProceedAfterError:error
					       movingItemAtURL:[NSURL fileURLWithPath:source]
							toURL:[NSURL fileURLWithPath:to]];
			}
			if ([_delegate respondsToSelector:@selector(fileManager:shouldProceedAfterError:movingItemAtPath:toPath:)]) {
				return [_delegate fileManager:self shouldProceedAfterError:error
					      movingItemAtPath:source
							toPath:to];
			}
			break;
		case FNOperationLink:
			if ([self fnDelegatePrefersURL:@selector(fileManager:shouldProceedAfterError:linkingItemAtURL:toURL:)]) {
				return [_delegate fileManager:self shouldProceedAfterError:error
					      linkingItemAtURL:[NSURL fileURLWithPath:source]
							toURL:[NSURL fileURLWithPath:to]];
			}
			if ([_delegate respondsToSelector:@selector(fileManager:shouldProceedAfterError:linkingItemAtPath:toPath:)]) {
				return [_delegate fileManager:self shouldProceedAfterError:error
					     linkingItemAtPath:source
							toPath:to];
			}
			break;
		case FNOperationRemove:
			break;
		}
	}
	return NO;
}

/* THE ERROR PATH OF ONE ITEM, IN ONE PLACE, AND IT ANSWERS WHAT THE CALLER MUST RETURN: YES when the
 * delegate swallowed the error (this arm is NOT a failure and the walk carries on), NO with *outErrno
 * set when it stands. `err == 0` cannot happen on a real failure path, and EIO is the honest stand-in
 * if it ever does - the alternatives are a zero errno reaching NSError or a success code in an error
 * arm. */
- (BOOL)fnFailed:(int)err
	    kind:(FNOperationKind)kind
	    from:(NSString *)source
	      to:(nullable NSString *)destination
	   error:(int *)outErrno
{
	if (err == 0) {
		err = EIO;
	}
	if ([self fnDelegateProceedAfterError:err kind:kind from:source to:destination]) {
		return YES;
	}
	*outErrno = err;
	return NO;
}

/* THE RECURSIVE REMOVE, WITH BOTH QUESTIONS IN IT. Apple asks "prior to removing EACH item", and a NO
 * on a directory "prevents both the directory and its children from being deleted" - which is exactly
 * what never entering the recursion below gives, without having to walk the children to find out. */
- (BOOL)fnRemoveItem:(NSString *)item error:(int *)outErrno
{
	const char *path = [item UTF8String];
	struct stat st;

	if (![self fnDelegateShould:FNOperationRemove from:item to:nil]) {
		/* A VETO IS A SKIP AND NOT A FAILURE, and the ground is this class's OWN contract (header,
		 * item 1): every failure here answers NO AND fills in an NSError whose code is an errno - and
		 * a refusal HAS no errno. A veto that answered NO would therefore have to invent an error
		 * code, which is the one thing this file refuses to do. Apple names none either: its
		 * sentences say what is NOT done, never that the operation failed. */
		return YES;
	}
	if (lstat(path, &st) != 0) {
		return [self fnFailed:errno kind:FNOperationRemove from:item to:nil error:outErrno];
	}
	if (S_ISDIR(st.st_mode)) {
		int err = 0;
		NSArray *names = fn_directory_names(path, &err);
		NSUInteger i;

		if (names == nil) {
			return [self fnFailed:err kind:FNOperationRemove from:item to:nil error:outErrno];
		}
		for (i = 0; i < [names count]; i++) {
			NSString *child = fn_joined(item, [names objectAtIndex:i]);

			if (![self fnRemoveItem:child error:outErrno]) {
				return NO;
			}
		}
	}
	/*
	 * THE DOOR DEPENDS ON WHAT IT IS: rmdir(2) for a directory, unlink(2) for everything else — and
	 * NOT remove(3), which is what this used to call (F13.23, and it cost a whole investigation to
	 * find). musl's remove(3) is `unlink(path)` and, only when that fails with EISDIR, a retry as
	 * `unlinkat(AT_FDCWD, path, AT_REMOVEDIR)`. THIS KERNEL'S unlink(2) ANSWERS -EPERM FOR A
	 * DIRECTORY, NOT -EISDIR — deliberately, and its own comment says so ("Linux returns -EISDIR;
	 * sys_rmdir is the dir path") — so the retry NEVER HAPPENS and remove(3) can never remove a
	 * directory here. rmdir(2) itself is fine, and always was: called directly it removes a
	 * directory and the empty/non-empty distinction works. The file's own `S_ISDIR` above is what
	 * makes the right choice possible; the bug was not using it at the end.
	 */
	if (S_ISDIR(st.st_mode) ? (rmdir(path) != 0) : (unlink(path) != 0)) {
		return [self fnFailed:errno kind:FNOperationRemove from:item to:nil error:outErrno];
	}
	return YES;
}

/* THE RECURSIVE COPY, WITH THE SAME TWO QUESTIONS - and the FIRST one is asked "once for each item
 * that needs to be copied ... once for the directory and once for each item in the directory", which
 * is why the recursion is where the vetting happens. */
- (BOOL)fnCopyItem:(NSString *)item to:(NSString *)target error:(int *)outErrno
{
	const char *from = [item UTF8String];
	const char *to = [target UTF8String];
	struct stat st;

	if (![self fnDelegateShould:FNOperationCopy from:item to:target]) {
		return YES;		/* a veto skips the item; a directory's children are never reached */
	}
	if (lstat(from, &st) != 0) {
		return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
	}
	if (S_ISDIR(st.st_mode)) {
		int err = 0;
		NSArray *names;
		NSUInteger i;

		if (mkdir(to, st.st_mode & 07777) != 0 && errno != EEXIST) {
			return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
		}
		names = fn_directory_names(from, &err);
		if (names == nil) {
			return [self fnFailed:err kind:FNOperationCopy from:item to:target error:outErrno];
		}
		for (i = 0; i < [names count]; i++) {
			NSString *name = [names objectAtIndex:i];
			NSString *source = fn_joined(item, name);
			NSString *destination = fn_joined(target, name);

			if (![self fnCopyItem:source to:destination error:outErrno]) {
				return NO;
			}
		}
		return YES;
	}
	if (S_ISREG(st.st_mode)) {
		int in = open(from, O_RDONLY);
		int out;
		char buffer[8192];
		ssize_t n;

		if (in < 0) {
			return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
		}
		out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
		if (out < 0) {
			int err = errno;

			close(in);
			return [self fnFailed:err kind:FNOperationCopy from:item to:target error:outErrno];
		}
		while ((n = read(in, buffer, sizeof(buffer))) > 0) {
			ssize_t written = 0;

			while (written < n) {
				ssize_t step = write(out, buffer + written, (size_t)(n - written));

				if (step <= 0) {
					int err = errno != 0 ? errno : EIO;

					close(in);
					close(out);
					return [self fnFailed:err kind:FNOperationCopy from:item to:target
							error:outErrno];
				}
				written += step;
			}
		}
		close(in);
		close(out);
		if (n < 0) {
			return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
		}
		return YES;
	}
	/* A SYMLINK OR SOMETHING ELSE: named rather than guessed at - and that naming is now the ERROR
	 * DOOR's business too, so a delegate may swallow it. (Apple copies the link itself; this refuses
	 * it, and that departure is a NAMED row of §60's slice 3 rather than a silence here.) */
	return [self fnFailed:ENOTSUP kind:FNOperationCopy from:item to:target error:outErrno];
}

/* A MOVE IS ONE rename(2), AND THE DELEGATE IS ASKED ONCE, FOR THE ITEM ITSELF. Apple states the
 * asymmetry as the difference from the copy above: "if the item being moved is a directory, the file
 * manager notifies the delegate only for the directory itself and not for any of its contents". */
- (BOOL)fnMoveItem:(NSString *)item to:(NSString *)target error:(int *)outErrno
{
	if (![self fnDelegateShould:FNOperationMove from:item to:target]) {
		return YES;
	}
	/* ONE SYSCALL, AND IT IS ONLY ONE BECAUSE BOTH PATHS ARE ON ONE FILESYSTEM: an EXDEV comes back
	 * to the caller as itself rather than being silently turned into a copy. */
	if (rename([item UTF8String], [target UTF8String]) != 0) {
		return [self fnFailed:errno kind:FNOperationMove from:item to:target error:outErrno];
	}
	return YES;
}

/* A HARD LINK, so the delegate's LINKING family has a caller at all. */
- (BOOL)fnLinkItem:(NSString *)item to:(NSString *)target error:(int *)outErrno
{
	if (![self fnDelegateShould:FNOperationLink from:item to:target]) {
		return YES;
	}
	if (link([item UTF8String], [target UTF8String]) != 0) {
		return [self fnFailed:errno kind:FNOperationLink from:item to:target error:outErrno];
	}
	return YES;
}

- (BOOL)fileExistsAtPath:(NSString *)path
{
	return [self fileExistsAtPath:path isDirectory:NULL];
}

- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(nullable BOOL *)isDirectory
{
	struct stat st;

	if (path == nil || stat([path UTF8String], &st) != 0) {
		return NO;
	}
	if (isDirectory != NULL) {
		*isDirectory = S_ISDIR(st.st_mode) ? YES : NO;
	}
	return YES;
}

- (BOOL)isReadableFileAtPath:(NSString *)path
{
	return path != nil && access([path UTF8String], R_OK) == 0;
}

- (BOOL)isWritableFileAtPath:(NSString *)path
{
	return path != nil && access([path UTF8String], W_OK) == 0;
}

- (BOOL)isExecutableFileAtPath:(NSString *)path
{
	return path != nil && access([path UTF8String], X_OK) == 0;
}

- (BOOL)isDeletableFileAtPath:(NSString *)path
{
	/* DELETABILITY IS A QUESTION ABOUT THE DIRECTORY, not the file: what has to be writable and
	 * searchable is the place the name lives. */
	NSString *parent;

	if (path == nil) {
		return NO;
	}
	parent = [path stringByDeletingLastPathComponent];
	if ([parent lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
		parent = @"/";
	}
	return access([parent UTF8String], W_OK | X_OK) == 0;
}

- (nullable NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError ** _Nullable)error
{
	int err = 0;
	NSArray *names;

	if (path == nil) {
		fn_failed(error, EINVAL);
		return nil;
	}
	names = fn_directory_names([path UTF8String], &err);
	if (names == nil) {
		fn_failed(error, err);
		return nil;
	}
	return names;
}

/* ---- THE DEEP WALK (W8 slice 1, foundation-plan.md §60) -----------------------------------------
 *
 * THE ENUMERATOR'S CONTRACT IS IN ITS OWN HEADER; what belongs HERE is why these three doors are
 * three lines each while the walk they answer with is a class. Apple's own sentence for the file case
 * - "an enumerator object that enumerates no files; the first call to -nextObject will return nil" -
 * is what makes an error channel unnecessary for -enumeratorAtPath:. A file, a name that is not
 * there, and a directory that cannot be listed all arrive at the SAME enumerator, because the level-0
 * listing inside decides it: nil from that listing is an empty frame, and an empty frame is a spent
 * cursor. */
- (nullable NSDirectoryEnumerator *)enumeratorAtPath:(NSString *)path
{
	if (path == nil) {
		return nil;
	}
	return [[[NSDirectoryEnumerator alloc] initWithPath:path options:0] autorelease];
}

/* THE WALK THE TWO -subpaths DOORS SHARE, because they ARE one contract with and without an error
 * channel - Apple's own 10.5 note ("use -subpathsOfDirectoryAtPath:error: instead") replaces the older
 * spelling and not its behaviour. The root is listed here, once, so a path that is not a directory is
 * refused in ONE place and both doors refuse it identically: nil, with the error where there is
 * somewhere to put it. (An EMPTY directory is not that case - it lists fine and answers an empty
 * array, which is the difference between "no items" and "not a directory".) */
- (nullable NSArray *)fn_subpathsOfDirectoryAtPath:(NSString *)path error:(NSError ** _Nullable)error
{
	NSDirectoryEnumerator *walk;
	NSMutableArray *subpaths;
	NSString *item;

	if (path == nil) {
		fn_failed(error, EINVAL);
		return nil;
	}
	{
		int err = 0;

		if (fn_directory_names([path UTF8String], &err) == nil) {
			fn_failed(error, err);
			return nil;
		}
	}
	walk = [[[NSDirectoryEnumerator alloc] initWithPath:path options:0] autorelease];
	subpaths = [NSMutableArray array];
	while ((item = [walk nextObject]) != nil) {
		[subpaths addObject:item];
	}
	return subpaths;
}

- (nullable NSArray *)subpathsAtPath:(NSString *)path
{
	return [self fn_subpathsOfDirectoryAtPath:path error:NULL];
}

- (nullable NSArray *)subpathsOfDirectoryAtPath:(NSString *)path error:(NSError ** _Nullable)error
{
	return [self fn_subpathsOfDirectoryAtPath:path error:error];
}

- (BOOL)createDirectoryAtPath:(NSString *)path
  withIntermediateDirectories:(BOOL)createIntermediates
		   attributes:(nullable NSDictionary *)attributes
			error:(NSError ** _Nullable)error
{
	if (path == nil) {
		return fn_failed(error, EINVAL);
	}
	if (createIntermediates) {
		/* EVERY PREFIX IN TURN, so "a/b/c" works from nothing. An EEXIST on a component is not a
		 * failure — it is the common case. */
		NSMutableString *prefix = [NSMutableString string];
		NSArray *parts = [path componentsSeparatedByString:@"/"];
		NSUInteger i;

		for (i = 0; i < [parts count]; i++) {
			NSString *part = [parts objectAtIndex:i];

			if ([part lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 0) {
				/* THE LEADING SLASH of an absolute path, or a doubled one. */
				if (i == 0) {
					[prefix appendString:@"/"];
				}
				continue;
			}
			if ([prefix lengthOfBytesUsingEncoding:NSUTF8StringEncoding] > 1 || ([prefix lengthOfBytesUsingEncoding:NSUTF8StringEncoding] == 1 && [prefix isEqualToString:@"/"])) {
				[prefix appendString:@"/"];
			}
			[prefix appendString:part];
			if (mkdir([prefix UTF8String], 0777) != 0 && errno != EEXIST) {
				return fn_failed(error, errno);
			}
		}
	} else if (mkdir([path UTF8String], 0777) != 0 && errno != EEXIST) {
		return fn_failed(error, errno);
	}
	if (attributes != nil) {
		NSNumber *mode = [attributes objectForKey:NSFilePosixPermissions];

		if (mode != nil && chmod([path UTF8String], (mode_t)[mode unsignedShortValue]) != 0) {
			return fn_failed(error, errno);
		}
	}
	return YES;
}

- (BOOL)createFileAtPath:(NSString *)path
		contents:(nullable NSData *)contents
	      attributes:(nullable NSDictionary *)attributes
{
	int fd;

	if (path == nil) {
		return NO;
	}
	fd = open([path UTF8String], O_WRONLY | O_CREAT | O_TRUNC, 0666);
	if (fd < 0) {
		return NO;
	}
	if (contents != nil && [contents length] > 0) {
		const void *bytes = [contents bytes];
		NSUInteger total = [contents length];
		NSUInteger written = 0;

		while (written < total) {
			ssize_t step = write(fd, (const char *)bytes + written, total - written);

			if (step <= 0) {
				close(fd);
				return NO;
			}
			written += (NSUInteger)step;
		}
	}
	close(fd);
	if (attributes != nil) {
		NSNumber *mode = [attributes objectForKey:NSFilePosixPermissions];

		if (mode != nil) {
			chmod([path UTF8String], (mode_t)[mode unsignedShortValue]);
		}
	}
	return YES;
}

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError ** _Nullable)error
{
	int err = 0;

	if (path == nil) {
		return fn_failed(error, EINVAL);
	}
	if (![self fnRemoveItem:path error:&err]) {
		return fn_failed(error, err);
	}
	return YES;
}

- (BOOL)moveItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error
{
	int err = 0;

	if (sourcePath == nil || destinationPath == nil) {
		return fn_failed(error, EINVAL);
	}
	if (![self fnMoveItem:sourcePath to:destinationPath error:&err]) {
		return fn_failed(error, err);
	}
	return YES;
}

- (BOOL)copyItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error
{
	int err = 0;

	if (sourcePath == nil || destinationPath == nil) {
		return fn_failed(error, EINVAL);
	}
	if (![self fnCopyItem:sourcePath to:destinationPath error:&err]) {
		return fn_failed(error, err);
	}
	return YES;
}

- (BOOL)linkItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error
{
	int err = 0;

	if (sourcePath == nil || destinationPath == nil) {
		return fn_failed(error, EINVAL);
	}
	if (![self fnLinkItem:sourcePath to:destinationPath error:&err]) {
		return fn_failed(error, err);
	}
	return YES;
}

- (nullable NSDictionary *)attributesOfItemAtPath:(NSString *)path
					    error:(NSError ** _Nullable)error
{
	struct stat st;
	NSMutableDictionary *attributes;

	if (path == nil) {
		fn_failed(error, EINVAL);
		return nil;
	}
	/* lstat, NOT stat: the attributes of a LINK are the link's, which is what a caller asking about
	 * that path means. */
	if (lstat([path UTF8String], &st) != 0) {
		fn_failed(error, errno);
		return nil;
	}
	attributes = [NSMutableDictionary dictionary];
	[attributes setObject:fn_type_of(st.st_mode) forKey:NSFileType];
	[attributes setObject:[NSNumber numberWithLongLong:(long long)st.st_size] forKey:NSFileSize];
	[attributes setObject:[NSDate dateWithTimeIntervalSince1970:(double)st.st_mtime]
		       forKey:NSFileModificationDate];
	[attributes setObject:[NSNumber numberWithUnsignedShort:(unsigned short)(st.st_mode & 07777)]
		       forKey:NSFilePosixPermissions];
	[attributes setObject:[NSNumber numberWithUnsignedInt:(unsigned int)st.st_uid]
		       forKey:NSFileOwnerAccountID];
	[attributes setObject:[NSNumber numberWithUnsignedInt:(unsigned int)st.st_gid]
		       forKey:NSFileGroupOwnerAccountID];
	return attributes;
}

- (nullable NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path
						 error:(NSError ** _Nullable)error
{
	char buffer[1024];
	ssize_t n;

	if (path == nil) {
		fn_failed(error, EINVAL);
		return nil;
	}
	n = readlink([path UTF8String], buffer, sizeof(buffer) - 1);
	if (n < 0) {
		fn_failed(error, errno);
		return nil;
	}
	buffer[n] = '\0';
	return [NSString stringWithUTF8String:buffer];
}

- (nullable NSString *)currentDirectoryPath
{
	char buffer[1024];

	if (getcwd(buffer, sizeof(buffer)) == NULL) {
		return nil;
	}
	return [NSString stringWithUTF8String:buffer];
}

- (BOOL)changeCurrentDirectoryPath:(NSString *)path
{
	return path != nil && chdir([path UTF8String]) == 0;
}

@end

/* THE FSH'S OWN ANSWER, WITH APPLE'S TRAILING SEPARATOR. It is a CONSTANT rather than a lookup because the
 * path is fixed by the filesystem hierarchy rather than discovered: the directory is part of the FSH
 * skeleton, and init recreates it at mount if it is missing, so answering the path here cannot race the
 * thing that makes it real. */
NSString *NSTemporaryDirectory(void)
{
	return @"/System/Temporary Files/";
}
