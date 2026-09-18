/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nsfilemanager.m — the file system as a service (F13.14). MANUAL OWNERSHIP.
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

#import <foundation/NSFileManager.h>
#import <foundation/NSArray.h>
#import <foundation/NSData.h>
#import <foundation/NSDate.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSError.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
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

static BOOL fn_remove_tree(const char *path, int *outErrno)
{
	struct stat st;

	if (lstat(path, &st) != 0) {
		*outErrno = errno;
		return NO;
	}
	if (S_ISDIR(st.st_mode)) {
		NSArray *names = fn_directory_names(path, outErrno);
		NSUInteger i;

		if (names == nil) {
			return NO;
		}
		for (i = 0; i < [names count]; i++) {
			NSString *child = fn_joined([NSString stringWithUTF8String:path],
						    [names objectAtIndex:i]);

			if (!fn_remove_tree([child UTF8String], outErrno)) {
				return NO;
			}
		}
	}
	if (remove(path) != 0) {
		*outErrno = errno;
		return NO;
	}
	return YES;
}

static BOOL fn_copy_tree(const char *from, const char *to, int *outErrno)
{
	struct stat st;

	if (lstat(from, &st) != 0) {
		*outErrno = errno;
		return NO;
	}
	if (S_ISDIR(st.st_mode)) {
		NSArray *names;

		if (mkdir(to, st.st_mode & 07777) != 0 && errno != EEXIST) {
			*outErrno = errno;
			return NO;
		}
		names = fn_directory_names(from, outErrno);
		if (names == nil) {
			return NO;
		}
		{
			NSUInteger i;

			for (i = 0; i < [names count]; i++) {
				NSString *name = [names objectAtIndex:i];
				NSString *source = fn_joined([NSString stringWithUTF8String:from], name);
				NSString *destination = fn_joined([NSString stringWithUTF8String:to], name);

				if (!fn_copy_tree([source UTF8String], [destination UTF8String], outErrno)) {
					return NO;
				}
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
			*outErrno = errno;
			return NO;
		}
		out = open(to, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode & 07777);
		if (out < 0) {
			*outErrno = errno;
			close(in);
			return NO;
		}
		while ((n = read(in, buffer, sizeof(buffer))) > 0) {
			ssize_t written = 0;

			while (written < n) {
				ssize_t step = write(out, buffer + written, (size_t)(n - written));

				if (step <= 0) {
					*outErrno = errno != 0 ? errno : EIO;
					close(in);
					close(out);
					return NO;
				}
				written += step;
			}
		}
		close(in);
		close(out);
		if (n < 0) {
			*outErrno = errno;
			return NO;
		}
		return YES;
	}
	/* A SYMLINK OR SOMETHING ELSE: named rather than guessed at. */
	*outErrno = ENOTSUP;
	return NO;
}

@implementation NSFileManager

+ (NSFileManager *)defaultManager
{
	if (fn_shared_file_manager == nil) {
		fn_shared_file_manager = [[NSFileManager alloc] init];
	}
	return fn_shared_file_manager;
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
	if ([parent length] == 0) {
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

			if ([part length] == 0) {
				/* THE LEADING SLASH of an absolute path, or a doubled one. */
				if (i == 0) {
					[prefix appendString:@"/"];
				}
				continue;
			}
			if ([prefix length] > 1 || ([prefix length] == 1 && [prefix isEqualToString:@"/"])) {
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
	if (!fn_remove_tree([path UTF8String], &err)) {
		return fn_failed(error, err);
	}
	return YES;
}

- (BOOL)moveItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error
{
	if (sourcePath == nil || destinationPath == nil) {
		return fn_failed(error, EINVAL);
	}
	/* ONE SYSCALL, AND IT IS ONLY ONE BECAUSE BOTH PATHS ARE ON ONE FILESYSTEM: an EXDEV comes
	 * back to the caller as itself rather than being silently turned into a copy. */
	if (rename([sourcePath UTF8String], [destinationPath UTF8String]) != 0) {
		return fn_failed(error, errno);
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
	if (!fn_copy_tree([sourcePath UTF8String], [destinationPath UTF8String], &err)) {
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
