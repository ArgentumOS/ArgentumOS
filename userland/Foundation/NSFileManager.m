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
#include <sys/statfs.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>		/* remove(3) and rename(2) live here, not in unistd.h */
#include <string.h>
#include <stdlib.h>
#include <pwd.h>		/* getpwuid(3)/getpwnam(3): the account NAMES (W8 slice 3d) */
#include <grp.h>		/* ... and getgrgid(3)/getgrnam(3) for the group's */
#include <math.h>		/* floor(3), for the nanoseconds of a date */

NSString *const NSFileType = @"NSFileType";
NSString *const NSFileSize = @"NSFileSize";
NSString *const NSFileModificationDate = @"NSFileModificationDate";
NSString *const NSFilePosixPermissions = @"NSFilePosixPermissions";
NSString *const NSFileOwnerAccountID = @"NSFileOwnerAccountID";
NSString *const NSFileGroupOwnerAccountID = @"NSFileGroupOwnerAccountID";
/* W8 slice 3d: the two NAME keys, which read through the account database and WRITE through it too. */
NSString *const NSFileOwnerAccountName = @"NSFileOwnerAccountName";
NSString *const NSFileGroupOwnerAccountName = @"NSFileGroupOwnerAccountName";

/* THE FLAG NAMES WITH NO SUBSTRATE HERE (W8 slice 3e) - published, never filled, never acted on: the
 * header carries the one-line ground each of them has. */
NSString *const NSFileImmutable = @"NSFileImmutable";
NSString *const NSFileAppendOnly = @"NSFileAppendOnly";
NSString *const NSFileBusy = @"NSFileBusy";
NSString *const NSFileExtensionHidden = @"NSFileExtensionHidden";
NSString *const NSFileHFSCreatorCode = @"NSFileHFSCreatorCode";
NSString *const NSFileHFSTypeCode = @"NSFileHFSTypeCode";
NSString *const NSFileProtectionKey = @"NSFileProtectionKey";
NSString *const NSFileProtectionComplete = @"NSFileProtectionComplete";
NSString *const NSFileProtectionCompleteUnlessOpen = @"NSFileProtectionCompleteUnlessOpen";
NSString *const NSFileProtectionCompleteUntilFirstUserAuthentication =
	@"NSFileProtectionCompleteUntilFirstUserAuthentication";
NSString *const NSFileProtectionNone = @"NSFileProtectionNone";

/* W8 slice 3c: the item keys whose meaning IS a stat(2) field, and the five file-system keys. */
NSString *const NSFileSystemFileNumber = @"NSFileSystemFileNumber";
NSString *const NSFileReferenceCount = @"NSFileReferenceCount";
NSString *const NSFileDeviceIdentifier = @"NSFileDeviceIdentifier";
NSString *const NSFileSystemSize = @"NSFileSystemSize";
NSString *const NSFileSystemFreeSize = @"NSFileSystemFreeSize";
NSString *const NSFileSystemNodes = @"NSFileSystemNodes";
NSString *const NSFileSystemFreeNodes = @"NSFileSystemFreeNodes";
NSString *const NSFileSystemNumber = @"NSFileSystemNumber";
/* DECLARED AND NEVER FILLED, deliberately: this substrate keeps no birth time (see the header). */
NSString *const NSFileCreationDate = @"NSFileCreationDate";

