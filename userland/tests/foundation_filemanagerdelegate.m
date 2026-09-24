/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filemanagerdelegate, unit of 1 — W8 slice 2's acceptance for NSFileManagerDelegate.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. IT WORKS IN A TREE OF ITS OWN MAKING under
 * /System/Temporary Files — this system's temp directory, spelled the FSH way — and removes it at the
 * end.
 *
 * WHAT IT MEASURES, and every one of these is a RULE from Apple's own pages rather than a taste:
 *   delegate-defaults-to-nil-and-round-trips      the property's default is nil, and it is ASSIGN -
 *                                                 the manager hands back the same object;
 *   a-delegate-implementing-nothing-is-legal      every protocol member is optional, so a conforming
 *                                                 object with no methods is a valid delegate;
 *   copy-asks-once-for-the-directory-and-once-for-each-item   "once for the directory and once for
 *                                                 each item in the directory";
 *   move-asks-only-for-the-item-itself            Apple's stated asymmetry: a moved directory's
 *                                                 CONTENTS are never asked about;
 *   remove-asks-for-every-item                    "prior to removing each item";
 *   link-asks-for-the-item-and-the-link-is-real   and the link is a second NAME (same inode);
 *   the-url-form-is-preferred-when-both-exist     "always prefers methods that take an NSURL object
 *                                                 over those that take an NSString object";
 *   the-path-form-is-asked-when-it-is-all-there-is  and it is asked when it is the only form present;
 *   a-veto-skips-the-item-and-the-operation-succeeds  a refusal is not an error: nothing is copied
 *                                                 for that item, the rest is, and the operation says YES;
 *   a-veto-on-a-directory-skips-its-contents      and its contents are never even asked about,
 *                                                 because the recursion is not entered;
 *   a-veto-on-remove-keeps-the-whole-subtree      Apple's own sentence: "returning NO prevents both
 *                                                 the directory and its children from being deleted";
 *   no-delegate-still-fails-on-an-error           the old behaviour, preserved;
 *   an-unimplemented-error-door-leaves-the-error-standing  "may also call" - so an absent door does
 *                                                 not silently swallow anything;
 *   the-error-door-can-swallow-an-error           YES ignores it and the walk carries on;
 *   the-error-door-can-abort                      NO stops the operation with that error;
 *   the-proceed-question-carries-the-errno        the NSError it is handed is NSPOSIXErrorDomain with
 *                                                 the failing call's errno;
 *   probe-tree-removed                            the tree is gone.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#define PROBE_ROOT "/System/Temporary Files/nsfilemanagerdelegate-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEMANAGERDELEGATE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEMANAGERDELEGATE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

/* ---- THE FIXTURE, built with POSIX calls so a create bug cannot be read as a delegate bug. ---- */

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

/* mkdir -p, AND THE `-p` IS NOT A CONVENIENCE: the fixture names paths like `vetodir/d`, and a helper
 * that makes only the LAST component fails on the parent - which is what this probe's first run did,
 * leaving two legs measuring an empty tree and five checks red for a reason that had nothing to do
 * with a delegate. (The DIAG line below is what said so, which is why it is a line and not a silent
 * return.) */
static void fn_make_dir(NSString *path)
{
	NSArray *parts = [path componentsSeparatedByString:@"/"];
	NSMutableString *prefix = [NSMutableString string];
	NSUInteger i;

	if ([path hasPrefix:@"/"]) {
		[prefix appendString:@"/"];
	}
	for (i = 0; i < [parts count]; i++) {
		NSString *part = [parts objectAtIndex:i];

		if ([part length] == 0) {
			continue;	/* the empty piece before a leading slash, and any doubled one */
		}
		if (![prefix hasSuffix:@"/"]) {
			[prefix appendString:@"/"];
		}
		[prefix appendString:part];
		if (mkdir([prefix UTF8String], 0755) != 0 && errno != EEXIST) {
			printf("FOUNDATION-FILEMANAGERDELEGATE DIAG mkdir %s: %s\n", [prefix UTF8String],
			       strerror(errno));
		}
	}
}

static void fn_make_file(NSString *path, const char *contents)
{
	int fd = open([path UTF8String], O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd < 0) {
		printf("FOUNDATION-FILEMANAGERDELEGATE DIAG open %s: %s\n", [path UTF8String],
		       strerror(errno));
		return;
	}
	if (write(fd, contents, strlen(contents)) != (ssize_t)strlen(contents)) {
		printf("FOUNDATION-FILEMANAGERDELEGATE DIAG write %s\n", [path UTF8String]);
	}
	close(fd);
}

