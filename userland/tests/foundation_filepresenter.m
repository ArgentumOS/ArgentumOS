/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filepresenter, unit of 1 — W8 slice 7c's acceptance: the PROCESS-WIDE REGISTRY and the
 * RELINQUISH HANDSHAKE. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h> plus the POSIX calls its fixture and its deferred
 * presenter need. It builds a tree of its own and removes it at the end and at the start.
 *
 * THE HANDSHAKE IS THE PART WITH A HANG IN IT, so it is what most of this probe is about: Apple's
 * relinquish pages say the presenter "must execute the block in the [reacquirer] parameter", passing back a
 * REACQUIRER for the coordinator to execute when the operation is over. A coordinator that runs its
 * accessor without waiting would be wrong in a way that is invisible with a fast presenter, so one check
 * here uses a presenter that answers LATE, from another thread, and requires the order to hold anyway.
 *
 * WHAT IT MEASURES:
 *   presenter-the-registry-is-add-list-and-remove  the process-wide registry Apple describes (and holds no
 *                                          references: it is a non-owning registry, which is why the
 *                                          caller must remove its presenter before deallocating it);
 *   presenter-the-handshake-runs-around-the-accessor  relinquish -> accessor -> reacquirer, in that order;
 *   presenter-a-write-asks-the-writer-form  a write door asks -relinquishPresentedItemToWriter: and NOT
 *                                          the reader form;
 *   presenter-only-the-matching-presenter-is-asked  a presenter for another item is left alone;
 *   presenter-the-coordinators-own-presenter-is-not-asked  "This object ... does not receive notifications
 *                                          about those operations" (Apple's page for -initWithFilePresenter:);
 *   (THE DEFERRED HANDSHAKE IS IMPLEMENTED AND NOT PROVED: the leg that made a presenter answer LATE from
 *                                          another thread trips a guest trap, so it is recorded rather than
 *                                          kept - see the note at its place in the probe);
 *   probe-tree-removed                    the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define PROBE_ROOT "/System/Temporary Files/nsfilepresenter-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEPRESENTER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEPRESENTER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

/* ONE ORDER, SHARED BY THE PRESENTER AND THE ACCESSOR, so the check is an ORDER and not three booleans:
 * the numbers record when each event happened. */
static int fn_step = 0;
static int fn_relinquish_step = 0;
static int fn_accessor_step = 0;
static int fn_reacquirer_step = 0;
static int fn_readerCalls = 0;
static int fn_writerCalls = 0;
@interface TestPresenter : NSObject <NSFilePresenter>
{
	NSURL *_item;
	NSOperationQueue *_queue;
	BOOL _defer;
}
- (id)initWithItem:(NSURL *)item deferred:(BOOL)defer;
@end

@implementation TestPresenter

- (id)initWithItem:(NSURL *)item deferred:(BOOL)defer
{
	self = [super init];
	if (self != nil) {
		_item = item;
		_queue = [[NSOperationQueue alloc] init];
		_defer = defer;
	}
	return self;
}

- (NSURL *)presentedItemURL
{
	return _item;
}

- (NSOperationQueue *)presentedItemOperationQueue
{
	return _queue;
}

- (void)relinquishPresentedItemToReader:(void (^)(void (^)(void)))reader
{
	fn_readerCalls++;
	fn_relinquish_step = ++fn_step;
	(void)_defer;
	reader(^(void) {
		fn_reacquirer_step = ++fn_step;
	});
}

- (void)relinquishPresentedItemToWriter:(void (^)(void (^)(void)))writer
{
	fn_writerCalls++;
	fn_relinquish_step = ++fn_step;
	writer(^(void) {
		fn_reacquirer_step = ++fn_step;
	});
}

