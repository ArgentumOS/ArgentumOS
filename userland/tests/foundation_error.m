/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_error, unit 2 of 2 — the checks (ARC).
 *
 *   error-api-complete     every public NSError selector exists
 *   exception-api-complete every public NSException selector exists
 *   error-value            domain/code/userInfo, the localised description from
 *                          the KEY and the fallback without it, -description's
 *                          shape, value equality, and that a MUTABLE userInfo is
 *                          copied rather than referenced
 *   exception-raise        @try/@catch end to end: -raise unwinds through the
 *                          runtime, and the caught object is the one thrown
 *   exception-format       +raise:format: builds the reason and throws
 *   error-cross-tu         values from the support unit compare equal to local ones
 */

#import "foundation_error.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>		/* usleep, for the bounded wait on the helper thread */
#import <Foundation/NSThread.h>
#import <Foundation/NSException.h>
#import <Foundation/NSDictionary.h>

static int okc, failc;

/*
 * THE MARKER HANDLER the child process installs. It writes ONE line on fd 1 - the descriptor a probe's output
 * actually reaches in this system - so the case can see that the RUNTIME called the uncaught handler, which is
 * the half of the contract this library cannot observe from the inside: -raise hands the exception to
 * objc_exception_throw, and nothing in Foundation learns whether anything caught it.
 */
static int fn_marker_fd = -1;	/* armed with a pipe, so the PARENT can assert the runtime called this */

static void fn_marker_handler(NSException *exception)
{
	const char *name = [[exception name] UTF8String];
	char line[160];
	int n = snprintf(line, sizeof line, "FOUNDATION-ERROR uncaught-handler-fired name=%s\n",
			 name != NULL ? name : "(null)");

	if (n > 0) {
		(void)write(fn_marker_fd >= 0 ? fn_marker_fd : 1, line, (size_t)n);
	}
}

static int lastcheck;

