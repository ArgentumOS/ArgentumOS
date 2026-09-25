/*
 * NSFileCoordinator.m — the synchronous accessor doors (W8 slice 7b). See the header for the measured
 * semantics, for why the doors are `void` with the error going to `outError`, and for the boundary: this
 * system has no coordination service, so what is coordinated here is what happens WITHIN one process.
 */

#import <Foundation/NSFileCoordinator.h>
#import <Foundation/NSFilePresenter.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSError.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <errno.h>
#include <pthread.h>

/* THE BLOCK RUNTIME, declared here rather than included, which is how NSFileHandle and
 * NSDirectoryEnumerator do it too. */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);
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


/* ---- THE PROCESS-WIDE REGISTRY AND THE HANDSHAKE (W8 slice 7c) ------------------------------------ */

/* THE REACQUIRERS ARE HELD IN A C ARRAY AND NOT IN A COLLECTION, because A BLOCK IS OWNED WITH
 * _Block_copy/_Block_release AND NOT WITH A MESSAGE SEND - which is not a style preference here: the
 * clean-room gate refuses a block owned any other way, and it refused this file's first version, which put
 * each one into an NSMutableArray with -addObject:/-autorelease. */
struct fn_reacquirers {
	void (^ *items)(void);
	size_t count;
	size_t capacity;
};

static NSMutableArray *fn_presenters = nil;		/* process wide; holds NO references (see the header) */
static NSLock *fn_presenters_lock = nil;
static pthread_mutex_t fn_handshake_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fn_handshake_cond = PTHREAD_COND_INITIALIZER;

@implementation NSFileCoordinator

+ (void)addFilePresenter:(id<NSFilePresenter>)filePresenter
{
	if (filePresenter == nil) {
		return;
	}
	if (fn_presenters_lock == nil) {
		fn_presenters_lock = [[NSLock alloc] init];
	}
	if (fn_presenters == nil) {
		fn_presenters = [[NSMutableArray alloc] init];
	}
	[fn_presenters_lock lock];
	/* ONE PRESENTER IS REGISTERED ONCE: Apple's balancing rule is about add/remove pairs, and a registry
	 * that held two entries for one object would ask it to relinquish twice for one operation. */
	if ([fn_presenters indexOfObjectIdenticalTo:filePresenter] == NSNotFound) {
		[fn_presenters addObject:filePresenter];
	}
	[fn_presenters_lock unlock];
}

+ (void)removeFilePresenter:(id<NSFilePresenter>)filePresenter
{
	[fn_presenters_lock lock];
	{
		NSUInteger at = [fn_presenters indexOfObjectIdenticalTo:filePresenter];

		if (at != NSNotFound) {
			[fn_presenters removeObjectAtIndex:at];
		}
	}
	[fn_presenters_lock unlock];
}

+ (NSArray *)filePresenters
{
	NSArray *copy;

	[fn_presenters_lock lock];
	copy = [NSArray arrayWithArray:fn_presenters != nil ? fn_presenters : [NSArray array]];
	[fn_presenters_lock unlock];
	return copy;
}

/* ONE ITEM'S HANDSHAKE, AND IT IS WHERE A WRONG READING WOULD HANG RATHER THAN ANSWER WRONGLY. Apple's
 * relinquish pages say the presenter must execute the block it is given, and pass back a REACQUIRER for
 * the coordinator to execute once the operation is over - so: the coordinator CALLS the right relinquish
 * form, WAITS for the presenter's call (the presenter's method runs on ITS OWN operation queue, which may
 * be another thread), runs its accessor, and finally executes each reacquirer. */