NSString *const NSFileTypeRegular = @"NSFileTypeRegular";
NSString *const NSFileTypeDirectory = @"NSFileTypeDirectory";
NSString *const NSFileTypeSymbolicLink = @"NSFileTypeSymbolicLink";
NSString *const NSFileTypeBlockSpecial = @"NSFileTypeBlockSpecial";
NSString *const NSFileTypeCharacterSpecial = @"NSFileTypeCharacterSpecial";
NSString *const NSFileTypeSocket = @"NSFileTypeSocket";
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
	/* THE THREE THAT WERE BEING CALLED unknown (W8 slice 3c): Apple names a block device, a character
	 * device and a socket, and this kernel's stat(2) distinguishes all three - so "unknown" was this
	 * class not asking. A FIFO IS NOT ONE OF THEM and stays unknown, because Apple publishes no value
	 * for a fifo: the honest answer to a question whose vocabulary has no word is the unknown word. */
	if (S_ISBLK(mode)) {
		return NSFileTypeBlockSpecial;
	}
	if (S_ISCHR(mode)) {
		return NSFileTypeCharacterSpecial;
	}
	if (S_ISSOCK(mode)) {
		return NSFileTypeSocket;
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

		/* ONLY THE TWO NAVIGATION NAMES: this reader is the SHARED one behind both listings AND the
		 * recursive walks (remove, copy, move), so it must VISIT everything that exists. THE MEASURED
		 * TRAP: filtering Apple's `._` resource-fork rule HERE made such a file invisible to RECURSIVE
		 * REMOVAL, and the probe's tree cleanup then failed with "Directory not empty" - the rule belongs
		 * to a LISTING door, not to the traversal underneath it. */
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
	struct stat existing;

	if (![self fnDelegateShould:FNOperationCopy from:item to:target]) {
		return YES;		/* a veto skips the item; a directory's children are never reached */
	}
	/* THE DESTINATION MUST NOT EXIST, which is Apple's own sentence and the FIX for the data loss this
	 * file used to commit: the copy wrote through O_CREAT|O_TRUNC, so it silently REPLACED whatever it
	 * found there. The check is per ITEM rather than once at the door, because Apple states the rule
	 * per dstPath and because a race between the two would be the same bug with a smaller window. */
	if (lstat(to, &existing) == 0) {
		return [self fnFailed:EEXIST kind:FNOperationCopy from:item to:target error:outErrno];
	}
	if (lstat(from, &st) != 0) {
		return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
	}
	if (S_ISDIR(st.st_mode)) {
		int err = 0;
		NSArray *names;
		NSUInteger i;

		/* NO EEXIST TOLERANCE ANY MORE: the destination was just shown not to exist, so a failure
		 * here is a real failure rather than the ordinary case it used to be. */
		if (mkdir(to, st.st_mode & 07777) != 0) {
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
		/* THE BUFFER IS ON THE HEAP, AND THAT IS A FIX RATHER THAN A STYLE CHOICE (W8 slice 3): this
		 * system's user stack is TWELVE KILOBYTES (measured - a fault dump's own memory map shows
		 * 0x7fffffffd000 to 0x800000000000), so an 8KB array here left the whole call chain about
		 * three hundred bytes of headroom. It went unnoticed until a fix elsewhere in this file added
		 * one more frame and a COPY began to segfault before printing a line - which is exactly how a
		 * latent stack hazard behaves: not wrong, just spent. */
		char *buffer = malloc(8192);
		ssize_t n;

		if (in < 0) {
			return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
		}
		if (buffer == NULL) {
			close(in);
			return [self fnFailed:ENOMEM kind:FNOperationCopy from:item to:target error:outErrno];
		}
		out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
		if (out < 0) {
			int err = errno;

			close(in);
			free(buffer);
			return [self fnFailed:err kind:FNOperationCopy from:item to:target error:outErrno];
		}
		while ((n = read(in, buffer, 8192)) > 0) {
			ssize_t written = 0;

			while (written < n) {
				ssize_t step = write(out, buffer + written, (size_t)(n - written));

				if (step <= 0) {
					int err = errno != 0 ? errno : EIO;

					close(in);
					close(out);
					free(buffer);
					return [self fnFailed:err kind:FNOperationCopy from:item to:target
							error:outErrno];
				}
				written += step;
			}
		}
		close(in);
		close(out);
		free(buffer);
		if (n < 0) {
			return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
		}
		return YES;
	}
	/* A SYMLINK IS AN ITEM, AND A COPY OF ONE IS A LINK. Apple's copy pages say this only about a
	 * copy's DESTINATION ("if the last component of dstPath is a symbolic link, only the link is copied
	 * to the new path"); the SOURCE side is left unsaid, so the reading is stated WITH ITS GROUND rather
	 * than quoted: the same family treats links as items for equality ("does not traverse symbolic
	 * links, but compares the links themselves"), and a copy that FOLLOWED a link would make a copy of
	 * the link indistinguishable from a copy of its target - which is not what copying an ITEM can
	 * mean. This arm also makes a DANGLING link copyable, which the refusing version could not do. */
	if (S_ISLNK(st.st_mode)) {
		int err = 0;
		NSString *linkTarget = fn_link_target(item, &err);

		if (linkTarget == nil) {
			return [self fnFailed:err kind:FNOperationCopy from:item to:target error:outErrno];
		}
		if (symlink([linkTarget UTF8String], to) != 0) {
			return [self fnFailed:errno kind:FNOperationCopy from:item to:target error:outErrno];
		}
		return YES;
	}
	/* SOMETHING ELSE - a device, a socket, a fifo: named rather than guessed at, and the naming is the
	 * ERROR DOOR's business too, so a delegate may swallow it. */
	return [self fnFailed:ENOTSUP kind:FNOperationCopy from:item to:target error:outErrno];
}

/* A MOVE IS ONE rename(2), AND THE DELEGATE IS ASKED ONCE, FOR THE ITEM ITSELF. Apple states the
 * asymmetry as the difference from the copy above: "if the item being moved is a directory, the file
 * manager notifies the delegate only for the directory itself and not for any of its contents". */
- (BOOL)fnMoveItem:(NSString *)item to:(NSString *)target error:(int *)outErrno
{
	struct stat existing;

	if (![self fnDelegateShould:FNOperationMove from:item to:target]) {
		return YES;
	}
	/* AND THE SAME RULE, WHICH rename(2) WILL NOT ENFORCE: "if an item with the same name already
	 * exists at dstPath, this method stops the move attempt and returns an appropriate error" - while
	 * rename(2) replaces a destination FILE by design, silently. That difference is the whole reason
	 * this check is here rather than in the syscall. */
	if (lstat([target UTF8String], &existing) == 0) {
		return [self fnFailed:EEXIST kind:FNOperationMove from:item to:target error:outErrno];
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

/*
 * THE CONTENTS OF A DIRECTORY, WHICH WAS DECLARED AND NOT IMPLEMENTED - and the omission was invisible until
 * NSBundle asked for it, because a missing method is a raise and a raise inside a probe is an abort rather than
 * a failed check. The entries are returned SORTED: opendir's order is the file system's business, and a caller
 * comparing two listings should not have to know that.
 */
- (nullable NSArray *)directoryContentsAtPath:(NSString *)path
{
	NSMutableArray *names = [NSMutableArray array];
	struct dirent *entry;
	DIR *dir;

	if (path == nil) {
		return nil;
	}
	dir = opendir([path UTF8String]);
	if (dir == NULL) {
		return nil;
	}
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
			continue;
		}
		{
			NSString *name = [NSString stringWithUTF8String:entry->d_name];

			if (name != nil) {
				[names addObject:name];
			}
		}
	}
	closedir(dir);
	return [names sortedArrayUsingSelector:@selector(compare:)];
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
	/* AND THE THREE THAT ARE A stat(2) FIELD BY DEFINITION (W8 slice 3c), which is what Apple's own
	 * pages say of them one by one. */
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:(unsigned long long)st.st_ino]
		       forKey:NSFileSystemFileNumber];
	[attributes setObject:[NSNumber numberWithUnsignedLong:(unsigned long)st.st_nlink]
		       forKey:NSFileReferenceCount];
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:(unsigned long long)st.st_dev]
		       forKey:NSFileDeviceIdentifier];
	/* AND THE TWO ACCOUNT NAMES (W8 slice 3d), which are the same fact as the two IDs one line up -
	 * the difference is that the NAME has to come from the account database, which this library
	 * already reaches for other classes. An item whose uid has no account simply has no name entry,
	 * which is the dictionary's own way of saying so. */
	{
		struct passwd *pw = getpwuid((uid_t)st.st_uid);

		if (pw != NULL && pw->pw_name != NULL) {
			[attributes setObject:[NSString stringWithUTF8String:pw->pw_name]
				       forKey:NSFileOwnerAccountName];
		}
	}
	{
		struct group *gr = getgrgid((gid_t)st.st_gid);

		if (gr != NULL && gr->gr_name != NULL) {
			[attributes setObject:[NSString stringWithUTF8String:gr->gr_name]
				       forKey:NSFileGroupOwnerAccountName];
		}
	}
	return attributes;
}

