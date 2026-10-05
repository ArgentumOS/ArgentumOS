/*
 * NSFileVersion.m — the current version, the empty collections, and the store doors refused by name
 * (W8 slice 8a). See the header for the split and for which choices are ours.
 */

#import <Foundation/NSFileVersion.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>

/* AN ERROR THE WAY THIS LIBRARY MAKES THEM. */
static NSError *fn_version_error(int err, NSString *what)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:[NSDictionary dictionaryWithObject:
					 [NSString stringWithFormat:@"%@: %s", what, strerror(err)]
								    forKey:NSLocalizedDescriptionKey]];
}

@implementation NSFileVersion

/* THE CURRENT VERSION, BUILT FROM ONE lstat: the item's URL, its modification date, and the opaque
 * identifier this class defines. Nil for anything that is not an existing item - which is what Apple's
 * nullable return allows and what a caller checking for existence would expect. */
+ (nullable NSFileVersion *)currentVersionOfItemAtURL:(NSURL *)url
{
	struct stat st;
	NSFileVersion *version;

	if (url == nil || ![url isFileURL] || lstat([[url path] UTF8String], &st) != 0) {
		return nil;
	}
	version = [[self alloc] init];
	if (version == nil) {
		return nil;
	}
	version->_url = [url retain];
	version->_modificationDate = [[NSDate dateWithTimeIntervalSince1970:(double)st.st_mtime] retain];
	/* THE IDENTIFIER IS OURS AND OPAQUE, and its one promise is Apple's own: "an identifier that can be
	 * used to refer to this version in the future". Device, inode and modification date identify this
	 * snapshot exactly, so a lookup that is handed one of these can be answered - and any other string is
	 * refused rather than answered with nil in silence. */
	version->_identifier = [[NSString stringWithFormat:@"%llu:%llu:%lld",
				 (unsigned long long)st.st_dev, (unsigned long long)st.st_ino,
				 (long long)st.st_mtime] retain];
	return [version autorelease];
}

+ (nullable NSArray *)otherVersionsOfItemAtURL:(NSURL *)url
{
	if (url == nil || ![url isFileURL]) {
		return nil;
	}
	/* THE POSTCONDITION, NOT A DEGRADATION: "the versions of the item OTHER than the current one" is an
	 * empty set on a system whose file systems do not store them. */
	return [NSArray array];
}

+ (nullable NSArray *)unresolvedConflictVersionsOfItemAtURL:(NSURL *)url
{
	if (url == nil || ![url isFileURL]) {
		return nil;
	}
	return [NSArray array];
}

+ (BOOL)removeOtherVersionsOfItemAtURL:(NSURL *)url error:(NSError **)error
{
	if (error != NULL) {
		*error = nil;
	}
	if (url == nil || ![url isFileURL]) {
		if (error != NULL) {
			*error = fn_version_error(EINVAL, @"a version's item must be a file URL");
		}
		return NO;
	}
	/* THERE ARE NONE, so the postcondition the caller asked for already holds. */
	return YES;
}

+ (nullable NSFileVersion *)versionOfItemAtURL:(NSURL *)url
			 forPersistentIdentifier:(NSString *)persistentIdentifier
{
	NSFileVersion *current = [self currentVersionOfItemAtURL:url];

	if (current == nil || persistentIdentifier == nil) {
		return nil;
	}
	/* ONLY WHAT THIS CLASS HANDED OUT CAN BE HANDED BACK, which is what makes the identifier meaningful
	 * rather than a string that happens to match. */
	if ([[current persistentIdentifier] isEqual:persistentIdentifier]) {
		return current;
	}
	return nil;
}

+ (nullable NSFileVersion *)addVersionOfItemAtURL:(NSURL *)url
				   withContentsOfURL:(NSURL *)contentsURL
					   options:(NSFileVersionAddingOptions)options
					     error:(NSError **)error
{
	(void)contentsURL;
	(void)options;
	if (error != NULL) {
		/* REGISTERED, AND REFUSED BY NAME: there is no version store for the version to be added to. */
		*error = fn_version_error(ENOTSUP, [NSString stringWithFormat:
			@"this system has no version store, so a version of %@ cannot be added", [url path]]);
	}
	return nil;
}

+ (nullable NSURL *)temporaryDirectoryURLForNewVersionOfItemAtURL:(NSURL *)url
{
	(void)url;
	/* REGISTERED: the directory exists to stage a version FOR THE STORE, and there is no store. */
	return nil;
}

+ (void)getNonlocalVersionsOfItemAtURL:(NSURL *)url
		     completionHandler:(void (^)(NSArray *nonlocalVersions, NSError *error))completionHandler
{
	if (completionHandler == NULL) {
		return;
	}
	(void)url;
	/* NONLOCAL MEANS "NOT ON THIS MACHINE", which is what a cloud or a file provider supplies - and there
	 * is neither, so the honest answer is an EMPTY array and no error: the question was asked and answered,
	 * rather than left hanging for a store that will never arrive. */
	completionHandler([NSArray array], nil);
}

- (NSURL *)URL
{
	return _url;
}

- (nullable NSString *)localizedName
{
	/* THE ITEM'S OWN NAME: this system has no localisation database, which is the same rule
	 * -displayNameAtPath: and NSURLLocalizedNameKey follow. */
	return [[_url path] lastPathComponent];
}

- (nullable NSDate *)modificationDate
{
	return _modificationDate;
}

- (nullable NSString *)persistentIdentifier
{
	return _identifier;
}

- (BOOL)isConflict
{
	return NO;
}

- (BOOL)hasLocalContents
{
	return YES;
}

- (BOOL)hasThumbnail
{
	return NO;
}

- (BOOL)removeAndReturnError:(NSError **)outError
{
	if (outError != NULL) {
		/* THE CURRENT VERSION IS THE FILE: there is no store to remove it from, and pretending otherwise
		 * would suggest the item had been rolled back. */
		*outError = fn_version_error(ENOTSUP, @"the current version of an item cannot be removed");
	}
	return NO;
}

- (void)dealloc
{
	[_url release];
	[_modificationDate release];
	[_identifier release];
	[super dealloc];
}

@end