- (BOOL)fnHandshakeForURL:(NSURL *)url
		  writing:(BOOL)writing
	      reacquirers:(struct fn_reacquirers *)reacquirers
{
	NSArray *registered = [NSFileCoordinator filePresenters];
	NSUInteger i;

	for (i = 0; i < [registered count]; i++) {
		id presenter = [registered objectAtIndex:i];
		__block BOOL relinquished = NO;
		__block void (^reacquirer)(void) = NULL;
		void (^relinquish)(void (^)(void)) = NULL;

		if (presenter == _presenter) {
			/* ITS OWN PRESENTER IS NOT TOLD ABOUT ITS OWN OPERATION: "This object is assumed to be
			 * performing the relevant file or directory operations and therefore does not receive
			 * notifications about those operations" (Apple's page for -initWithFilePresenter:). */
			continue;
		}
		if (![presenter respondsToSelector:@selector(presentedItemURL)] ||
		    ![[presenter presentedItemURL] isEqual:url]) {
			continue;	/* only the presenters whose item THIS is */
		}
		/* THE BLOCK TAKES THE MUTEX, SO THE COORDINATOR MUST NOT HOLD IT WHILE IT CALLS THE PRESENTER -
		 * and the first version of this code did exactly that and DEADLOCKED (a non-recursive mutex
		 * locked twice by one thread), which is the failure this slice's probe was written to catch and
		 * the first thing it did catch. The textbook pattern is below: the state is changed and the
		 * condition signalled UNDER the lock, and the waiter checks the state under the same lock. */
		relinquish = ^(void (^presenterReacquirer)(void)) {
			pthread_mutex_lock(&fn_handshake_mutex);
			reacquirer = presenterReacquirer != NULL ? _Block_copy(presenterReacquirer) : NULL;
			relinquished = YES;
			pthread_cond_broadcast(&fn_handshake_cond);
			pthread_mutex_unlock(&fn_handshake_mutex);
		};
		if (writing) {
			if ([presenter respondsToSelector:@selector(relinquishPresentedItemToWriter:)]) {
				[presenter relinquishPresentedItemToWriter:relinquish];
			} else {
				relinquished = YES;
			}
		} else {
			if ([presenter respondsToSelector:@selector(relinquishPresentedItemToReader:)]) {
				[presenter relinquishPresentedItemToReader:relinquish];
			} else {
				relinquished = YES;
			}
		}
		/* THE WAIT, AND IT IS THE PRESENTER'S PROMISE THAT ENDS IT - the same contract Apple's pages
		 * state ("you must execute the block in the [reacquirer] parameter"). A presenter that never
		 * calls it blocks the coordinated operation, which is Apple's semantics rather than a bug here.
		 * A presenter that answers BEFORE we get here is harmless, because the state is read under the
		 * lock and the loop is a re-check rather than a blind wait. */
		pthread_mutex_lock(&fn_handshake_mutex);
		while (!relinquished) {
			pthread_cond_wait(&fn_handshake_cond, &fn_handshake_mutex);
		}
		pthread_mutex_unlock(&fn_handshake_mutex);
		if (reacquirer != NULL) {
			if (reacquirers->count == reacquirers->capacity) {
				size_t grown = reacquirers->capacity > 0 ? reacquirers->capacity * 2 : 4;
				void (^ *moved)(void) = realloc(reacquirers->items, grown * sizeof(void (^)(void)));

				if (moved == NULL) {
					/* NO ROOM TO REMEMBER IT: the presenter is released immediately rather than
					 * leaked, which is the honest failure for an out-of-memory case. */
					_Block_release(reacquirer);
					continue;
				}
				reacquirers->items = moved;
				reacquirers->capacity = grown;
			}
			reacquirers->items[reacquirers->count++] = reacquirer;
		}
	}
	return YES;
}

/* AND THE OTHER END OF IT: each reacquirer runs once the accessor has finished. */
/* AND THE OTHER END OF IT: each reacquirer runs once the accessor has finished, which is what the presenter
 * was promised when it stepped aside - and each is RELEASED the way it was acquired. */
static void fnReacquire(struct fn_reacquirers *reacquirers)
{
	size_t i;

	for (i = 0; i < reacquirers->count; i++) {
		void (^reacquirer)(void) = reacquirers->items[i];

		reacquirer();
		_Block_release(reacquirer);
	}
	free(reacquirers->items);
	reacquirers->items = NULL;
	reacquirers->count = 0;
	reacquirers->capacity = 0;
}

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
	{
		/* THE HAND-SHAKE AROUND THE ACCESSOR: the presenters that hold this item step aside first, the
		 * accessor runs, and their reacquirers are executed afterwards. Apple's page for this door says
		 * exactly this order for a read with no options ("the -relinquishPresentedItemToReader: method
		 * of relevant file presenters is called BEFORE your block executes"). */
		struct fn_reacquirers reacquirers = { NULL, 0, 0 };

		[self fnHandshakeForURL:coordinated writing:NO reacquirers:&reacquirers];
		reader(coordinated);
		fnReacquire(&reacquirers);
	}
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
	{
		struct fn_reacquirers reacquirers = { NULL, 0, 0 };

		[self fnHandshakeForURL:coordinated writing:YES reacquirers:&reacquirers];
		writer(coordinated);
		fnReacquire(&reacquirers);
	}
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
	{
		struct fn_reacquirers reacquirers = { NULL, 0, 0 };

		[self fnHandshakeForURL:reading writing:NO reacquirers:&reacquirers];
		[self fnHandshakeForURL:writing writing:YES reacquirers:&reacquirers];
		readerWriter(reading, writing);
		fnReacquire(&reacquirers);
	}
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
	{
		struct fn_reacquirers reacquirers = { NULL, 0, 0 };

		[self fnHandshakeForURL:first writing:YES reacquirers:&reacquirers];
		[self fnHandshakeForURL:second writing:YES reacquirers:&reacquirers];
		writer(first, second);
		fnReacquire(&reacquirers);
	}
}

@end