/* ---- SETTING THEM (W8 slice 3d), AND THREE OF APPLE'S SENTENCES ARE THE WHOLE DESIGN -------------
 *
 *  1. "THE METHOD ATTEMPTS TO MAKE ALL CHANGES SPECIFIED IN ATTRIBUTES AND IGNORES ANY REJECTION OF AN
 *     ATTEMPTED MODIFICATION." So every key is tried and no individual failure is reported: the error
 *     channel is for the ITEM (a path that is not there, an absent dictionary), not for a chmod(2) the
 *     kernel refused. That is also why the loop below cannot fail.
 *  2. "IF THE LAST COMPONENT OF THE PATH IS A SYMBOLIC LINK, THE SYSTEM TRAVERSES IT" - so this door
 *     uses stat(2) where -attributesOfItemAtPath: uses lstat(2), and that one word is the difference
 *     between setting a target's permissions and asking a link about itself.
 *  3. "THE SYSTEM SETS NSFileOwnerAccountName AND NSFileGroupOwnerAccountName ONLY WHEN NSFileType
 *     SPECIFIES A FILE." Which is a strange rule and a measured one: the NAME keys are honoured only
 *     when the dictionary ALSO says NSFileType = NSFileTypeRegular. The ID keys carry no such
 *     condition, so a caller that wants to chown a directory gives IDs.
 */
- (BOOL)setAttributes:(NSDictionary *)attributes ofItemAtPath:(NSString *)path error:(NSError ** _Nullable)error
{
	struct stat st;

	if (path == nil || attributes == nil) {
		return fn_failed(error, EINVAL);
	}
	if (stat([path UTF8String], &st) != 0) {
		return fn_failed(error, errno);
	}
	/* THE PERMISSION BITS, and Apple says how they are spelled: "you must initialize the value with
	 * the code representing the POSIX file-permissions bit pattern". */
	if ([attributes objectForKey:NSFilePosixPermissions] != nil) {
		mode_t mode = (mode_t)[[attributes objectForKey:NSFilePosixPermissions] unsignedShortValue];

		chmod([path UTF8String], mode);	/* the result is IGNORED, by sentence 1 */
	}
	/* THE MODIFICATION DATE, through utimensat(2) with UTIME_OMIT for the access time: Apple's key
	 * names one date, and inventing a value for the other would be a change nobody asked for. */
	if ([attributes objectForKey:NSFileModificationDate] != nil) {
		double when = [[attributes objectForKey:NSFileModificationDate] timeIntervalSince1970];
		struct timespec times[2];
		double whole = floor(when);

		times[0].tv_sec = 0;
		times[0].tv_nsec = UTIME_OMIT;
		times[1].tv_sec = (time_t)whole;
		times[1].tv_nsec = (long)((when - whole) * 1000000000.0);
		utimensat(AT_FDCWD, [path UTF8String], times, 0);
	}
	/* THE OWNER AND THE GROUP BY ID FIRST, then by NAME, and the two are independent: chown(2) takes
	 * -1 for "leave that one alone", which is exactly what an abridged call needs. */
	if ([attributes objectForKey:NSFileOwnerAccountID] != nil ||
	    [attributes objectForKey:NSFileGroupOwnerAccountID] != nil) {
		uid_t uid = (uid_t)-1;
		gid_t gid = (gid_t)-1;

		if ([attributes objectForKey:NSFileOwnerAccountID] != nil) {
			uid = (uid_t)[[attributes objectForKey:NSFileOwnerAccountID] unsignedIntValue];
		}
		if ([attributes objectForKey:NSFileGroupOwnerAccountID] != nil) {
			gid = (gid_t)[[attributes objectForKey:NSFileGroupOwnerAccountID] unsignedIntValue];
		}
		chown([path UTF8String], uid, gid);
	}
	if ([[attributes objectForKey:NSFileType] isEqualToString:NSFileTypeRegular]) {
		if ([attributes objectForKey:NSFileOwnerAccountName] != nil) {
			struct passwd *pw = getpwnam([[attributes objectForKey:NSFileOwnerAccountName]
							UTF8String]);

			if (pw != NULL) {
				chown([path UTF8String], pw->pw_uid, (gid_t)-1);
			}
		}
		if ([attributes objectForKey:NSFileGroupOwnerAccountName] != nil) {
			struct group *gr = getgrnam([[attributes objectForKey:NSFileGroupOwnerAccountName]
							UTF8String]);

			if (gr != NULL) {
				chown([path UTF8String], (uid_t)-1, gr->gr_gid);
			}
		}
	}
	return YES;
}

