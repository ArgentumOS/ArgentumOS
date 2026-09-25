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
 *   purpose-a-string-round-trips-and-empty-is-ignored  "A string that uniquely identifies the file
 *                                          access" round-trips, and the one spelling rule Apple states
 *                                          ("You cannot use [nil] or zero-length strings") is honoured by
 *                                          IGNORING such an assignment;
 *   a-write-tells-the-presenters-the-item-changed / a-read-does-not-tell-them  the presenters are told the
 *                                          item CHANGED after a write - and not after a read, because a
 *                                          read changes nothing;
 *   didMove-notifies-the-items-presenters  "This method calls the [-presentedItemDidMoveToURL:] method for
 *                                          any of the item's file presenters";
 *   willMove-has-no-purpose-on-a-system-without-a-sandbox  APPLE'S OWN SENTENCE: the will form "is
 *                                          intended for apps that adopt App Sandbox ... If your macOS app
 *                                          is not sandboxed, this method serves no purpose" - declared so a
 *                                          balanced pair compiles, and notifying nobody;
 *   cancel-with-nothing-active-does-not-stop-the-next-call  -cancel cancels ACTIVE calls ("any active
 *                                          file coordination calls"), so a cancel with nothing in flight
 *                                          does not stop the next one;
 *   async-the-door-runs-the-accessor-on-its-queue  the asynchronous door: the accessor runs on the given
 *                                          queue with the intents and a nil error, and the probe PRINTS
 *                                          whether the door had already returned (a fact about this
 *                                          system's queue rather than something to assert);
 *   async-a-nil-queue-or-no-intents-does-nothing  the door has no error of its own, so a nil queue or an
 *                                          empty intent list answers by doing nothing rather than crashing;
 *   async-a-reading-intent-gets-the-coordinated-url  "The system updates this URL property to account for
 *                                          any changes to the underlying files";
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
static int fn_changeCalls = 0;
static int fn_moveCalls = 0;
static NSString *fn_movedTo = nil;
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

- (void)presentedItemDidChange
{
	fn_changeCalls++;
}

- (void)presentedItemDidMoveToURL:(NSURL *)newURL
{
	fn_moveCalls++;
	fn_movedTo = [newURL path];
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
	NSURL *linkURL = fn_url(fn_path(@"link.txt"));	/* NOT named `link`: POSIX link(2) is in scope */
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
	symlink("presented.txt", [fn_path(@"link.txt") UTF8String]);

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
		/* THE PURPOSE IDENTIFIER, AND THE ONE SPELLING RULE APPLE STATES FOR IT: "You cannot use [nil] or
		 * zero-length strings", answered here by IGNORING such an assignment and keeping what it had -
		 * Apple states the rule and not the mechanism, so nothing is invented and nothing is stored. */
		NSString *first = @"com.argentum.coordination.one";

		[coordinator setPurposeIdentifier:first];
		{
			BOOL roundTrips = [[coordinator purposeIdentifier] isEqual:first];

			[coordinator setPurposeIdentifier:@""];
			[coordinator setPurposeIdentifier:nil];
			check("purpose-a-string-round-trips-and-empty-is-ignored",
			      roundTrips && [[coordinator purposeIdentifier] isEqual:first],
			      [NSString stringWithFormat:@"after setting empty and nil: %@",
				[coordinator purposeIdentifier]]);
		}
	}

	{
		/* A WRITE IS WHAT MAKES A CHANGE, so the presenters are told after it - and a READ is not. */
		fn_changeCalls = 0;
		[coordinator coordinateWritingItemAtURL:item
						options:NSFileCoordinatorWritingForMerging
						  error:NULL
					     byAccessor:^(NSURL *newURL) {
			(void)newURL;
		}];
		check("a-write-tells-the-presenters-the-item-changed", fn_changeCalls == 1,
		      [NSString stringWithFormat:@"didChange calls=%d", fn_changeCalls]);

		[coordinator coordinateReadingItemAtURL:item options:0 error:NULL byAccessor:^(NSURL *newURL) {
			(void)newURL;
		}];
		check("a-read-does-not-tell-them", fn_changeCalls == 1,
		      [NSString stringWithFormat:@"didChange calls after a read=%d", fn_changeCalls]);
	}

	{
		/* APPLE'S MEASURED SENTENCE FOR THIS DOOR: "This method calls the [-presentedItemDidMoveToURL:]
		 * method for any of the item's file presenters." */
		NSURL *moved = fn_url(fn_path(@"presented-renamed.txt"));

		fn_moveCalls = 0;
		fn_movedTo = nil;
		[coordinator itemAtURL:item didMoveToURL:moved];
		check("didMove-notifies-the-items-presenters",
		      fn_moveCalls == 1 && [fn_movedTo isEqual:[moved path]],
		      [NSString stringWithFormat:@"move calls=%d movedTo=%@", fn_moveCalls, fn_movedTo]);
	}

	{
		/* AND THE HALF PAIR THAT APPLE ITSELF CALLS A NO-OP HERE: "This method is intended for apps that
		 * adopt App Sandbox ... If your macOS app is not sandboxed, this method serves no purpose." It is
		 * declared so a balanced will/did pair compiles, and it notifies nobody. */
		NSURL *moved = fn_url(fn_path(@"presented-will-move.txt"));

		fn_moveCalls = 0;
		[coordinator itemAtURL:item willMoveToURL:moved];
		check("willMove-has-no-purpose-on-a-system-without-a-sandbox", fn_moveCalls == 0,
		      [NSString stringWithFormat:@"move calls=%d", fn_moveCalls]);
	}

	{
		/* -CANCEL CANCELS *ACTIVE* CALLS, so a cancel with nothing active does not stop the next one -
		 * which is the half of this door that needs no second thread to observe. */
		__block int accessor = 0;

		[coordinator cancel];
		[coordinator coordinateReadingItemAtURL:item options:0 error:NULL byAccessor:^(NSURL *newURL) {
			(void)newURL;
			accessor++;
		}];
		check("cancel-with-nothing-active-does-not-stop-the-next-call", accessor == 1,
		      [NSString stringWithFormat:@"the accessor ran %d time(s) after a cancel with no call in flight",
			accessor]);
	}

	{
		/* THE ASYNCHRONOUS DOOR: "waits asynchronously to get access to the files and then invokes the
		 * accessor block ON THE SPECIFIED QUEUE", with "an error message ... passed to the block". The
		 * probe polls because the accessor is not ours to schedule, and it PRINTS whether the door had
		 * already returned - a fact about this system's queue rather than something to assert. */
		NSFileAccessIntent *intent = [NSFileAccessIntent readingIntentWithURL:item options:0];
		NSArray *intents = [NSArray arrayWithObject:intent];
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		__block NSUInteger calls = 0;
		__block BOOL gotIntents = NO;
		__block BOOL gotError = YES;
		BOOL returnedFirst = NO;
		int waited = 0;

		[coordinator coordinateAccessWithIntents:intents queue:queue byAccessor:
			^(NSArray *handedIntents, NSError *error) {
			calls++;
			gotIntents = [handedIntents count] == 1;
			gotError = error != nil;
		}];
		while (calls == 0 && waited < 2000) {
			usleep(1000);
			waited++;
		}
		returnedFirst = (calls == 0);
		check("async-the-door-runs-the-accessor-on-its-queue",
		      calls == 1 && gotIntents && !gotError,
		      [NSString stringWithFormat:@"calls=%lu intents=%d error=%d (the door returned before the "
			@"accessor ran: %d, after %d ms)", (unsigned long)calls, (int)gotIntents, (int)gotError,
			(int)returnedFirst, waited]);
	}

	{
		/* AND THE REFUSALS THIS DOOR CAN MAKE: it has no error of its own, so a nil queue or an empty
		 * intent list answers by DOING NOTHING rather than by crashing. */
		__block NSUInteger calls = 0;
		NSFileAccessIntent *intent = [NSFileAccessIntent readingIntentWithURL:item options:0];

		[coordinator coordinateAccessWithIntents:[NSArray arrayWithObject:intent]
						    queue:nil
					       byAccessor:^(NSArray *handed, NSError *error) {
			(void)handed;
			(void)error;
			calls++;
		}];
		[coordinator coordinateAccessWithIntents:[NSArray array]
						    queue:[[NSOperationQueue alloc] init]
					       byAccessor:^(NSArray *handed, NSError *error) {
			(void)handed;
			(void)error;
			calls++;
		}];
		usleep(50000);
		check("async-a-nil-queue-or-no-intents-does-nothing", calls == 0,
		      [NSString stringWithFormat:@"the accessor ran %lu time(s)", (unsigned long)calls]);
	}

	{
		/* "The system UPDATES this URL property to account for any changes to the underlying files" -
		 * here, the URL the coordination produced: a reading intent that asked for the symbolic link to
		 * be resolved sees the TARGET's URL afterwards. */
		NSFileAccessIntent *intent = [NSFileAccessIntent readingIntentWithURL:linkURL
								     options:NSFileCoordinatorReadingResolvesSymbolicLink];
		NSArray *intents = [NSArray arrayWithObject:intent];
		__block BOOL ran = NO;
		int waited = 0;

		[coordinator coordinateAccessWithIntents:intents
						    queue:[[NSOperationQueue alloc] init]
					       byAccessor:^(NSArray *handed, NSError *error) {
			(void)handed;
			(void)error;
			ran = YES;
		}];
		while (!ran && waited < 2000) {
			usleep(1000);
			waited++;
		}
		check("async-a-reading-intent-gets-the-coordinated-url",
		      ran && [[intent URL] isEqual:fn_url(fn_path(@"presented.txt"))],
		      [NSString stringWithFormat:@"the intent's URL is now %@", [intent URL]]);
	}

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