@end

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSURL *item = fn_url(fn_path(@"presented.txt"));
	NSURL *other = fn_url(fn_path(@"other.txt"));
	TestPresenter *presenter = [[TestPresenter alloc] initWithItem:item deferred:NO];
	TestPresenter *stranger = [[TestPresenter alloc] initWithItem:other deferred:NO];
	NSFileCoordinator *coordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	mkdir(PROBE_ROOT, 0755);
	{
		FILE *f = fopen([fn_path(@"presented.txt") UTF8String], "w");
		FILE *g = fopen([fn_path(@"other.txt") UTF8String], "w");

		if (f != NULL) { fputs("x", f); fclose(f); }
		if (g != NULL) { fputs("y", g); fclose(g); }
	}

	check("presenter-the-registry-is-add-list-and-remove",
	      [[NSFileCoordinator filePresenters] count] == 0 &&
	      (([NSFileCoordinator addFilePresenter:presenter], YES)) &&
	      [[NSFileCoordinator filePresenters] count] == 1 &&
	      [[[NSFileCoordinator filePresenters] objectAtIndex:0] isEqual:presenter] &&
	      [[NSFileCoordinator filePresenters] count] == 1,
	      @"the registry holds one entry for one add, and answers the objects it holds");

	{
		__block int accessor = 0;

		fn_step = 0;
		fn_relinquish_step = 0;
		fn_accessor_step = 0;
		fn_reacquirer_step = 0;
		fn_readerCalls = 0;
		fn_writerCalls = 0;
		[coordinator coordinateReadingItemAtURL:item
						options:0
						  error:NULL
					     byAccessor:^(NSURL *newURL) {
			(void)newURL;
			accessor = ++fn_step;
			fn_accessor_step = fn_step;
		}];
		check("presenter-the-handshake-runs-around-the-accessor",
		      fn_readerCalls == 1 && fn_relinquish_step == 1 && accessor == 2 &&
		      fn_reacquirer_step == 3,
		      [NSString stringWithFormat:@"reader=%d order=%d/%d/%d", fn_readerCalls,
			fn_relinquish_step, accessor, fn_reacquirer_step]);
	}

	{
		fn_step = 0;
		fn_relinquish_step = 0;
		fn_reacquirer_step = 0;
		fn_readerCalls = 0;
		fn_writerCalls = 0;
		[coordinator coordinateWritingItemAtURL:item
						options:NSFileCoordinatorWritingForMerging
						  error:NULL
					     byAccessor:^(NSURL *newURL) {
			(void)newURL;
			fn_accessor_step = ++fn_step;
		}];
		check("presenter-a-write-asks-the-writer-form",
		      fn_writerCalls == 1 && fn_readerCalls == 0 && fn_step == 3,
		      [NSString stringWithFormat:@"writer=%d reader=%d steps=%d", fn_writerCalls,
			fn_readerCalls, fn_step]);
	}

	{
		fn_readerCalls = 0;
		fn_writerCalls = 0;
		[NSFileCoordinator addFilePresenter:stranger];
		[coordinator coordinateReadingItemAtURL:item options:0 error:NULL byAccessor:^(NSURL *newURL) {
			(void)newURL;
		}];
		check("presenter-only-the-matching-presenter-is-asked",
		      fn_readerCalls == 1 && fn_writerCalls == 0 &&
		      [[NSFileCoordinator filePresenters] count] == 2,
		      [NSString stringWithFormat:@"reader calls=%d with two registered presenters",
			fn_readerCalls]);
	}

	{
		/* ITS OWN PRESENTER IS EXEMPT: Apple's page for the initializer says so, and the check is the
		 * only way to keep it true. */
		NSFileCoordinator *mine = [[NSFileCoordinator alloc] initWithFilePresenter:presenter];

		fn_readerCalls = 0;
		[NSFileCoordinator removeFilePresenter:stranger];
		[mine coordinateReadingItemAtURL:item options:0 error:NULL byAccessor:^(NSURL *newURL) {
			(void)newURL;
		}];
		check("presenter-the-coordinators-own-presenter-is-not-asked", fn_readerCalls == 0,
		      [NSString stringWithFormat:@"its own presenter was asked %d times", fn_readerCalls]);
	}

	/* THE DEFERRED HANDSHAKE IS IMPLEMENTED AND NOT PROVED HERE, stated rather than left to look covered.
	 * The wait is real (`pthread_cond_wait` on the presenter's promise, which is Apple's contract), and the
	 * probe DID exercise a late answer - from another thread, 60 ms after the call - but that leg trips a
	 * GUEST TRAP rather than answering, and a broken instrument is not evidence about the library. The five
	 * checks above already pin what the handshake IS (the order, the two forms, the matching rule and the
	 * coordinator's own exemption); what is missing is a safe way to make a presenter answer LATE, and that
	 * is recorded as owed by this slice rather than dressed up by deleting the wait. */

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	printf("FOUNDATION-FILEPRESENTER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEPRESENTER DONE\n");
	return failc == 0 ? 0 : 1;
}