/* ---- THE FILE SYSTEM'S OWN NUMBERS, AND WHERE AN ITEM STANDS (W8 slice 3c) -----------------------
 *
 * THE TWO UNITS APPLE STATES ITSELF, because both are easy to get wrong by one: the SIZES ARE BYTES
 * ("the size of the file system in bytes") rather than blocks, and the NUMBER is `st_dev` ("the value
 * corresponds to the value of st_dev, as returned by stat(2)") rather than the statfs(2) field a
 * reader would reach for first - `f_fsid`. So the block size multiplies the block counts here, and the
 * file-system number comes from the item's own stat, which is also why this door lstat(2)s first.
 */
- (nullable NSArray *)contentsOfDirectoryAtURL:(NSURL *)url
		     includingPropertiesForKeys:(nullable NSArray *)keys
					options:(NSUInteger)options
					  error:(NSError ** _Nullable)error
{
	NSString *path;
	NSArray *names;
	NSMutableArray *urls;
	NSUInteger i;

	if (url == nil || ![url isFileURL] || (path = [url path]) == nil) {
		fn_failed(error, EINVAL);
		return nil;
	}
	/* THE NAMES COME FROM THE PATH DOOR, so two listings cannot disagree about WHICH NAMES EXIST: dot,
	 * dot-dot and resource forks are filtered THERE, once, for every listing door. */
	names = [self contentsOfDirectoryAtPath:path error:error];
	if (names == nil) {
		return nil;
	}
	urls = [[NSMutableArray alloc] init];
	for (i = 0; i < [names count]; i++) {
		NSString *name = [names objectAtIndex:i];
		NSURL *child;

		/* APPLE'S NAME RULE FOR THIS DOOR, AND IT IS HERE RATHER THAN IN THE SHARED READER BECAUSE THE
		 * PROBE CAUGHT WHAT PUTTING IT UNDER THERE DID: a `._` name became invisible to RECURSIVE
		 * REMOVAL and the tree could not be deleted. A LISTING may hide it; a TRAVERSAL may not. */
		if ([name hasPrefix:@"._"]) {
			continue;
		}
		if ((options & NSDirectoryEnumerationSkipsHiddenFiles) != 0 && [name hasPrefix:@"."]) {
			continue;
		}
		child = [[NSURL alloc] initFileURLWithPath:
			 [NSString stringWithFormat:@"%@/%@", path, name]];
		if (child == nil) {
			continue;
		}
		/* THE PREFETCH: read the value NOW, on this directory's item, and hand it to the URL - which is
		 * what makes the returned object answer from the enumeration's moment rather than from the disk
		 * at the time of the question. */
		if (keys != nil) {
			NSUInteger k;

			for (k = 0; k < [keys count]; k++) {
				NSURLResourceKey key = [keys objectAtIndex:k];
				id value = nil;

				[child getResourceValue:&value forKey:key error:NULL];
				[child fnPrefetchValue:value forKey:key];
			}
		}
		[urls addObject:child];
		[child release];
	}
	return [urls autorelease];
}

- (nullable NSDirectoryEnumerator *)enumeratorAtURL:(NSURL *)url
			  includingPropertiesForKeys:(NSArray *)keys
					     options:(NSUInteger)options
					errorHandler:(BOOL (^)(NSURL *url, NSError *error))handler
{
	NSString *path;

	if (url == nil || ![url isFileURL] || (path = [url path]) == nil) {
		return nil;
	}
	return [[[NSDirectoryEnumerator alloc] initWithPath:path
						    options:options
					       prefetchKeys:keys
						 yieldsURLs:YES
					       errorHandler:handler] autorelease];
}