/* A FIFO, and it is here for ONE reason: this probe needs a PER-ITEM error that is deterministic in a
 * guest that runs as root. A symlink used to serve, and its refusal was a departure §60 recorded -
 * fixed, because Apple copies the link - so the source of the error moved to the refusal that is still
 * there: the copy answers ENOTSUP for a source that is neither a directory, a regular file nor a link
 * (a fifo, a device, a socket), by name and through the error door. */
static void fn_make_fifo(NSString *path)
{
	if (mkfifo([path UTF8String], 0644) != 0) {
		printf("FOUNDATION-FILEMANAGERDELEGATE DIAG mkfifo %s: %s\n", [path UTF8String],
		       strerror(errno));
	}
}

static NSString *fn_joined_list(NSArray *items)
{
	return [[[NSSet setWithArray:items] allObjects] componentsJoinedByString:@" "];
}

/* ---- THE DELEGATES ---------------------------------------------------------------------------- */

/* THE PATH-ONLY DELEGATE: all FOUR path questions and their FOUR error doors, and NONE of the URL
 * ones. That shape is what makes Apple's preference rule measurable from both sides: with this
 * delegate the path form is what must be asked (the URL one does not exist), and with the next one
 * the URL form must win. */
@interface PathDelegate : NSObject <NSFileManagerDelegate>
{
@public
	NSMutableArray *asked;		/* every SOURCE path a "should" question was about */
	NSMutableArray *errors;		/* every error-door call, spelled "kind:code" */
	NSMutableSet *vetoed;		/* last path components answered NO for */
	BOOL proceed;			/* what the error doors answer */
}
@end

@implementation PathDelegate

- (BOOL)fnWants:(NSString *)source
{
	[asked addObject:source];
	return [vetoed containsObject:[source lastPathComponent]] ? NO : YES;
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldCopyItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	return [self fnWants:srcPath];
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldMoveItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	return [self fnWants:srcPath];
}

