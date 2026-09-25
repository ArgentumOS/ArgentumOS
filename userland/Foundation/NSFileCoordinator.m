/*
 * NSFileCoordinator.m — the synchronous accessor doors (W8 slice 7b). See the header for the measured
 * semantics, for why the doors are `void` with the error going to `outError`, and for the boundary: this
 * system has no coordination service, so what is coordinated here is what happens WITHIN one process.
 */

#import <Foundation/NSFileCoordinator.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

/* AN ERROR THE WAY THIS LIBRARY MAKES THEM. */
static NSError *fn_coordinator_error(int err, NSString *what)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:[NSDictionary dictionaryWithObject:
					 [NSString stringWithFormat:@"%@: %s", what, strerror(err)]
								    forKey:NSLocalizedDescriptionKey]];
}

@implementation NSFileCoordinator

- (id)initWithFilePresenter:(id)filePresenterOrNil
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* NOT RETAINED: the presenter is the object that created us and holds us, so retaining it would make
	 * a cycle of two objects that can never be freed. Apple's page says the presenter "is assumed to be
	 * performing the relevant file or directory operations", which is the same relationship. */
	_presenter = filePresenterOrNil;
	[super init];		/* the door's own name, kept beside the assignment */
	return self;
}

/* ONE PLACE WHERE A URL IS VALIDATED AND (IF ASKED) RESOLVED, so all four doors agree about what a
 * coordinated item is: a file URL that is not nil. A refusal answers an error and tells the caller NOT to
 * run the accessor, which is the shape Apple's page describes for a failed preparation. */
- (nullable NSURL *)fnCoordinatedURL:(nullable NSURL *)url
			      resolve:(BOOL)resolve
				outError:(NSError **)outError
{
	if (url == nil || ![url isFileURL]) {
		if (outError != NULL) {
			*outError = fn_coordinator_error(EINVAL, @"a coordinated item must be a file URL");
		}
		return nil;
	}
	if (resolve) {
		/* NSFileCoordinatorReadingResolvesSymbolicLink IS THE ONE OPTION WITH A MEANING HERE, and what
		 * it means is that the ACCESSOR sees the resolved item - realpath(3) is the same resolution the
		 * file system itself performs, and the accessor is handed the result because Apple's page says
		 * the URL passed to the block is the authoritative one. */
		char *resolved = realpath([[url path] UTF8String], NULL);

		if (resolved != NULL) {
			NSString *path = [NSString stringWithUTF8String:resolved];
			NSURL *answer;

			free(resolved);
			answer = path != nil ? [NSURL fileURLWithPath:path] : nil;
			if (answer != nil) {
				return answer;
			}
		}
	}
	return url;
}

- (void)coordinateReadingItemAtURL:(NSURL *)url
			   options:(NSFileCoordinatorReadingOptions)options
			     error:(NSError **)outError
			byAccessor:(void (^)(NSURL *newURL))reader
{
	NSURL *coordinated;

	if (outError != NULL) {
		*outError = nil;
	}
	if (reader == NULL) {
		if (outError != NULL) {
			*outError = fn_coordinator_error(EINVAL, @"a coordinated read needs an accessor");
		}
		return;
	}
	coordinated = [self fnCoordinatedURL:url
				     resolve:(options & NSFileCoordinatorReadingResolvesSymbolicLink) != 0
				    outError:outError];
	if (coordinated == nil) {
		return;		/* the error is set and the accessor does NOT run */
	}
	reader(coordinated);
}

- (void)coordinateWritingItemAtURL:(NSURL *)url
			   options:(NSFileCoordinatorWritingOptions)options
			     error:(NSError **)outError
			byAccessor:(void (^)(NSURL *newURL))writer
{
	NSURL *coordinated;

	(void)options;
	if (outError != NULL) {
		*outError = nil;
	}
	if (writer == NULL) {
		if (outError != NULL) {
			*outError = fn_coordinator_error(EINVAL, @"a coordinated write needs an accessor");
		}
		return;
	}
	coordinated = [self fnCoordinatedURL:url resolve:NO outError:outError];
	if (coordinated == nil) {
		return;
	}
	writer(coordinated);
}

- (void)coordinateReadingItemAtURL:(NSURL *)readingURL
			   options:(NSFileCoordinatorReadingOptions)readingOptions
		  writingItemAtURL:(NSURL *)writingURL
			   options:(NSFileCoordinatorWritingOptions)writingOptions
			     error:(NSError **)outError
			byAccessor:(void (^)(NSURL *newReadingURL, NSURL *newWritingURL))readerWriter
{
	NSURL *reading;
	NSURL *writing;

	(void)writingOptions;
	if (outError != NULL) {
		*outError = nil;
	}
	if (readerWriter == NULL) {
		if (outError != NULL) {
			*outError = fn_coordinator_error(EINVAL, @"a coordinated operation needs an accessor");
		}
		return;
	}
	reading = [self fnCoordinatedURL:readingURL
				 resolve:(readingOptions & NSFileCoordinatorReadingResolvesSymbolicLink) != 0
				outError:outError];
	if (reading == nil) {
		return;
	}
	writing = [self fnCoordinatedURL:writingURL resolve:NO outError:outError];
	if (writing == nil) {
		return;
	}
	readerWriter(reading, writing);
}

- (void)coordinateWritingItemAtURL:(NSURL *)url1
			   options:(NSFileCoordinatorWritingOptions)options1
		  writingItemAtURL:(NSURL *)url2
			   options:(NSFileCoordinatorWritingOptions)options2
			     error:(NSError **)outError
			byAccessor:(void (^)(NSURL *newURL1, NSURL *newURL2))writer
{
	NSURL *first;
	NSURL *second;

	(void)options1;
	(void)options2;
	if (outError != NULL) {
		*outError = nil;
	}
	if (writer == NULL) {
		if (outError != NULL) {
			*outError = fn_coordinator_error(EINVAL, @"a coordinated operation needs an accessor");
		}
		return;
	}
	first = [self fnCoordinatedURL:url1 resolve:NO outError:outError];
	if (first == nil) {
		return;
	}
	second = [self fnCoordinatedURL:url2 resolve:NO outError:outError];
	if (second == nil) {
		return;
	}
	writer(first, second);
}

@end