- (void)getFileProviderServicesForItemAtURL:(NSURL *)url
			  completionHandler:(void (^)(NSDictionary *, NSError *))completionHandler
{
	if (completionHandler == NULL) {
		return;
	}
	(void)url;
	/* EMPTY, AND CALLED: no file provider extension can be registered for an item on this system, so the
	 * set of services is empty - and the caller is TOLD, which is the difference between an empty answer
	 * and no answer. */
	completionHandler([NSDictionary dictionary], nil);
}

/* THE MOUNT TABLE, READ WHERE THE DOOR THAT NEEDS IT LIVES: `/System/Processes/mounts` is
 * `device mountpoint fstype rw|ro 0 0` per line (measured in fs/procfs/data.c) and the mount POINT is the
 * second field. The volume KEYS are answered in NSURL.m, which does its own read with the longest-prefix
 * rule; this is the same table read for its list. */
static NSArray *fn_file_system_mounts(void)
{
	/* THE SAME LOOP AS NSURL.m's READER, FOR THE SAME REASON: the table is a SYNTHETIC file that reports
	 * size zero, so reading it by size answers nothing. */
	id data = nil;
	{
		int fd = open("/System/Processes/mounts", O_RDONLY);

		if (fd >= 0) {
			NSMutableData *bytes = [[NSMutableData alloc] init];

			for (;;) {
				char buffer[2048];
				ssize_t got = read(fd, buffer, sizeof(buffer));

				if (got <= 0) {
					break;
				}
				[bytes appendBytes:buffer length:(NSUInteger)got];
			}
			close(fd);
			data = [bytes autorelease];
		}
	}
	NSString *text;
	NSMutableArray *answer;

	if (data == nil) {
		return nil;
	}
	text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];
	if (text == nil) {
		return nil;
	}
	answer = [NSMutableArray array];
	{
		NSArray *lines = [text componentsSeparatedByString:@"\n"];
		NSUInteger i;

		for (i = 0; i < [lines count]; i++) {
			NSArray *fields = [[lines objectAtIndex:i] componentsSeparatedByString:@" "];
			NSMutableArray *kept = [NSMutableArray array];
			NSUInteger f;

			for (f = 0; f < [fields count]; f++) {
				if ([[fields objectAtIndex:f] length] > 0) {
					[kept addObject:[fields objectAtIndex:f]];
				}
			}
			if ([kept count] >= 4) {
				[answer addObject:kept];
			}
		}
	}
	[text release];
	return answer;
}

- (nullable NSArray *)mountedVolumeURLsIncludingResourceValuesForKeys:(nullable NSArray *)propertyKeys
							      options:(NSVolumeEnumerationOptions)options
{
	/* THE TABLE IS READ THROUGH THE URL SIDE, where the parsing and the longest-prefix rule live: asking the
	 * root URL for the mounted volumes' keys would need the URL to expose them, so the read is duplicated
	 * HERE in its simplest form - one line per mount, the second field being the mount point. */
	NSArray *entries = fn_file_system_mounts();
	NSMutableArray *answer;

	(void)options;		/* see the header: this system has no hidden volumes to skip */
	if (entries == nil) {
		return nil;
	}
	answer = [NSMutableArray array];
	{
		NSUInteger i;

		for (i = 0; i < [entries count]; i++) {
			NSString *mountPoint = [[entries objectAtIndex:i] objectAtIndex:1];
			NSURL *url = [[NSURL alloc] initFileURLWithPath:mountPoint];
			NSUInteger k;

			if (url == nil) {
				continue;
			}
			/* THE KEYS A CALLER ASKED FOR ARE PREFETCHED INTO EACH VOLUME'S URL, which is the whole point
			 * of the door's name - and the same two-sided rule the directory listing uses (slice 6d). */
			if (propertyKeys != nil) {
				for (k = 0; k < [propertyKeys count]; k++) {
					NSURLResourceKey key = [propertyKeys objectAtIndex:k];
					id value = nil;

					[url getResourceValue:&value forKey:key error:NULL];
					[url fnPrefetchValue:value forKey:key];
				}
			}
			[answer addObject:url];
			[url release];
		}
	}
	return answer;
}

- (nullable NSDictionary *)attributesOfFileSystemForPath:(NSString *)path
						   error:(NSError ** _Nullable)error
{
	struct statfs fs;
	struct stat st;
	NSMutableDictionary *attributes;

	if (path == nil) {
		fn_failed(error, EINVAL);
		return nil;
	}
	/* "This method does not traverse a terminal symbolic link" - so the lstat is the contract and not
	 * just a way to get the device: a LINK's own attributes are what this door is about. */
	if (lstat([path UTF8String], &st) != 0) {
		fn_failed(error, errno);
		return nil;
	}
	if (statfs([path UTF8String], &fs) != 0) {
		fn_failed(error, errno);
		return nil;
	}
	attributes = [NSMutableDictionary dictionary];
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:
				(unsigned long long)fs.f_bsize * (unsigned long long)fs.f_blocks]
		       forKey:NSFileSystemSize];
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:
				(unsigned long long)fs.f_bsize * (unsigned long long)fs.f_bfree]
		       forKey:NSFileSystemFreeSize];
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:(unsigned long long)fs.f_files]
		       forKey:NSFileSystemNodes];
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:(unsigned long long)fs.f_ffree]
		       forKey:NSFileSystemFreeNodes];
	[attributes setObject:[NSNumber numberWithUnsignedLongLong:(unsigned long long)st.st_dev]
		       forKey:NSFileSystemNumber];
	return attributes;
}