- (BOOL)fileManager:(NSFileManager *)fileManager shouldRemoveItemAtPath:(NSString *)path
{
	return [self fnWants:path];
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldLinkItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	return [self fnWants:srcPath];
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
  copyingItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	[errors addObject:[NSString stringWithFormat:@"copy:%ld:%@", (long)[error code], [error domain]]];
	return proceed;
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
   movingItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	[errors addObject:[NSString stringWithFormat:@"move:%ld:%@", (long)[error code], [error domain]]];
	return proceed;
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
 removingItemAtPath:(NSString *)path
{
	[errors addObject:[NSString stringWithFormat:@"remove:%ld:%@", (long)[error code], [error domain]]];
	return proceed;
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
  linkingItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	[errors addObject:[NSString stringWithFormat:@"link:%ld:%@", (long)[error code], [error domain]]];
	return proceed;
}

@end

/* A DELEGATE THAT IMPLEMENTS NOTHING AT ALL: legal exactly because every member of the protocol is
 * optional, and the shape "the delegate does not implement the appropriate methods" needs. */
@interface SilentDelegate : NSObject <NSFileManagerDelegate>
@end

@implementation SilentDelegate
@end

/* BOTH FORMS, so that the preference rule has something to prefer: `-pathCalls` must stay ZERO when a
 * manager asks this delegate about a copy. */
@interface BothFormsDelegate : NSObject <NSFileManagerDelegate>
{
@public
	int pathCalls;
	int urlCalls;
	NSString *lastURLPath;
}
@end

@implementation BothFormsDelegate

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldCopyItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath
{
	pathCalls++;
	return YES;
}

- (BOOL)fileManager:(NSFileManager *)fileManager
shouldCopyItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL
{
	urlCalls++;
	lastURLPath = [srcURL path];
	return YES;
}

@end

int main(void)
{
	NSFileManager *manager = [[NSFileManager alloc] init];

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];	/* a previous run's litter */
	fn_make_dir(@PROBE_ROOT);

	{
		/* THE PROPERTY ITSELF, FIRST: default nil, ASSIGN (the same object comes back), and a
		 * conforming delegate with no methods at all is legal - which is what "every member is
		 * optional" means in practice. */
		id <NSFileManagerDelegate> before = [manager delegate];
		SilentDelegate *silent = [[SilentDelegate alloc] init];
		BOOL copied;

		[manager setDelegate:silent];
		fn_make_dir(fn_path(@"plain"));
		fn_make_file(fn_path(@"plain/silent.txt"), "s");
		copied = [manager copyItemAtPath:fn_path(@"plain/silent.txt")
					  toPath:fn_path(@"plain/silent-copy.txt")
					   error:NULL];
		/* THE DELEGATE IS ASSIGN, SO THE PROBE MUST KEEP ITS OWN REFERENCE - and the reads below are
		 * also what keeps ARC from releasing the delegate out from under the manager. */
		check("delegate-defaults-to-nil-and-round-trips",
		      before == nil && [manager delegate] == silent,
		      [NSString stringWithFormat:@"default=%s handed-back-the-same-object=%d",
			before == nil ? "nil" : "not nil", (int)([manager delegate] == silent)]);
		check("a-delegate-implementing-nothing-is-legal",
		      copied && [manager fileExistsAtPath:fn_path(@"plain/silent-copy.txt")],
		      [NSString stringWithFormat:@"copied=%d - a delegate with NO methods implemented is a "
			"valid delegate and changes nothing", (int)copied]);
		[manager setDelegate:nil];
	}

	{
		/* THE COPY ASKS ONCE PER ITEM, INCLUDING INSIDE A DIRECTORY - and the SAME leg shows the
		 * path form being asked when it is the only form that exists. THE TOP-LEVEL ITEM IS ONE OF
		 * THE ITEMS: Apple's "once for the directory and once for each item in the directory" counts
		 * the directory that was handed over, which this probe's first run got wrong by one. */
		PathDelegate *delegate = [[PathDelegate alloc] init];
		NSArray *expected = @[ fn_path(@"peritem"), fn_path(@"peritem/a.txt"), fn_path(@"peritem/d"),
				       fn_path(@"peritem/d/b.txt"), fn_path(@"peritem/d/c.txt") ];
		NSString *detail;

		delegate->asked = [NSMutableArray array];
		delegate->errors = [NSMutableArray array];
		delegate->vetoed = [[NSMutableSet alloc] init];
		fn_make_dir(fn_path(@"peritem"));
		fn_make_dir(fn_path(@"peritem/d"));
		fn_make_file(fn_path(@"peritem/a.txt"), "a");
		fn_make_file(fn_path(@"peritem/d/b.txt"), "bb");
		fn_make_file(fn_path(@"peritem/d/c.txt"), "ccc");
		[manager setDelegate:delegate];
		[manager copyItemAtPath:fn_path(@"peritem") toPath:fn_path(@"peritem-copy") error:NULL];

		detail = [NSString stringWithFormat:@"asked %lu: %@", (unsigned long)[delegate->asked count],
			fn_joined_list(delegate->asked)];
		check("copy-asks-once-for-the-directory-and-once-for-each-item",
		      [delegate->asked count] == [expected count] && fn_joined_list(delegate->asked) != nil &&
		      [[NSSet setWithArray:delegate->asked] isEqualToSet:[NSSet setWithArray:expected]],
		      detail);
		check("the-path-form-is-asked-when-it-is-all-there-is",
		      [delegate->asked count] == 5 && [delegate->errors count] == 0,
		      [NSString stringWithFormat:@"asked=%lu errors=%lu - a delegate with no URL methods is "
			"still asked, and nothing failed", (unsigned long)[delegate->asked count],
			(unsigned long)[delegate->errors count]]);
		[manager setDelegate:nil];
	}

	{
		/* THE MOVE ASKS ONLY FOR THE ITEM. Apple states this as the difference from the copy above:
		 * "if the item being moved is a directory, the file manager notifies the delegate only for
		 * the directory itself and not for any of its contents." */
		PathDelegate *delegate = [[PathDelegate alloc] init];

		delegate->asked = [NSMutableArray array];
		delegate->errors = [NSMutableArray array];
		delegate->vetoed = [[NSMutableSet alloc] init];
		fn_make_dir(fn_path(@"moved/d"));
		fn_make_file(fn_path(@"moved/d/x.txt"), "x");
		fn_make_file(fn_path(@"moved/d/y.txt"), "y");
		[manager setDelegate:delegate];
		[manager moveItemAtPath:fn_path(@"moved/d") toPath:fn_path(@"moved-d") error:NULL];

		check("move-asks-only-for-the-item-itself",
		      [delegate->asked count] == 1 &&
		      [[delegate->asked objectAtIndex:0] isEqualToString:fn_path(@"moved/d")] &&
		      [manager fileExistsAtPath:fn_path(@"moved-d/x.txt")],
		      [NSString stringWithFormat:@"asked %lu: %@", (unsigned long)[delegate->asked count],
			fn_joined_list(delegate->asked)]);
		[manager setDelegate:nil];
	}

	{
		/* THE REMOVE ASKS PER ITEM, "prior to removing each item". */
		PathDelegate *delegate = [[PathDelegate alloc] init];
		NSArray *expected = @[ fn_path(@"removed"), fn_path(@"removed/d"),
				       fn_path(@"removed/d/x.txt"), fn_path(@"removed/f.txt") ];

		delegate->asked = [NSMutableArray array];
		delegate->errors = [NSMutableArray array];
		delegate->vetoed = [[NSMutableSet alloc] init];
		fn_make_dir(fn_path(@"removed/d"));
		fn_make_file(fn_path(@"removed/d/x.txt"), "x");
		fn_make_file(fn_path(@"removed/f.txt"), "f");
		[manager setDelegate:delegate];
		[manager removeItemAtPath:fn_path(@"removed") error:NULL];

		check("remove-asks-for-every-item",
		      [[NSSet setWithArray:delegate->asked] isEqualToSet:[NSSet setWithArray:expected]] &&
		      ![manager fileExistsAtPath:fn_path(@"removed")],
		      [NSString stringWithFormat:@"asked %lu: %@", (unsigned long)[delegate->asked count],
			fn_joined_list(delegate->asked)]);
		[manager setDelegate:nil];
	}

	{
		/* THE LINK: one question, and the answer is a SECOND NAME - the same inode, which is what
		 * makes a hard link different from a copy. */
		PathDelegate *delegate = [[PathDelegate alloc] init];
		struct stat from;
		struct stat to;
		BOOL linked;

		delegate->asked = [NSMutableArray array];
		delegate->errors = [NSMutableArray array];
		delegate->vetoed = [[NSMutableSet alloc] init];
		fn_make_file(fn_path(@"linked.txt"), "l");
		[manager setDelegate:delegate];
		linked = [manager linkItemAtPath:fn_path(@"linked.txt") toPath:fn_path(@"linked-2.txt")
					   error:NULL];
		check("link-asks-for-the-item-and-the-link-is-real",
		      [delegate->asked count] == 1 && linked &&
		      stat([fn_path(@"linked.txt") UTF8String], &from) == 0 &&
		      stat([fn_path(@"linked-2.txt") UTF8String], &to) == 0 && from.st_ino == to.st_ino,
		      [NSString stringWithFormat:@"asked=%lu linked=%d inodes %llu/%llu",
			(unsigned long)[delegate->asked count], (int)linked,
			(unsigned long long)from.st_ino, (unsigned long long)to.st_ino]);
		[manager setDelegate:nil];
	}

	{
		/* THE PREFERENCE RULE: this delegate implements BOTH forms, so the URL one must be the one
		 * asked - "the file manager always prefers methods that take an NSURL object over those that
		 * take an NSString object". */
		BothFormsDelegate *delegate = [[BothFormsDelegate alloc] init];

		fn_make_file(fn_path(@"preferred.txt"), "p");
		[manager setDelegate:delegate];
		[manager copyItemAtPath:fn_path(@"preferred.txt") toPath:fn_path(@"preferred-copy.txt")
				  error:NULL];
		check("the-url-form-is-preferred-when-both-exist",
		      delegate->urlCalls == 1 && delegate->pathCalls == 0 &&
		      [delegate->lastURLPath isEqualToString:fn_path(@"preferred.txt")],
		      [NSString stringWithFormat:@"url=%d path=%d last=%@", delegate->urlCalls,
			delegate->pathCalls, delegate->lastURLPath]);
		[manager setDelegate:nil];
	}

	{
		/* A VETO IS A SKIP AND NOT A FAILURE: the item is not copied, its siblings are, and the
		 * operation answers YES - because a refusal has no errno and this class's contract is that
		 * every failure carries one. */
		PathDelegate *delegate = [[PathDelegate alloc] init];
		BOOL copied;

		delegate->asked = [NSMutableArray array];
		delegate->errors = [NSMutableArray array];
		delegate->vetoed = [NSMutableSet setWithObject:@"skip.txt"];
		fn_make_dir(fn_path(@"veto"));
		fn_make_file(fn_path(@"veto/keep.txt"), "k");
		fn_make_file(fn_path(@"veto/skip.txt"), "s");
		[manager setDelegate:delegate];
		copied = [manager copyItemAtPath:fn_path(@"veto") toPath:fn_path(@"veto-copy") error:NULL];
		check("a-veto-skips-the-item-and-the-operation-succeeds",
		      copied && [manager fileExistsAtPath:fn_path(@"veto-copy/keep.txt")] &&
		      ![manager fileExistsAtPath:fn_path(@"veto-copy/skip.txt")] &&
		      [delegate->errors count] == 0,
		      [NSString stringWithFormat:@"copied=%d keep=%d skip=%d errors=%lu", (int)copied,
			(int)[manager fileExistsAtPath:fn_path(@"veto-copy/keep.txt")],
			(int)[manager fileExistsAtPath:fn_path(@"veto-copy/skip.txt")],
			(unsigned long)[delegate->errors count]]);
		[manager setDelegate:nil];
	}

	{
		/* AND A VETO ON A DIRECTORY SKIPS ITS CONTENTS - the recursion is never entered, so the
		 * delegate is not even asked about them: Apple's "returning NO prevents both the directory
		 * and its children from being deleted", read for a copy. THE COUNT IS THREE (the tree, the
		 * vetoed directory, the sibling file) AND THE REAL PROPERTY IS THE ABSENCE of anything under
		 * the vetoed directory, which is asserted as such rather than inferred from the count. */
		PathDelegate *delegate = [[PathDelegate alloc] init];
		BOOL copied;
		BOOL contentsAsked = NO;
		NSUInteger i;

		delegate->asked = [NSMutableArray array];
		delegate->errors = [NSMutableArray array];
		delegate->vetoed = [NSMutableSet setWithObject:@"d"];
		fn_make_dir(fn_path(@"vetodir/d"));
		fn_make_file(fn_path(@"vetodir/d/inner.txt"), "i");
		fn_make_file(fn_path(@"vetodir/outer.txt"), "o");
		[manager setDelegate:delegate];
		copied = [manager copyItemAtPath:fn_path(@"vetodir") toPath:fn_path(@"vetodir-copy")
					   error:NULL];
		for (i = 0; i < [delegate->asked count]; i++) {
			NSString *asked = [delegate->asked objectAtIndex:i];

			if ([asked hasPrefix:[fn_path(@"vetodir/d") stringByAppendingString:@"/"]]) {
				contentsAsked = YES;
			}
		}
		check("a-veto-on-a-directory-skips-its-contents",
		      copied && ![manager fileExistsAtPath:fn_path(@"vetodir-copy/d")] &&
		      [manager fileExistsAtPath:fn_path(@"vetodir-copy/outer.txt")] &&
		      [delegate->asked count] == 3 && !contentsAsked,
		      [NSString stringWithFormat:@"copied=%d dir=%d contents-asked=%d asked %lu: %@",
			(int)copied, (int)[manager fileExistsAtPath:fn_path(@"vetodir-copy/d")],
			(int)contentsAsked, (unsigned long)[delegate->asked count],
			fn_joined_list(delegate->asked)]);
		[manager setDelegate:nil];
	}

	{
		/* THE SAME RULE FOR A REMOVE, AND APPLE SAYS IT OUT LOUD: a veto on a directory prevents the
		 * directory AND its children from being deleted, while a sibling file still goes.
		 *
		 * AND THE OPERATION ITSELF THEN REPORTS NO - WHICH IS THE SYSCALL TALKING AND NOT THE VETO.
		 * A veto is a skip, so `-removeItemAtPath:` still tries to remove the directory it was asked
		 * about; that directory cannot come out (a child is still in it), rmdir(2) answers ENOTEMPTY,
		 * and the error door is asked about THAT - a REAL errno, unlike the refusal, which has none.
		 * The probe's first expectation here was YES, and it was wrong: Apple's sentence says what is
		 * NOT deleted, and this class's contract says a failure reports its errno. */
		PathDelegate *refusing = [[PathDelegate alloc] init];
		PathDelegate *permitting = [[PathDelegate alloc] init];
		NSError *refusedError = nil;
		BOOL refused;
		BOOL permitted;

		refusing->asked = [NSMutableArray array];
		refusing->errors = [NSMutableArray array];
		refusing->vetoed = [NSMutableSet setWithObject:@"keepdir"];
		permitting->asked = [NSMutableArray array];
		permitting->errors = [NSMutableArray array];
		permitting->vetoed = [NSMutableSet setWithObject:@"keepdir"];
		permitting->proceed = YES;
		fn_make_dir(fn_path(@"removing/keepdir"));
		fn_make_file(fn_path(@"removing/keepdir/inner.txt"), "i");
		fn_make_file(fn_path(@"removing/goes.txt"), "g");
		[manager setDelegate:refusing];
		refused = [manager removeItemAtPath:fn_path(@"removing") error:&refusedError];
		check("a-veto-on-remove-keeps-the-whole-subtree",
		      !refused && refusedError != nil && [refusedError code] == ENOTEMPTY &&
		      ![manager fileExistsAtPath:fn_path(@"removing/goes.txt")] &&
		      [manager fileExistsAtPath:fn_path(@"removing/keepdir/inner.txt")],
		      [NSString stringWithFormat:@"removed=%d code=%ld goes=%d kept=%d", (int)refused,
			(long)(refusedError != nil ? [refusedError code] : -1),
			(int)[manager fileExistsAtPath:fn_path(@"removing/goes.txt")],
			(int)[manager fileExistsAtPath:fn_path(@"removing/keepdir/inner.txt")]]);
		/* AND WITH THE ERROR DOOR ANSWERING YES THE SAME SHAPE SUCCEEDS - which is the remove half of
		 * what the copy leg below measures, and the reason the two doors exist at all. */
		fn_make_dir(fn_path(@"removing2/keepdir"));
		fn_make_file(fn_path(@"removing2/keepdir/inner.txt"), "i");
		fn_make_file(fn_path(@"removing2/goes.txt"), "g");
		[manager setDelegate:permitting];
		permitted = [manager removeItemAtPath:fn_path(@"removing2") error:NULL];
		check("a-veto-on-remove-needs-the-error-door-to-succeed",
		      permitted && [permitting->errors count] == 1 &&
		      ![manager fileExistsAtPath:fn_path(@"removing2/goes.txt")] &&
		      [manager fileExistsAtPath:fn_path(@"removing2/keepdir/inner.txt")],
		      [NSString stringWithFormat:@"removed=%d errors=%lu goes=%d kept=%d", (int)permitted,
			(unsigned long)[permitting->errors count],
			(int)[manager fileExistsAtPath:fn_path(@"removing2/goes.txt")],
			(int)[manager fileExistsAtPath:fn_path(@"removing2/keepdir/inner.txt")]]);
		[manager setDelegate:nil];
	}

	{
		/* THE ERROR DOORS, AND THE ERROR IS A PER-ITEM REFUSAL INSIDE A TREE: a FIFO among regular
		 * files, which the copy answers ENOTSUP for by name (§60 records that refusal - what it is NOT
		 * is the symlink it used to be, because Apple copies a link, and a probe must not stand on a
		 * departure).
		 *
		 * THE SHAPE MATTERS AS MUCH AS THE ERROR: a FIFO can sit BESIDE the files, so a delegate that
		 * swallows it lets the walk reach the others - which is what Apple's "continues copying any
		 * other items and ignores the error" actually promises, and something a failure at the TOP of
		 * a tree could never show, since the whole tree is one item and there is nothing to continue
		 * with. This leg taught that the hard way: an existing destination refuses the top item, so it
		 * cannot demonstrate the "other items" half at all.
		 *
		 * FOUR ANSWERS, and each is a different rule: no delegate, a delegate that implements NO error
		 * door, one that swallows, and one that aborts. */
		SilentDelegate *silent = [[SilentDelegate alloc] init];
		PathDelegate *swallow = [[PathDelegate alloc] init];
		PathDelegate *abort = [[PathDelegate alloc] init];
		NSError *noDelegateError = nil;
		NSError *silentError = nil;
		NSError *swallowError = nil;
		NSError *abortError = nil;
		BOOL withNone;
		BOOL withSilent;
		BOOL withSwallow;
		BOOL withAbort;

		swallow->asked = [NSMutableArray array];
		swallow->errors = [NSMutableArray array];
		swallow->vetoed = [[NSMutableSet alloc] init];
		swallow->proceed = YES;
		abort->asked = [NSMutableArray array];
		abort->errors = [NSMutableArray array];
		abort->vetoed = [[NSMutableSet alloc] init];
		abort->proceed = NO;
		fn_make_dir(fn_path(@"trouble"));
		fn_make_file(fn_path(@"trouble/a.txt"), "a");
		fn_make_fifo(fn_path(@"trouble/pipe"));
		fn_make_file(fn_path(@"trouble/z.txt"), "z");

		[manager setDelegate:nil];
		withNone = [manager copyItemAtPath:fn_path(@"trouble") toPath:fn_path(@"trouble-none")
					      error:&noDelegateError];
		[manager setDelegate:silent];
		withSilent = [manager copyItemAtPath:fn_path(@"trouble") toPath:fn_path(@"trouble-silent")
					       error:&silentError];
		[manager setDelegate:swallow];
		withSwallow = [manager copyItemAtPath:fn_path(@"trouble") toPath:fn_path(@"trouble-swallow")
					       error:&swallowError];
		[manager setDelegate:abort];
		withAbort = [manager copyItemAtPath:fn_path(@"trouble") toPath:fn_path(@"trouble-abort")
					     error:&abortError];
		[manager setDelegate:nil];

		check("no-delegate-still-fails-on-an-error",
		      !withNone && noDelegateError != nil && [noDelegateError code] == ENOTSUP &&
		      ![manager fileExistsAtPath:fn_path(@"trouble-none/pipe")],
		      [NSString stringWithFormat:@"copied=%d code=%ld pipe=%d", (int)withNone,
			(long)(noDelegateError != nil ? [noDelegateError code] : -1),
			(int)[manager fileExistsAtPath:fn_path(@"trouble-none/pipe")]]);
		/* AND THE SHAPE THAT NEEDS SAYING OUT LOUD: a delegate that conforms but implements NO error
		 * door leaves the error STANDING. That is what Apple's "the file manager MAY also call this
		 * method" means in practice, and it is the reason a delegate cannot silently swallow a failure
		 * by simply not having an opinion. */
		check("an-unimplemented-error-door-leaves-the-error-standing",
		      !withSilent && silentError != nil && [silentError code] == ENOTSUP,
		      [NSString stringWithFormat:@"copied=%d code=%ld", (int)withSilent,
			(long)(silentError != nil ? [silentError code] : -1)]);
		/* THE HALF THAT ONLY A PER-ITEM ERROR CAN SHOW: the walk CONTINUES, so the FIFO is absent from
		 * the copy while its two neighbours are present. Apple's sentence is "continues copying any
		 * other items and ignores the error", and this is what it means. */
		check("the-error-door-can-swallow-an-error",
		      withSwallow && [swallow->errors count] == 1 &&
		      [manager fileExistsAtPath:fn_path(@"trouble-swallow/a.txt")] &&
		      [manager fileExistsAtPath:fn_path(@"trouble-swallow/z.txt")] &&
		      ![manager fileExistsAtPath:fn_path(@"trouble-swallow/pipe")],
		      [NSString stringWithFormat:@"copied=%d errors=%lu a=%d z=%d pipe=%d", (int)withSwallow,
			(unsigned long)[swallow->errors count],
			(int)[manager fileExistsAtPath:fn_path(@"trouble-swallow/a.txt")],
			(int)[manager fileExistsAtPath:fn_path(@"trouble-swallow/z.txt")],
			(int)[manager fileExistsAtPath:fn_path(@"trouble-swallow/pipe")]]);
		check("the-error-door-can-abort",
		      !withAbort && abortError != nil && [abortError code] == ENOTSUP &&
		      [abort->errors count] == 1,
		      [NSString stringWithFormat:@"copied=%d code=%ld errors=%lu", (int)withAbort,
			(long)(abortError != nil ? [abortError code] : -1),
			(unsigned long)[abort->errors count]]);
		check("the-proceed-question-carries-the-errno",
		      [swallow->errors count] == 1 &&
		      [[swallow->errors objectAtIndex:0] isEqualToString:[NSString stringWithFormat:
			@"copy:%d:NSPOSIXErrorDomain", ENOTSUP]],
		      [swallow->errors count] == 1 ? [swallow->errors objectAtIndex:0] : @"(none)");
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"the tree is still there");
	}

	printf("FOUNDATION-FILEMANAGERDELEGATE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEMANAGERDELEGATE DONE\n");
	return failc == 0 ? 0 : 1;
}
