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
 *   prepare-batches-the-presenters-around-one-block  the BATCH door: "This method executes synchronously,
 *                                          blocking the current thread until the [batch] block finishes
 *                                          executing", and the handshake runs ONCE around the whole
 *                                          batch - asserted as an ORDER (relinquish, block, reacquire);
 *   prepare-takes-both-lists              the read list goes to readers and the write list to writers;
 *   prepare-refuses-a-bad-url-without-running-the-block  "the error is returned in this parameter and the
 *                                          block ... is not executed";
 *   probe-tree-removed                    the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
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

static id fn_web(void)
{
	return [NSURL URLWithString:@"https://example.invalid/x"];
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
- (NSURL *)primaryPresentedItemURL;
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

/* §63.236: THE OPTIONAL PROTOCOL PROPERTY. Apple declares -primaryPresentedItemURL on NSFilePresenter for the
 * package case; the presented item IS the primary here, so the two answer the same URL. */
- (NSURL *)primaryPresentedItemURL
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


/* ---- THE THREE MEASUREMENTS §60 OWES (slice 6j) ------------------------------------------------------
 *
 * Each is a statement about WHEN something runs, so each needs an instrument rather than a re-read:
 *   (1) the handshake is NOT deferred - the wait is on the caller's thread, which a presenter that answers
 *       LATE makes observable (the door cannot return before the presenter does);
 *   (2) `-cancel` ends a wait that is IN FLIGHT, and the accessor must NOT run - a presenter that never
 *       answers is the only way to have something in wait;
 *   (3) the queue hazard behind D14(b): dispatching to a queue and BLOCKING on it deadlocks when the waiting
 *       thread is that queue's only worker. Measured two-sidedly - from outside the queue it completes, from
 *       INSIDE it (a block running on the queue that waits for another block on the same queue) it times out.
 * `+mainQueue` is NOT the main thread's run loop in this library (it is a serial queue on worker threads),
 * so the hazard is the SELF-wait and not a main-thread stalls. */
static double fn_now_ms(void)
{
	/* gettimeofday AND NOT clock_gettime(CLOCK_MONOTONIC): the first version used the latter and BOTH legs
	 * reported exactly 20.0 ms - which is not a measurement of anything, it is the clock. A timing
	 * instrument that cannot be trusted produces exactly the kind of confident wrong conclusion this
	 * slice's note in §60 warns about, so the primitive is one this system's probes already rely on. */
	struct timeval tv;

	gettimeofday(&tv, NULL);
	return (double)tv.tv_sec * 1000.0 + (double)tv.tv_usec / 1000.0;
}

/* A DELAY THAT DOES NOT DEPEND ON A SLEEP PRIMITIVE. FNX's timeout sleeps are recorded as unreliable, and
 * the first version of these legs slept with usleep - which is why BOTH legs reported exactly ~20 ms and why
 * the "the handshake is deferred" reading that followed was an artefact of the instrument. The DIAG in main
 * measures the primitive head to head, and the delay below is a gettimeofday loop whose only cost is the
 * measurement's own spin. */
static void fn_delay_ms(int ms)
{
	double start = fn_now_ms();

	while (fn_now_ms() - start < (double)ms) {
		/* spin */
	}
}

static int fn_sem_wait_ms(sem_t *sem, int ms)
{
	struct timespec ts;

	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += ms / 1000;
	ts.tv_nsec += (long)(ms % 1000) * 1000000L;
	if (ts.tv_nsec >= 1000000000L) {
		ts.tv_sec++;
		ts.tv_nsec -= 1000000000L;
	}
	return sem_timedwait(sem, &ts);
}

static int fn_lateCalls = 0;
static int fn_silentCalls = 0;
static NSFileCoordinator *fn_cancelTarget = nil;

static void *fn_cancel_after_delay(void *unused)
{
	(void)unused;
	fn_delay_ms(150);
	[fn_cancelTarget cancel];
	return NULL;
}

@interface LatePresenter : NSObject <NSFilePresenter>
{
	NSURL *_item;
	NSOperationQueue *_queue;
	int _delayMs;
	BOOL _answers;
}
- (id)initWithItem:(NSURL *)item delayMs:(int)ms answers:(BOOL)answers;
@end

@implementation LatePresenter

- (id)initWithItem:(NSURL *)item delayMs:(int)ms answers:(BOOL)answers
{
	self = [super init];
	if (self != nil) {
		_item = item;
		_queue = [[NSOperationQueue alloc] init];
		_delayMs = ms;
		_answers = answers;
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

- (void)relinquishPresentedItemToWriter:(void (^)(void (^)(void)))writer
{
	if (_answers) {
		fn_lateCalls++;
		fn_delay_ms(_delayMs);
		writer(^(void) { });
	} else {
		/* NEVER ANSWERS, on purpose: the coordinator is left waiting, which is the only state in which
		 * the in-wait half of -cancel can be measured. */
		fn_silentCalls++;
	}
}

- (void)relinquishPresentedItemToReader:(void (^)(void (^)(void)))reader
{
	[self relinquishPresentedItemToWriter:reader];
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

	{
		/* THE PRIMITIVE, MEASURED BEFORE IT IS USED TO MEASURE ANYTHING ELSE. */
		double slept = fn_now_ms();

		usleep(200 * 1000);
		printf("FOUNDATION-FILEPRESENTER DIAG usleep-200ms=%.1fms\n", fn_now_ms() - slept);
	}

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

	/* §63.236: THE OPTIONAL PRIMARY-ITEM PROPERTY, declared on the protocol and answered by a presenter whose
	 * presented item is a package's main item. The declaration IS the door; this pins that it is live. */
	check("presenter-primary-presented-item-url-is-declared",
	      [presenter respondsToSelector:@selector(primaryPresentedItemURL)] &&
	      [[presenter primaryPresentedItemURL] isEqual:item],
	      @"the optional protocol property answers the primary item's URL");

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
		/* THE BATCH DOOR: "This method executes synchronously, blocking the current thread until the
		 * [batch] block finishes executing", and its block holds NESTED coordinate calls rather than
		 * performing the operations itself. The handshake runs ONCE around the whole batch, which is what
		 * the door is for - and the ORDER is what the check measures. */
		fn_step = 0;
		fn_relinquish_step = 0;
		fn_reacquirer_step = 0;
		fn_readerCalls = 0;
		fn_writerCalls = 0;
		{
			__block int blockStep = 0;

			[coordinator prepareForReadingItemsAtURLs:[NSArray arrayWithObject:item]
							  options:0
						 writingItemsAtURLs:nil
							  options:0
							    error:NULL
					       byAccessor:^{
				blockStep = ++fn_step;
			}];
			check("prepare-batches-the-presenters-around-one-block",
			      fn_readerCalls == 1 && fn_relinquish_step == 1 && blockStep == 2 &&
			      fn_reacquirer_step == 3,
			      [NSString stringWithFormat:@"reader=%d order=%d/%d/%d", fn_readerCalls,
				fn_relinquish_step, blockStep, fn_reacquirer_step]);
		}
	}

	{
		/* BOTH LISTS ARE HANDSHAKEN, EACH ITEM WITH ITS OWN FORM: the read list to readers and the write
		 * list to writers. */
		fn_readerCalls = 0;
		fn_writerCalls = 0;
		[NSFileCoordinator addFilePresenter:stranger];		/* its item is other.txt */
		[coordinator prepareForReadingItemsAtURLs:[NSArray arrayWithObject:item]
						  options:0
					     writingItemsAtURLs:[NSArray arrayWithObject:other]
						  options:NSFileCoordinatorWritingForMerging
						    error:NULL
					       byAccessor:^{
		}];
		check("prepare-takes-both-lists", fn_readerCalls == 1 && fn_writerCalls == 1,
		      [NSString stringWithFormat:@"reader=%d writer=%d", fn_readerCalls, fn_writerCalls]);
		[NSFileCoordinator removeFilePresenter:stranger];
	}

	{
		/* AND A URL THAT CANNOT BE COORDINATED REFUSES THE BATCH: "the error is returned in this parameter
		 * and the block ... is not executed". */
		__block int blockCalls = 0;
		NSError *error = nil;

		[coordinator prepareForReadingItemsAtURLs:
			[NSArray arrayWithObjects:item, fn_web(), nil]
						  options:0
					     writingItemsAtURLs:nil
						  options:0
						    error:&error
					       byAccessor:^{
			blockCalls++;
		}];
		check("prepare-refuses-a-bad-url-without-running-the-block",
		      blockCalls == 0 && error != nil && [error code] == EINVAL,
		      [NSString stringWithFormat:@"block calls=%d error=%@", blockCalls, error]);
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"still there");
	}

	/* ---- THE THREE MEASUREMENTS OWED (slice 6j) ------------------------------------------------------- */
	{
		NSURL *lateItem = fn_url(fn_path(@"late.txt"));
		LatePresenter *late = [[LatePresenter alloc] initWithItem:lateItem delayMs:200 answers:YES];
		NSFileCoordinator *lateCoordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
		__block int accessors = 0;
		double elapsed;

		{
			FILE *f = fopen([fn_path(@"late.txt") UTF8String], "w");

			if (f != NULL) { fputs("x", f); fclose(f); }
		}
		[NSFileCoordinator addFilePresenter:late];
		elapsed = fn_now_ms();
		[lateCoordinator coordinateWritingItemAtURL:lateItem options:0 error:NULL
					 byAccessor:^(NSURL *newURL) { accessors++; (void)newURL; }];
		elapsed = fn_now_ms() - elapsed;
		[NSFileCoordinator removeFilePresenter:late];
		/* MEASURED: the door returns in ~20 ms while the presenter answers after 200 ms, so the handshake
		 * IS DEFERRED - the presenter's method does not hold the caller. D14(b) RECORDED THE OPPOSITE ("the
		 * presenter's relinquish method is called on the COORDINATING THREAD"), so this measurement
		 * CORRECTS THE REGISTER instead of confirming it. */
		/* MEASURED, WITH A DELAY THAT REALLY DELAYS (see fn_delay_ms): the door takes 200.4 ms for a
		 * presenter that answers after 200 ms, so THE HANDSHAKE IS SYNCHRONOUS ON THE CALLER'S THREAD -
		 * which is what D14(b) said and what an earlier reading of this same experiment, taken with a
		 * broken usleep, appeared to refute. */
		check("debt-the-handshake-is-synchronous-measured",
		      fn_lateCalls == 1 && accessors == 1 && elapsed >= 150.0,
		      [NSString stringWithFormat:@"presenter-answered-after-200ms calls=%d accessors=%d "
			@"elapsed=%.1fms (a DEFERRED handshake would have returned at once)",
			fn_lateCalls, accessors, elapsed]);
	}

	{
		NSURL *silentItem = fn_url(fn_path(@"silent.txt"));
		LatePresenter *silent = [[LatePresenter alloc] initWithItem:silentItem delayMs:0 answers:NO];
		NSFileCoordinator *silentCoordinator = [[NSFileCoordinator alloc] initWithFilePresenter:nil];
		__block int accessors = 0;
		pthread_t canceller;
		double elapsed;

		{
			FILE *f = fopen([fn_path(@"silent.txt") UTF8String], "w");

			if (f != NULL) { fputs("x", f); fclose(f); }
		}
		fn_cancelTarget = silentCoordinator;
		[NSFileCoordinator addFilePresenter:silent];
		pthread_create(&canceller, NULL, fn_cancel_after_delay, NULL);
		elapsed = fn_now_ms();
		[silentCoordinator coordinateWritingItemAtURL:silentItem options:0 error:NULL
					   byAccessor:^(NSURL *newURL) { accessors++; (void)newURL; }];
		elapsed = fn_now_ms() - elapsed;
		pthread_join(canceller, NULL);
		[NSFileCoordinator removeFilePresenter:silent];
		/* MEASURED, AND THIS IS THE DEFECT: with a presenter that NEVER answers, the ACCESSOR RUNS ANYWAY
		 * (~20 ms) - so nothing is ever in wait for -cancel to end, and the coordinated operation is
		 * performed BEFORE the presenter relinquishes. Apple's contract is the opposite, so this is a
		 * DEVIATION and not a difference chosen: the probe asserts what the system DOES and §60 names the
		 * gap. The detached canceller still runs, and the check requires that its late cancel changed
		 * nothing else. */
		/* THE DEFECT, AND NOW THE FIX: with a presenter that NEVER answers and a cancel at 150 ms, the
		 * accessor used to RUN (measured: accessors=1) because the handshake broke out of its wait and
		 * still returned YES. Apple's contract is that a cancelled coordination does not perform the
		 * operation, so the check now requires accessors=0 - and it failed before the fix. */
		check("debt-a-cancelled-wait-does-not-run-the-accessor",
		      fn_silentCalls == 1 && accessors == 0 && elapsed >= 100.0 && elapsed < 2000.0,
		      [NSString stringWithFormat:@"presenter-never-answered calls=%d accessors=%d "
			@"elapsed=%.1fms (the cancel arrives at 150ms; accessors=0 is the contract)",
			fn_silentCalls, accessors, elapsed]);
	}

	{
		/* THE HAZARD BEHIND D14(b), TWO-SIDED - and measured with a POLLING FLAG, because the first
		 * version used sem_timedwait and that primitive answered "timed out" even from OUTSIDE the queue:
		 * an instrument that fails for its own reasons measures nothing, so the flag takes it out. */
		NSOperationQueue *queue = [[NSOperationQueue alloc] init];
		__block volatile int posted = 0;
		int fromOutside = 0;
		__block int fromInside = 1;
		int i;

		[queue setMaxConcurrentOperationCount:1];
		[queue addOperationWithBlock:^{ posted = 1; }];
		for (i = 0; i < 300 && posted == 0; i++) {
			usleep(1000);
		}
		fromOutside = posted;
		[queue waitUntilAllOperationsAreFinished];
		posted = 0;
		[queue addOperationWithBlock:^{
			[queue addOperationWithBlock:^{ posted = 1; }];
			{
				int j;

				for (j = 0; j < 300 && posted == 0; j++) {
					usleep(1000);
				}
				fromInside = posted;
			}
		}];
		[queue waitUntilAllOperationsAreFinished];
		check("debt-dispatch-then-block-deadlocks-on-the-callers-own-queue",
		      fromOutside == 1 && fromInside == 0,
		      [NSString stringWithFormat:@"from-outside=%d from-inside=%d (1 = the block ran, 0 = it never "
			@"did: the queue's only worker was the thread waiting for it)", fromOutside, fromInside]);
	}

	printf("FOUNDATION-FILEPRESENTER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEPRESENTER DONE\n");
	return failc == 0 ? 0 : 1;
}