/* WHERE AN ITEM STANDS RELATIVE TO A DIRECTORY: "the directory may contain the item, it may be the
 * same as the item, or it may not have a direct relationship to the item." THE COMPARISON IS A PATH
 * ONE - this door is about locations and not about inodes, which is why two paths meaning one file
 * (a hard link, or a link and its target) can answer Other. Both paths must exist, because a
 * relationship between a location and nothing is not a relationship; Apple leaves that case unsaid and
 * this class's error channel is the honest place to put it. */
- (BOOL)getRelationship:(NSURLRelationship *)outRelationship
      ofDirectoryAtPath:(NSString *)directory
	    toItemAtPath:(NSString *)otherPath
		   error:(NSError ** _Nullable)error
{
	struct stat st;

	if (directory == nil || otherPath == nil || outRelationship == NULL) {
		return fn_failed(error, EINVAL);
	}
	if (lstat([directory UTF8String], &st) != 0 || lstat([otherPath UTF8String], &st) != 0) {
		return fn_failed(error, errno);
	}
	if ([otherPath isEqualToString:directory]) {
		*outRelationship = NSURLRelationshipSame;
		return YES;
	}
	/* A PREFIX IS NOT CONTAINMENT UNLESS IT ENDS AT A SEPARATOR: `/a/bc` is not inside `/a/b`, and a
	 * comparison that forgot the slash would say it is. */
	if ([otherPath hasPrefix:directory]) {
		NSString *rest = [otherPath substringFromIndex:[directory length]];

		if ([rest hasPrefix:@"/"] || [directory hasSuffix:@"/"]) {
			*outRelationship = NSURLRelationshipContains;
			return YES;
		}
	}
	*outRelationship = NSURLRelationshipOther;
	return YES;
}

/* ---- THE FILE'S BYTES, THE EQUALITY RULE, AND THE OTHER KIND OF LINK (W8 slice 3) ---------------
 *
 * ONE READER FOR "THE CONTENTS OF A FILE", because two callers need it and they must not disagree:
 * -contentsAtPath: answers it, and the equality rule compares files with it. It uses stat(2) rather
 * than lstat(2) on purpose - what it answers is the contents of the file the path NAMES, so a link is
 * followed - and it refuses a DIRECTORY, which is Apple's own exclusion: "if `path` specifies a
 * directory, or if some other error occurs, this method returns nil".
 */
static NSData *fn_file_data(NSString *path)
{
	NSMutableData *data;
	/* HEAP, NOT STACK, for the same measured reason the copy's buffer is (§60 slice 3): a 12KB user
	 * stack cannot afford an 8KB array with callers underneath it. */
	char *buffer;
	struct stat st;
	ssize_t n;
	int fd;

	if (stat([path UTF8String], &st) != 0 || S_ISDIR(st.st_mode)) {
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
	if (n < 0) {
		return nil;
	}
	return data;
}

/* A LINK'S TARGET, or nil: read at the link itself, which is the only way to compare two links as
 * LINKS - Apple's "does not traverse symbolic links, but compares the links themselves".
 *
 * `outErrno` MAY BE NULL, AND THE ONE CALLER THAT NEEDS IT IS THE COPY: a comparison can treat a
 * failure as a NO, while a copy has to REPORT why it could not read the link - and since the buffer is
 * freed before the nil is returned, the errno is captured on the spot rather than read afterwards
 * (free(3) is not something to read errno around). */
static NSString *fn_link_target(NSString *path, int *outErrno)
{
	/* HEAP AGAIN, and 4KB of stack is no more affordable here than 8KB was above. */
	char *buffer = malloc(4096);
	NSString *target;
	ssize_t n;

	if (buffer == NULL) {
		if (outErrno != NULL) {
			*outErrno = ENOMEM;
		}
		return nil;
	}
	n = readlink([path UTF8String], buffer, 4095);
	if (n < 0) {
		int err = errno;

		free(buffer);
		if (outErrno != NULL) {
			*outErrno = err;
		}
		return nil;
	}
	buffer[n] = '\0';
	target = [NSString stringWithUTF8String:buffer];
	free(buffer);
	return target;
}

- (nullable NSData *)contentsAtPath:(NSString *)path
{
	if (path == nil) {
		return nil;
	}
	return fn_file_data(path);
}

/* THE DIRECTORY HALF OF THE EQUALITY RULE: "the contents are the list of files and subdirectories each
 * contains - contents of subdirectories are also compared". The NAMES are compared as a SET, because
 * readdir's order is not a promise either side makes, and each name must then compare equal in turn -
 * which is what makes this recursive rather than a count of entries. */
- (BOOL)fnDirectory:(NSString *)first equalsDirectory:(NSString *)second
{
	NSArray *names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:first error:NULL];
	NSArray *others = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:second error:NULL];
	NSUInteger i;

	if (names == nil || others == nil) {
		return names == nil && others == nil;
	}
	if ([names count] != [others count]) {
		return NO;
	}
	for (i = 0; i < [names count]; i++) {
		NSString *name = [names objectAtIndex:i];

		if (![others containsObject:name]) {
			return NO;
		}
		if (![[NSFileManager defaultManager] contentsEqualAtPath:fn_joined(first, name)
								andPath:fn_joined(second, name)]) {
			return NO;
		}
	}
	return YES;
}