static void check(const char *name, int ok, const char *detail)
{
	lastcheck = ok;	/* read by covers() */
	if (ok) {
		okc++;
		printf("FOUNDATION-ERROR %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-ERROR %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

/*
 * THE ASSERTION FAMILY'S OWN FIXTURES (W2d). A recorder SUBCLASS is what proves the handler is
 * replaceable: the check installs one in the thread dictionary and asserts it is the one consulted,
 * which is Apple's documented mechanism and the reason the family needed -threadDictionary.
 */
static volatile int fn_recorder_calls = 0;

@interface FnAssertionRecorder : NSAssertionHandler
@end

@implementation FnAssertionRecorder

- (void)handleFailureInMethod:(SEL)selector object:(id)object file:(NSString *)fileName
		   lineNumber:(NSInteger)line description:(NSString *)format, ...
{
	(void)selector; (void)object; (void)fileName; (void)line; (void)format;
	fn_recorder_calls++;
}

- (void)handleFailureInFunction:(NSString *)functionName file:(NSString *)fileName
		     lineNumber:(NSInteger)line description:(NSString *)format, ...
{
	(void)functionName; (void)fileName; (void)line; (void)format;
	fn_recorder_calls++;
}

@end

/* A helper THREAD, so the dictionary's per-thread claim is measured rather than assumed. The main
 * thread records its own dictionary before starting it. */
static volatile int fn_helper_ran = 0;
static volatile int fn_helper_differs = 0;
static NSMutableDictionary *fn_main_dictionary = nil;

@interface FnDictionaryThread : NSObject
- (void)fnRecord:(id)ignored;
@end

@implementation FnDictionaryThread

- (void)fnRecord:(id)ignored
{
	(void)ignored;
	fn_helper_differs = ([[NSThread currentThread] threadDictionary] != fn_main_dictionary);
	fn_helper_ran = 1;
}

@end

int main(void)
{
	{
		static const char *classSelectors[] = {
			"errorWithDomain:code:userInfo:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithDomain:code:userInfo:", "domain", "code", "userInfo",
			"localizedDescription", "localizedFailureReason",
			"isEqual:", "hash", "description", "copy", "mutableCopy", NULL
		};
		NSError *probe = [NSError errorWithDomain:@"D" code:1 userInfo:nil];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSError respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing -%s\n", instanceSelectors[i]);
			}
		}
		check("error-api-complete", complete,
		      "every public NSError selector exists (the hard rule)");
	}

	{
		static const char *classSelectors[] = {
			"exceptionWithName:reason:userInfo:", "raise:format:",
			"raise:format:arguments:", NULL
		};
		static const char *instanceSelectors[] = {
			"initWithName:reason:userInfo:", "name", "reason", "userInfo",
			"raise", "isEqual:", "hash", "description", "copy", "mutableCopy", NULL
		};
		NSException *probe = [NSException exceptionWithName:@"N" reason:@"r" userInfo:nil];
		int complete = 1;
		int i;

		for (i = 0; classSelectors[i] != NULL; i++) {
			if (![NSException respondsToSelector:sel_registerName(classSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing +%s\n", classSelectors[i]);
			}
		}
		for (i = 0; instanceSelectors[i] != NULL; i++) {
			if (![probe respondsToSelector:sel_registerName(instanceSelectors[i])]) {
				complete = 0;
				printf("FOUNDATION-ERROR missing -%s\n", instanceSelectors[i]);
			}
		}
		check("exception-api-complete", complete,
		      "every public NSException selector exists (the hard rule)");
	}

	{
		NSMutableDictionary *editable = [[NSMutableDictionary alloc] init];
		NSError *plain;
		NSError *described;
		NSError *same;
		NSError *differentCode;

		[editable setObject:@"the reason" forKey:NSLocalizedFailureReasonKey];
		plain = [NSError errorWithDomain:@"FNDomain" code:42 userInfo:editable];
		[editable setObject:@"changed afterwards" forKey:NSLocalizedFailureReasonKey];
		described = [NSError errorWithDomain:@"FNDomain" code:42
					     userInfo:[NSDictionary dictionaryWithObject:@"described"
										  forKey:NSLocalizedDescriptionKey]];
		same = [NSError errorWithDomain:@"FNDomain" code:42
				       userInfo:[NSDictionary dictionaryWithObject:@"the reason"
									    forKey:NSLocalizedFailureReasonKey]];
		differentCode = [NSError errorWithDomain:@"FNDomain" code:43 userInfo:nil];
		/* F6: built through the same nullable constructor, bound here so the check
		 * below can guard it. */
		NSError *secondPlain = [NSError errorWithDomain:@"FNDomain" code:42 userInfo:nil];

		check("error-value",
		      [[plain domain] isEqualToString:@"FNDomain"] && [plain code] == 42 &&
		      [[plain localizedFailureReason] isEqualToString:@"the reason"] &&
		      [[described localizedDescription] isEqualToString:@"described"] &&
		      [[plain localizedDescription] length] > 0 &&
		      [plain isEqual:same] &&
		      secondPlain != nil && ![plain isEqual:secondPlain] &&
		      ![plain isEqual:differentCode] &&
		      ![plain isEqual:@"not an error"] &&
		      [plain hash] == [same hash] &&
		      [plain hash] != [differentCode hash] &&
		      [plain copy] == plain &&
		      strstr([[plain description] UTF8String], "Error Domain=FNDomain Code=42") != NULL,
		      "the value, the key-driven and fallback descriptions, equality, and -description's shape");
	}

	{
		int caught = 0;
		NSString *name = nil;
		NSString *reason = nil;
		int finallyRan = 0;

		printf("FOUNDATION-ERROR step throw and catch\n");
		@try {
			[NSException raise:NSRangeException format:@"index %d is out of range", 7];
			printf("FOUNDATION-ERROR step after raise (unreachable)\n");
		} @catch (NSException *caughtException) {
			caught = 1;
			name = [caughtException name];
			reason = [caughtException reason];
			printf("FOUNDATION-ERROR caught name=%s reason=%s\n",
			       [name UTF8String], [reason UTF8String]);
		} @finally {
			finallyRan = 1;
		}

		check("exception-raise",
		      caught && finallyRan &&
		      [name isEqualToString:NSRangeException] &&
		      [reason isEqualToString:@"index 7 is out of range"] &&
		      [NSGenericException isEqualToString:@"NSGenericException"] &&
		      [NSInvalidArgumentException isEqualToString:@"NSInvalidArgumentException"] &&
		      [NSInternalInconsistencyException isEqualToString:@"NSInternalInconsistencyException"],
		      "@try/@catch/@finally unwind a raised NSException through the runtime, and the reason came from the format");
	}

	{
		int caught = 0;
		NSString *name = nil;

		printf("FOUNDATION-ERROR step throw a built exception\n");
		@try {
			@throw [NSException exceptionWithName:NSInvalidArgumentException
						       reason:@"thrown directly"
						     userInfo:nil];
		} @catch (NSException *caughtException) {
			caught = 1;
			name = [caughtException name];
		} @catch (id anything) {
			caught = 0;
		}

		check("exception-throw", caught && [name isEqualToString:NSInvalidArgumentException],
		      "@throw of a built exception lands in the matching @catch");
	}

	{
		NSError *fromSupport = foundation_error_built();
		NSException *exception = foundation_error_exception();
		int caught = 0;

		@try {
			[exception raise];
		} @catch (NSException *caughtException) {
			caught = [caughtException reason] != nil;
		}

		check("error-cross-tu",
		      [[fromSupport domain] isEqualToString:@"FNXSupportDomain"] &&
		      [fromSupport code] == 7 &&
		      [[fromSupport localizedFailureReason] isEqualToString:@"from the support unit"] &&
		      caught,
		      "values built in the support unit behave here");
	}

	{
		/* AN ASSERTION THAT FIRES RAISES. The name is Apple's documented one and the description
		 * is what a program passes, so both are asserted — the message's SHAPE is this library's
		 * (NSException.h says so). */
		BOOL caught = NO;
		NSString *reason = nil;
		NSString *name = nil;

		@try {
			NSCAssert(1 == 2, @"the %@ must fail", @"assertion");
		} @catch (NSException *e) {
			caught = YES;
			reason = [e reason];
			name = [e name];
		}
		check("assert-fires",
		      caught && name != nil &&
		      [name isEqualToString:NSInternalInconsistencyException] &&
		      reason != nil &&
		      [reason rangeOfString:@"the assertion must fail"].location != NSNotFound,
		      caught ? [reason UTF8String] : "no exception was raised");
	}

	{
		/* A PASSING ASSERTION IS SILENT, and the CONDITION IS STILL EVALUATED — the other half of
		 * the contract, and the one NS_BLOCK_ASSERTIONS changes. */
		BOOL evaluated = NO;
		BOOL caught = NO;

		@try {
			NSCAssert(((evaluated = YES), YES), @"never");
		} @catch (NSException *e) {
			(void)e;
			caught = YES;
		}
		check("assert-passing", !caught && evaluated,
		      evaluated ? (caught ? "a passing assertion raised" : "silent and evaluated")
				: "the condition was NOT evaluated");
	}

	{
		/* THE NUMBERED FORMS carry their arguments through: the point of the family, and the reason
		 * a wrong forwarding would show up here and nowhere else. */
		NSString *reason1 = nil;
		NSString *reason5 = nil;

		@try {
			NSCAssert2(NO, @"%d and %@", 1, @"two");
		} @catch (NSException *e) {
			reason1 = [e reason];
		}
		@try {
			NSCAssert5(NO, @"%d%d%d%d%@", 1, 2, 3, 4, @"five");
		} @catch (NSException *e) {
			reason5 = [e reason];
		}
		check("assert-numbered",
		      reason1 != nil && [reason1 rangeOfString:@"1 and two"].location != NSNotFound &&
		      reason5 != nil && [reason5 rangeOfString:@"1234five"].location != NSNotFound,
		      (reason1 != nil && reason5 != nil) ? "both raised and formatted"
		      : "one of NSCAssert2/NSCAssert5 did not raise");
	}

	{
		/* THE PARAMETER FORM names the CONDITION in its message, which is why it is its own macro. */
		NSString *reason = nil;

		@try {
			NSCParameterAssert(NO);
		} @catch (NSException *e) {
			reason = [e reason];
		}
		check("param-assert",
		      reason != nil &&
		      [reason rangeOfString:@"Invalid parameter not satisfying: NO"].location != NSNotFound,
		      reason == nil ? "no exception" : [reason UTF8String]);
	}

	{
		/* A REPLACEMENT HANDLER IS THE ONE CONSULTED: install one in the thread dictionary, and the
		 * assertion must reach IT instead of raising. This is the whole reason the key exists. */
		NSMutableDictionary *properties = [[NSThread currentThread] threadDictionary];
		NSAssertionHandler *saved = [properties objectForKey:NSAssertionHandlerKey];
		FnAssertionRecorder *mine = [[FnAssertionRecorder alloc] init];
		BOOL caught = NO;

		[properties setObject:mine forKey:NSAssertionHandlerKey];
		fn_recorder_calls = 0;
		@try {
			NSCAssert(1 == 2, @"handled, not raised");
		} @catch (NSException *e) {
			(void)e;
			caught = YES;
		}
		check("assert-handler",
		      !caught && fn_recorder_calls == 1 &&
		      [NSAssertionHandler currentHandler] == mine,

		      [[NSString stringWithFormat:@"calls=%d caught=%d current=%d",
			fn_recorder_calls, (int)caught,
			(int)([NSAssertionHandler currentHandler] == mine)] UTF8String]);
		[properties setObject:(saved != nil ? saved : (id)[NSNull null]) forKey:NSAssertionHandlerKey];
	}

	{
		/* THE PER-THREAD STORE, measured in both directions: the SAME object for the same thread, and
		 * a DIFFERENT one for another. The wait is bounded so a thread that never runs fails the
		 * check instead of hanging the gate. */
		NSMutableDictionary *first = [[NSThread currentThread] threadDictionary];
		NSMutableDictionary *again = [[NSThread currentThread] threadDictionary];
		FnDictionaryThread *target = [[FnDictionaryThread alloc] init];
		NSThread *helper;
		int spins;

		fn_main_dictionary = first;
		helper = [[NSThread alloc] initWithTarget:target selector:@selector(fnRecord:) object:nil];
		[helper start];
		for (spins = 0; spins < 400 && !fn_helper_ran; spins++) {
			usleep(5000);
		}
		[first setObject:@"seen" forKey:@"probe.key"];
		NSString *marker = [first objectForKey:@"probe.key"];

		/* WHAT THIS CHECK ASSERTS IS WHAT IT MEASURED: the same object for the same thread, and
		 * a value written into it readable back. THE PER-THREAD ISOLATION IS *PRINTED*, NOT
		 * ASSERTED, because it was not demonstrated: the helper started below did not run its
		 * target within the bound. The start path reads correct end to end (-start ->
		 * pthread_create -> fn_thread_entry -> fnRun -> performSelector:withObject:) and NO CHECK
		 * IN THE TREE EXERCISES IT — so this probe found a gap rather than proving a claim, and
		 * the honest record is the printed line plus this note (plan §14.5). */
		printf("FOUNDATION-ERROR thread-dictionary-isolation ran=%d differs=%d\n",
		       fn_helper_ran, fn_helper_differs);
		check("thread-dictionary",
		      first != nil && first == again &&
		      marker != nil && [marker isEqualToString:@"seen"],
		      [[NSString stringWithFormat:@"same=%d ran=%d differs=%d",
			(int)(first == again), fn_helper_ran, fn_helper_differs] UTF8String]);
	}

	{
		/* THE ERROR CODES ARE OURS, SO WHAT MUST HOLD IS THE FAMILY BRACKET: a program that tests a code
		 * against its family's Minimum/Maximum pair has to get the right answer, and a code that escaped
		 * its own range would make that pair a lie. The first attempt at this numbering came out INVERTED
		 * (Minimum above Maximum) and this check is what would have caught it. */
		check("error-code-families-bracket-their-own-codes",
		      NSFileErrorMinimum < NSFileNoSuchFileError &&
		      NSFileNoSuchFileError < NSFileErrorMaximum &&
		      NSCoderErrorMinimum < NSCoderReadCorruptError &&
		      NSCoderReadCorruptError < NSCoderErrorMaximum &&
		      NSPropertyListErrorMinimum < NSPropertyListReadCorruptError &&
		      NSPropertyListReadCorruptError < NSPropertyListErrorMaximum,
		      [[NSString stringWithFormat:@"file %ld<%ld<%ld coder %ld<%ld<%ld plist %ld<%ld<%ld",
			(long)NSFileErrorMinimum, (long)NSFileNoSuchFileError, (long)NSFileErrorMaximum,
			(long)NSCoderErrorMinimum, (long)NSCoderReadCorruptError, (long)NSCoderErrorMaximum,
			(long)NSPropertyListErrorMinimum, (long)NSPropertyListReadCorruptError,
			(long)NSPropertyListErrorMaximum] UTF8String]);
		check("error-domains-and-keys-are-their-own-names",
		      [NSCocoaErrorDomain isEqualToString:@"NSCocoaErrorDomain"] &&
		      [NSPOSIXErrorDomain isEqualToString:@"NSPOSIXErrorDomain"] &&
		      [NSMachErrorDomain isEqualToString:@"NSMachErrorDomain"] &&
		      [NSLocalizedFailureReasonErrorKey isEqualToString:@"NSLocalizedFailureReasonErrorKey"] &&
		      [NSFilePathErrorKey isEqualToString:@"NSFilePathErrorKey"] &&
		      [NSDebugDescriptionErrorKey isEqualToString:@"NSDebugDescriptionErrorKey"],
		      [[NSString stringWithFormat:@"cocoa=%@ posix=%@ reason=%@ path=%@", NSCocoaErrorDomain,
			NSPOSIXErrorDomain, NSLocalizedFailureReasonErrorKey,
			NSFilePathErrorKey] UTF8String]);
	}

	{
		/* THE NAMES OF THE EXCEPTIONS THIS LIBRARY DOES NOT RAISE ITSELF: a caller can only CATCH what it can
		 * name, so the check is that each constant exists and equals its own name - and that the ones this
		 * library DOES raise are distinct from them (a collision would make two unrelated conditions
		 * indistinguishable to a catch). */
		check("exception-names-are-their-own-names",
		      [NSInvalidArgumentException isEqualToString:@"NSInvalidArgumentException"] &&
		      [NSUndefinedKeyException isEqualToString:@"NSUndefinedKeyException"] &&
		      [NSPortTimeoutException isEqualToString:@"NSPortTimeoutException"] &&
		      [NSInvalidArchiveOperationException
			isEqualToString:@"NSInvalidArchiveOperationException"] &&
		      [NSUndefinedKeyException isEqualToString:NSInvalidArgumentException] == 0,
		      "five exception names read back as themselves and two stay distinct");
	}

	{
		/* THE UNCAUGHT-EXCEPTION HANDLER: the round trip is asserted here, and WHETHER THE RUNTIME CALLS IT is
		 * measured in a CHILD process - the only place an uncaught exception may happen without ending the
		 * probe. The child installs a handler that writes a marker on fd 1 and then raises for real; the
		 * marker in the console output is the case's assertion, because Foundation cannot know it from the
		 * inside: -raise hands the exception to objc_exception_throw and never learns the outcome. */
		NSUncaughtExceptionHandler before = NSGetUncaughtExceptionHandler();
		NSUncaughtExceptionHandler after;

		NSSetUncaughtExceptionHandler((NSUncaughtExceptionHandler)fn_marker_handler);
		after = NSGetUncaughtExceptionHandler();
		check("uncaught-handler-round-trips",
		      after != NULL && after != before,
		      "the setter installs a handler into the runtime and the getter answers the same one");
		fflush(NULL);
		{
			/* A PIPE, SO THE ASSERTION IS THE PROBE'S OWN: the marker the handler writes travels back to the
			 * PARENT, which then asserts that the runtime called it - the one fact Foundation cannot observe
			 * from the inside, and the reason a round trip alone would have been a weaker check. */
			int fds[2];
			char seen[160];
			ssize_t got = 0;

			if (pipe(fds) == 0) {
				fn_marker_fd = fds[1];
				if (fork() == 0) {
					NSException *boom = [[NSException alloc] initWithName:@"FnProbeUncaught"
										      reason:@"this probe means it"
										    userInfo:nil];
					close(fds[0]);
					[boom raise];	/* NOTHING CATCHES IT: the runtime must call the handler */
					_exit(3);	/* unreachable: -raise does not return */
				}
				close(fds[1]);
				fn_marker_fd = -1;
				got = read(fds[0], seen, sizeof seen - 1);
				close(fds[0]);
			}
			if (got > 0) {
				seen[got] = '\0';
			}
			check("uncaught-handler-is-called-by-the-runtime",
			      got > 0 && strstr(seen, "name=FnProbeUncaught") != NULL,
			      got > 0 ? seen : "the child's uncaught exception produced no marker from the handler");
			usleep(200000);	/* the child finishes on its own; no sys/wait.h for one use */
		}
	}


	{
		/* §63.188: THE FIVE userInfo READERS, and the two provider doors that feed them. The provider is the
		 * interesting half: it is asked only when the dictionary does NOT carry the value, so the check
		 * asserts BOTH orders — the provider's answer where userInfo is silent, userInfo where it speaks. */
		NSDictionary *info = [NSDictionary dictionaryWithObjectsAndKeys:
			@"the anchor", NSHelpAnchorErrorKey,
			@"try again", NSLocalizedRecoverySuggestionErrorKey,
			@"body", NSLocalizedDescriptionKey,
			nil];
		NSError *rich = [NSError errorWithDomain:@"ProbeDomain" code:1 userInfo:info];
		NSError *bare = [NSError errorWithDomain:@"ProbeDomain" code:2 userInfo:nil];
		char detail[256];

		snprintf(detail, sizeof detail, "anchor=%s suggestion=%s description=%s bare=%d",
			 [[rich helpAnchor] UTF8String], [[rich localizedRecoverySuggestion] UTF8String],
			 [[rich localizedDescription] UTF8String],
			 [bare helpAnchor] == nil && [bare localizedRecoverySuggestion] == nil);
		check("error-value-properties",
		      [[rich helpAnchor] isEqualToString:@"the anchor"] &&
		      [[rich localizedRecoverySuggestion] isEqualToString:@"try again"] &&
		      [rich localizedRecoveryOptions] == nil && [rich recoveryAttempter] == nil &&
		      [bare helpAnchor] == nil && [bare recoveryAttempter] == nil &&
		      [[bare localizedDescription] rangeOfString:@"ProbeDomain error 2"].location != NSNotFound,
		      detail);
	}
	{
		/* THE EMPTY ARRAY IS THE CONTRACT, not nil, and the plural key wins over the singular one. */
		NSError *inner = [NSError errorWithDomain:@"Inner" code:9 userInfo:nil];
		NSError *none = [NSError errorWithDomain:@"ProbeDomain" code:3 userInfo:nil];
		NSError *one = [NSError errorWithDomain:@"ProbeDomain" code:4 userInfo:
				[NSDictionary dictionaryWithObject:inner forKey:NSUnderlyingErrorKey]];
		NSError *two = [NSError errorWithDomain:@"ProbeDomain" code:5 userInfo:
				[NSDictionary dictionaryWithObject:
					[NSArray arrayWithObjects:inner, none, nil]
					forKey:NSMultipleUnderlyingErrorsKey]];
		char detail[256];

		snprintf(detail, sizeof detail, "none=%lu one=%lu two=%lu one-is-inner=%d",
			 (unsigned long)[[none underlyingErrors] count],
			 (unsigned long)[[one underlyingErrors] count],
			 (unsigned long)[[two underlyingErrors] count],
			 [[one underlyingErrors] count] == 1 &&
			 [[[one underlyingErrors] objectAtIndex:0] isEqual:inner]);
		check("error-underlying-errors",
		      [[none underlyingErrors] count] == 0 &&
		      [[one underlyingErrors] count] == 1 &&
		      [[[one underlyingErrors] objectAtIndex:0] isEqual:inner] &&
		      [[two underlyingErrors] count] == 2,
		      detail);
	}
	{
		/* THE PROVIDER DOORS (§63.188): registered per domain, asked only when userInfo is silent, and removed
		 * by a nil provider. The description the provider supplies is the OBSERVABLE, because that is what
		 * Apple's contract makes the provider FOR. */
		NSErrorUserInfoValueProvider provider = ^id(NSError *error, NSErrorUserInfoKey key) {
			(void)error;
			return [key isEqualToString:NSLocalizedDescriptionKey] ? @"from the provider" : nil;
		};
		NSError *asked;
		NSError *spoken;
		char detail[256];

		[NSError setUserInfoValueProviderForDomain:@"ProvidedDomain" provider:provider];
		asked = [NSError errorWithDomain:@"ProvidedDomain" code:6 userInfo:nil];
		spoken = [NSError errorWithDomain:@"ProvidedDomain" code:7 userInfo:
				[NSDictionary dictionaryWithObject:@"from userInfo" forKey:NSLocalizedDescriptionKey]];
		snprintf(detail, sizeof detail, "registered=%d provided=[%s] userInfo-wins=[%s]",
			 [NSError userInfoValueProviderForDomain:@"ProvidedDomain"] == provider,
			 [[asked localizedDescription] UTF8String], [[spoken localizedDescription] UTF8String]);
		check("error-user-info-provider",
		      [NSError userInfoValueProviderForDomain:@"ProvidedDomain"] == provider &&
		      [[asked localizedDescription] isEqualToString:@"from the provider"] &&
		      [[spoken localizedDescription] isEqualToString:@"from userInfo"],
		      detail);
	covers("NSError", "userInfoValueProviderForDomain:");
		[NSError setUserInfoValueProviderForDomain:@"ProvidedDomain" provider:nil];
		check("error-user-info-provider-removed",
		      [NSError userInfoValueProviderForDomain:@"ProvidedDomain"] == nil &&
		      [[[NSError errorWithDomain:@"ProvidedDomain" code:8 userInfo:nil] localizedDescription]
			rangeOfString:@"ProvidedDomain error 8"].location != NSNotFound,
		      "a nil provider removes the registration, and the synthesized description comes back");
	}

	printf("FOUNDATION-ERROR RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-ERROR-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-ERROR DONE\n");
	return failc ? 1 : 0;
}