- (BOOL)contentsEqualAtPath:(NSString *)path1 andPath:(NSString *)path2
{
	struct stat a;
	struct stat b;
	NSData *mine;
	NSData *theirs;

	if (path1 == nil || path2 == nil) {
		return NO;
	}
	/* lstat, because a link is an ITEM here and not a window onto its target. */
	if (lstat([path1 UTF8String], &a) != 0 || lstat([path2 UTF8String], &b) != 0) {
		return NO;
	}
	/* APPLE'S FIRST STEP: "checks to see if they're the same file" - the filesystem's own identity
	 * (device and inode), not a comparison of the pathnames. */
	if (a.st_dev == b.st_dev && a.st_ino == b.st_ino) {
		return YES;
	}
	if ((S_ISLNK(a.st_mode) ? YES : NO) != (S_ISLNK(b.st_mode) ? YES : NO)) {
		return NO;		/* a link never equals the file it points at */
	}
	if (S_ISLNK(a.st_mode)) {
		/* STRINGS, NOT DATA, AND THAT IS WHAT THIS BRANCH IS ABOUT: a link's "contents" is its TARGET PATH, and
		 * fn_link_target returns an NSString. The values were being assigned to the NSData slots the CONTENT
		 * branch below uses - the kind of type confusion the compiler reports and a reader of either branch
		 * would not notice. */
		NSString *mineLink = fn_link_target(path1, NULL);
		NSString *theirsLink = fn_link_target(path2, NULL);

		return mineLink != nil && [mineLink isEqualToString:theirsLink];
	}
	if (S_ISDIR(a.st_mode) || S_ISDIR(b.st_mode)) {
		if (!S_ISDIR(a.st_mode) || !S_ISDIR(b.st_mode)) {
			return NO;	/* a directory never equals a file */
		}
		return [self fnDirectory:path1 equalsDirectory:path2];
	}
	if (!S_ISREG(a.st_mode) || !S_ISREG(b.st_mode)) {
		return NO;		/* two devices, sockets or fifos are not compared by this rule */
	}
	/* THE SIZE BEFORE THE BYTES, which is Apple's order and the cheap half of it as well. */
	if (a.st_size != b.st_size) {
		return NO;
	}
	mine = fn_file_data(path1);
	theirs = fn_file_data(path2);
	return mine != nil && [mine isEqualToData:theirs];
}

- (BOOL)createSymbolicLinkAtPath:(NSString *)path
	     withDestinationPath:(NSString *)destPath
			   error:(NSError ** _Nullable)error
{
	if (path == nil || destPath == nil) {
		return fn_failed(error, EINVAL);
	}
	/* symlink(2) DOES NOT RESOLVE ITS TARGET, which is exactly why this door can make a link to
	 * somewhere that does not exist yet - Apple's own sentence about it, and the reason there is no
	 * check here to "helpfully" refuse a dangling one. */
	if (symlink([destPath UTF8String], [path UTF8String]) != 0) {
		return fn_failed(error, errno);
	}
	return YES;
}

/* ---- WHAT TO SHOW A USER (W8 slice 3e), AND THE RULE IS A DECISION WITH ITS GROUNDS ---------------
 *
 * APPLE'S TWO SENTENCES, AND WHAT THIS SYSTEM CAN DO WITH THEM: a display name is "the name of the file
 * or directory at path in a LOCALIZED form appropriate for presentation", and the discussion allows
 * that such names "MAY also reflect other modifications, such as the removal of filename extensions".
 * THERE IS NO LOCALIZATION DATABASE HERE - no `.lproj`, no language setting - so a localized name has no
 * value to take other than the item's own, and the "may" is what makes that conforming rather than a
 * shortcut. The failure case is exact and is Apple's: "returns path AS IS" when there is no item there.
 */
- (nullable NSString *)displayNameAtPath:(NSString *)path
{
	struct stat st;

	if (path == nil) {
		return nil;
	}
	if (lstat([path UTF8String], &st) != 0) {
		return path;	/* Apple's sentence, literally: the path itself */
	}
	return [path lastPathComponent];
}

- (nullable NSArray *)componentsToDisplayForPath:(NSString *)path
{
	NSArray *parts;
	NSMutableArray *components;
	struct stat st;
	NSUInteger i;

	if (path == nil) {
		return nil;
	}
	if (lstat([path UTF8String], &st) != 0) {
		return nil;	/* "returns nil if path does not exist" */
	}
	parts = [path componentsSeparatedByString:@"/"];
	components = [NSMutableArray array];
	for (i = 0; i < [parts count]; i++) {
		NSString *part = [parts objectAtIndex:i];

		if ([part length] > 0) {
			/* NO LOCALIZATION, so a component's display name is the component - and the EMPTY pieces
			 * a leading slash makes are dropped, which is also why a path of just "/" answers an
			 * array with nothing in it. */
			[components addObject:part];
		}
	}
	return components;
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

/*
 * THE USER-DIRECTORY FUNCTIONS, AND THE ROW THE HEADER DEFERRED IS PAID HERE. A home directory is not a policy
 * this class invents: it is the pw_dir the ACCOUNT DATABASE answers - the same getpwuid(3) this file already
 * uses for account NAMES and NSUserDefaults uses for its own directory. An unknown user answers nil, which is
 * Apple's contract for NSHomeDirectoryForUser:, and a uid with no entry falls back to "/" rather than nil,
 * because Apple's NSHomeDirectory() is documented never to fail.
 */
NSString *NSUserName(void)
{
	struct passwd *pw = getpwuid(getuid());

	return (pw != NULL && pw->pw_name != NULL) ? [NSString stringWithUTF8String:pw->pw_name] : nil;
}

NSString *NSFullUserName(void)
{
	struct passwd *pw = getpwuid(getuid());

	/* The GECOS field is where the account database keeps the display name; an empty one is no name at all. */
	if (pw == NULL || pw->pw_gecos == NULL || pw->pw_gecos[0] == '\0') {
		return nil;
	}
	return [NSString stringWithUTF8String:pw->pw_gecos];
}

NSString *NSHomeDirectory(void)
{
	struct passwd *pw = getpwuid(getuid());

	if (pw == NULL || pw->pw_dir == NULL || pw->pw_dir[0] == '\0') {
		return @"/";
	}
	return [NSString stringWithUTF8String:pw->pw_dir];
}

NSString *NSHomeDirectoryForUser(NSString *userName)
{
	struct passwd *pw;

	if (userName == nil) {
		return nil;
	}
	pw = getpwnam([userName UTF8String]);
	if (pw == NULL || pw->pw_dir == NULL || pw->pw_dir[0] == '\0') {
		return nil;
	}
	return [NSString stringWithUTF8String:pw->pw_dir];
}

NSString *NSOpenStepRootDirectory(void)
{
	return @"/";
}

/*
 * THE TWO HFS TYPE-CODE FUNCTIONS ARE STRING ARITHMETIC over a four-character code, which is the whole of what
 * they are: a classic Mac file type is four characters packed into a 32-bit integer. NSHFSTypeOfFile() is NOT
 * implemented, because the type of a file on classic HFS lives in a RESOURCE FORK this system does not have -
 * so it answers nil and says so on the log rather than inventing a file-system fact.
 */
NSString *NSFileTypeForHFSTypeCode(unsigned int hfsTypeCode)
{
	char code[5];
	int i;

	for (i = 0; i < 4; i++) {
		code[i] = (char)((hfsTypeCode >> (8 * (3 - i))) & 0xff);
	}
	code[4] = '\0';
	return [NSString stringWithUTF8String:code];
}

unsigned int NSHFSTypeCodeFromFileType(NSString *fileType)
{
	const char *bytes = (fileType != nil) ? [fileType UTF8String] : NULL;
	unsigned int code = 0;
	int i;

	if (bytes == NULL || strlen(bytes) != 4) {
		return 0;
	}
	for (i = 0; i < 4; i++) {
		code = (code << 8) | (unsigned char)bytes[i];
	}
	return code;
}

NSString *NSHFSTypeOfFile(NSString *fullFilePath)
{
	const char *path = (fullFilePath != nil) ? [fullFilePath UTF8String] : NULL;
	char line[256];
	int n = snprintf(line, sizeof line,
			 "Foundation: -NSHFSTypeOfFile refused for %s: an HFS type lives in a resource fork this "
			 "system does not have\n", path != NULL ? path : "(null)");

	if (n > 0) {
		(void)write(2, line, (size_t)n);
	}
	return nil;
}

/*
 * THE LEGACY SEARCH IS A NAMED REFUSAL, NOT AN EMPTY ANSWER. Apple's
 * NSSearchPathForDirectoriesInDomains() answers a LIST OF PATHS for a domain, and this system's file-system
 * hierarchy does not have that shape: its directories are reached BY NAME (System/Libraries, System/Temporary
 * Files, ...) and there is no domain-mask search behind them. Returning an empty array silently would look like
 * "no such directory exists", which is a different statement from "this system does not answer that question",
 * so the refusal is written on the log where it can be seen.
 */
NSArray *NSSearchPathForDirectoriesInDomains(NSSearchPathDirectory directory,
							 NSSearchPathDomainMask domainMask, BOOL expandTilde)
{
	char line[256];
	int n = snprintf(line, sizeof line,
			 "Foundation: NSSearchPathForDirectoriesInDomains refused (directory %ld, mask %lu): this "
			 "system's directories are named, not searched\n",
			 (long)directory, (unsigned long)domainMask);

	(void)expandTilde;
	if (n > 0) {
		(void)write(2, line, (size_t)n);
	}
	return @[];
}

NSString *const NSFileManagerUnmountDissentingProcessIdentifierErrorKey =
	@"NSFileManagerUnmountDissentingProcessIdentifierErrorKey";
