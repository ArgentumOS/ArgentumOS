/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_filewrapper, unit of 1 — W8 slice 4's acceptance for NSFileWrapper.
 * docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. IT WORKS IN A TREE OF ITS OWN MAKING under
 * /System/Temporary Files — this system's temp directory, spelled the FSH way — and removes it at the
 * end and at the start.
 *
 * WHAT IT MEASURES, and every one is a rule from the class's own pages:
 *   fw-wraps-a-file               init(path:) decides the kind "by the type of file-system node", and a
 *                                 regular file's wrapper carries its bytes, its name and its attributes;
 *   fw-wraps-a-directory-tree     a directory is a DICTIONARY of children keyed by name, navigable;
 *   fw-wraps-a-link               a symlink is a link and NOT what it points at (lstat, not stat) -
 *                                 which is what lets a DANGLING one be wrapped at all;
 *   fw-writing-reproduces-the-tree  a write is recursive and puts back what was read, node for node;
 *   fw-the-reading-option-is-observable  NSFileWrapperReadingImmediate is "the option to read files
 *                                 IMMEDIATELY", so its ABSENCE is the lazy form - and the two differ
 *                                 exactly when the file changes between wrapping and reading;
 *   fw-add-file-wrapper-answers-a-unique-key  the key is the child's preferred name "UNLESS that name is
 *                                 already in use", and -keyForChildFileWrapper: finds it again;
 *   fw-a-second-child-with-one-name-gets-another-key  ... which is the same rule seen from the caller's
 *                                 side, with -removeFileWrapper: taking one back out;
 *   fw-adding-to-a-non-directory-raises  Apple raises rather than doing nothing quietly;
 *   fw-name-updating-sets-filenames  -filename is nil until a write asks for the name updating, and then
 *                                 every descendant learns the name it was written under;
 *   fw-atomic-writing-leaves-no-temporary  an atomic tree write lands whole and leaves nothing behind.
 */

#import <Foundation/Foundation.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#define PROBE_ROOT "/System/Temporary Files/nsfilewrapper-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEWRAPPER %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEWRAPPER %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSString *fn_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

/* THE FIXTURE IS BUILT WITH POSIX CALLS, because what is under test is the WRAPPER and not a creator. */
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
			continue;
		}
		if (![prefix hasSuffix:@"/"]) {
			[prefix appendString:@"/"];
		}
		[prefix appendString:part];
		if (mkdir([prefix UTF8String], 0755) != 0 && errno != EEXIST) {
			printf("FOUNDATION-FILEWRAPPER DIAG mkdir %s: %s\n", [prefix UTF8String],
			       strerror(errno));
		}
	}
}

static void fn_make_file(NSString *path, const char *contents)
{
	int fd = open([path UTF8String], O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd < 0) {
		printf("FOUNDATION-FILEWRAPPER DIAG open %s: %s\n", [path UTF8String], strerror(errno));
		return;
	}
	if (write(fd, contents, strlen(contents)) != (ssize_t)strlen(contents)) {
		printf("FOUNDATION-FILEWRAPPER DIAG write %s\n", [path UTF8String]);
	}
	close(fd);
}

/* THE VALUES THIS PROBE MAKES AND HANDS TO THE CLASS GO THROUGH ONE NAME EACH, because the probe is
 * built with -Werror=nullable-to-nonnull-conversion and the class's doors take NONNULL arguments: Apple
 * declares +fileURLWithPath:, -dataUsingEncoding: and +dataWithContentsOfFile: as NULLABLE, so a call
 * site that passes one straight in is a build error rather than a runtime surprise. */
static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

static id fn_bytes(NSString *text)
{
	return [text dataUsingEncoding:NSUTF8StringEncoding];
}

static id fn_file_bytes(NSString *path)
{
	return [NSData dataWithContentsOfFile:path];
}

static NSString *fn_text(NSString *path)
{
	id bytes = fn_file_bytes(path);

	return bytes != nil ? [[NSString alloc] initWithData:bytes encoding:NSUTF8StringEncoding] : nil;
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *source = fn_path(@"source");

	[manager removeItemAtPath:@PROBE_ROOT error:NULL];
	fn_make_dir(@PROBE_ROOT);
	fn_make_dir(source);
	fn_make_file(fn_path(@"source/hello.txt"), "hello");
	fn_make_dir(fn_path(@"source/nested"));
	fn_make_file(fn_path(@"source/nested/deep.txt"), "deep");
	/* TWO TARGETS, DELIBERATELY DIFFERENT IN KIND: `link` points at a RELATIVE path, which is the form
	 * -symbolicLinkDestination: answers as a string, while `dangling` points at an ABSOLUTE one that does
	 * not exist - so the URL form has something it CAN build and the link is still dangling. This
	 * library's NSURL takes absolute paths only (F8's rule), which is why the relative case cannot have a
	 * URL at all. */
	symlink("hello.txt", [fn_path(@"source/link-rel") UTF8String]);
	symlink([fn_path(@"nowhere-at-all") UTF8String], [fn_path(@"source/dangling") UTF8String]);

	{
		/* A FILE, AND EVERYTHING THE WRAPPER REMEMBERS ABOUT IT. */
		NSFileWrapper *wrapper = [[NSFileWrapper alloc] initWithPath:fn_path(@"source/hello.txt")];
		NSData *bytes = [wrapper regularFileContents];
		NSDictionary *attributes = [wrapper fileAttributes];

		check("fw-wraps-a-file",
		      wrapper != nil && [wrapper isRegularFile] && ![wrapper isDirectory] &&
		      ![wrapper isSymbolicLink] &&
		      bytes != nil && [bytes isEqualToData:[NSData dataWithBytes:"hello" length:5]] &&
		      [[wrapper filename] isEqualToString:@"hello.txt"] &&
		      [[wrapper preferredFilename] isEqualToString:@"hello.txt"] &&
		      [[attributes objectForKey:NSFileSize] unsignedLongLongValue] == 5,
		      [NSString stringWithFormat:@"regular=%d bytes=%lu name=%@ size=%@",
			(int)[wrapper isRegularFile], (unsigned long)(bytes != nil ? [bytes length] : 0),
			[wrapper filename], [attributes objectForKey:NSFileSize]]);
	}

	{
		/* THE WHOLE TREE: a directory is a dictionary of children keyed by their names, and a child
		 * that is itself a directory is navigable in turn. */
		NSFileWrapper *wrapper = [[NSFileWrapper alloc] initWithPath:source];
		NSDictionary *children = [wrapper fileWrappers];
		NSFileWrapper *nested = [children objectForKey:@"nested"];
		NSFileWrapper *link = [children objectForKey:@"link-rel"];

		check("fw-wraps-a-directory-tree",
		      wrapper != nil && [wrapper isDirectory] && [children count] == 4 &&
		      nested != nil && [nested isDirectory] &&
		      [[nested fileWrappers] count] == 1 &&
		      link != nil && [link isSymbolicLink] &&
		      [manager fileExistsAtPath:fn_path(@"source")],
		      [NSString stringWithFormat:@"children=%lu nested=%d",
			(unsigned long)(children != nil ? [children count] : 0),
			(int)(nested != nil && [nested isDirectory])]);
	}

	{
		/* A LINK IS A LINK, INCLUDING A BROKEN ONE: the kind comes from lstat(2), so a wrapper can hold
		 * a destination that does not exist - which a kind read through stat(2) could never do. */
		NSFileWrapper *link = [[NSFileWrapper alloc] initWithPath:fn_path(@"source/dangling")];

		check("fw-wraps-a-link",
		      link != nil && [link isSymbolicLink] && ![link isRegularFile] &&
		      [[link symbolicLinkDestination] isEqualToString:fn_path(@"nowhere-at-all")] &&
		      /* THE URL IS THE TARGET'S, NOT THE LINK'S - which is the whole difference between the
		       * two doors: this one answers where the link POINTS, and the link's own path is what the
		       * wrapper was built FROM. */
		      [[[link symbolicLinkDestinationURL] path]
			isEqualToString:fn_path(@"nowhere-at-all")] &&
		      ![manager fileExistsAtPath:fn_path(@"source/dangling")],
		      [NSString stringWithFormat:@"link=%d target=%@ url=%@",
			(int)[link isSymbolicLink], [link symbolicLinkDestination],
			[[link symbolicLinkDestinationURL] path]]);
	}

	{
		/* A WRITE PUTS THE TREE BACK: kinds, bytes, a nested directory, and the links. */
		NSFileWrapper *wrapper = [[NSFileWrapper alloc] initWithPath:source];
		NSString *written = fn_path(@"written");
		BOOL wrote = [wrapper writeToURL:fn_url(written)
					 options:0
			     originalContentsURL:nil
					  error:NULL];
		NSFileWrapper *reread = [[NSFileWrapper alloc] initWithPath:written];
		NSDictionary *children = [reread fileWrappers];
		NSFileWrapper *link = [children objectForKey:@"link-rel"];
		NSFileWrapper *dangling = [children objectForKey:@"dangling"];
		NSString *text = fn_text(fn_path(@"written/nested/deep.txt"));

		check("fw-writing-reproduces-the-tree",
		      wrote && [reread isDirectory] && [children count] == 4 &&
		      [text isEqualToString:@"deep"] &&
		      link != nil && [link isSymbolicLink] &&
		      [[link symbolicLinkDestination] isEqualToString:@"hello.txt"] &&
		      dangling != nil && [dangling isSymbolicLink] &&
		      [[dangling symbolicLinkDestination] isEqualToString:fn_path(@"nowhere-at-all")],
		      [NSString stringWithFormat:@"wrote=%d children=%lu deep=%@ link=%@",
			(int)wrote, (unsigned long)(children != nil ? [children count] : 0), text,
			[link symbolicLinkDestination]]);
	}

	{
		/* THE READING OPTION'S ABSENCE, MADE VISIBLE: wrap the file, then CHANGE it, then read - and
		 * the lazy wrapper answers the NEW bytes while an NSFileWrapperReadingImmediate one answers the
		 * bytes it took when it was built. That pair is the whole meaning of the option. */
		NSFileWrapper *lazy = [[NSFileWrapper alloc] initWithPath:fn_path(@"source/hello.txt")];
		NSFileWrapper *eager = [[NSFileWrapper alloc]
			initWithURL:fn_url(fn_path(@"source/hello.txt"))
			    options:NSFileWrapperReadingImmediate
			      error:NULL];

		fn_make_file(fn_path(@"source/hello.txt"), "changed!");
		{
			NSData *lazyBytes = [lazy regularFileContents];
			NSData *eagerBytes = [eager regularFileContents];
			NSString *lazyText = [[NSString alloc] initWithData:lazyBytes
								  encoding:NSUTF8StringEncoding];
			NSString *eagerText = [[NSString alloc] initWithData:eagerBytes
								   encoding:NSUTF8StringEncoding];

			check("fw-the-reading-option-is-observable",
			      [lazyText isEqualToString:@"changed!"] && [eagerText isEqualToString:@"hello"],
			      [NSString stringWithFormat:@"lazy='%@' immediate='%@'", lazyText, eagerText]);
		}
	}

	{
		/* A DIRECTORY OF WRAPPERS WITH NO LANDING PLACE, so the two uniqueness checks have one. */
		NSFileWrapper *directory = [[NSFileWrapper alloc] initDirectoryWithFileWrappers:nil];
		NSFileWrapper *first = [[NSFileWrapper alloc]
			initRegularFileWithContents:fn_bytes(@"1")];
		NSFileWrapper *second = [[NSFileWrapper alloc]
			initRegularFileWithContents:fn_bytes(@"2")];
		NSString *firstKey;
		NSString *secondKey;
		NSString *found;
		BOOL raised = NO;

		[first setPreferredFilename:@"photo.jpg"];
		[second setPreferredFilename:@"photo.jpg"];
		firstKey = [directory addFileWrapper:first];
		secondKey = [directory addFileWrapper:second];
		found = [directory keyForChildFileWrapper:second];
		check("fw-add-file-wrapper-answers-a-unique-key",
		      [firstKey isEqualToString:@"photo.jpg"] &&
		      ![secondKey isEqualToString:firstKey] && [secondKey hasPrefix:@"photo.jpg"] &&
		      [found isEqualToString:secondKey] && [[directory fileWrappers] count] == 2,
		      [NSString stringWithFormat:@"first=%@ second=%@ found=%@ count=%lu", firstKey,
			secondKey, found, (unsigned long)[[directory fileWrappers] count]]);

		[directory removeFileWrapper:first];
		check("fw-a-second-child-with-one-name-gets-another-key",
		      [[directory fileWrappers] count] == 1 &&
		      [directory keyForChildFileWrapper:first] == nil &&
		      [directory keyForChildFileWrapper:second] != nil,
		      [NSString stringWithFormat:@"count=%lu first=%@",
			(unsigned long)[[directory fileWrappers] count],
			[directory keyForChildFileWrapper:first]]);

		@try {
			[second addFileWrapper:first];
		}
		@catch (NSException *exception) {
			raised = YES;
		}
		check("fw-adding-to-a-non-directory-raises", raised,
		      raised ? @"a regular-file wrapper refused a child by raising"
			     : @"adding a child to a file wrapper was allowed quietly");
	}

	{
		/* TWO OF APPLE'S WRITING RULES IN ONE LEG. The first: -filename is nil until a write ASKS for
		 * the name updating ("descendant file wrappers' properties are set if the writing succeeds").
		 * The second: an atomic tree write lands whole and leaves its temporary name behind for nobody. */
		NSFileWrapper *directory = [[NSFileWrapper alloc] initDirectoryWithFileWrappers:nil];
		NSFileWrapper *child = [[NSFileWrapper alloc]
			initRegularFileWithContents:fn_bytes(@"atomic")];
		NSString *target = fn_path(@"atomic-dir");
		BOOL wrote;
		NSArray *leftovers;

		[child setPreferredFilename:@"inside.txt"];
		[directory addFileWrapper:child];
		check("fw-a-new-wrapper-has-no-filename-yet", [child filename] == nil,
		      [NSString stringWithFormat:@"filename=%@", [child filename]]);

		wrote = [directory writeToURL:fn_url(target)
				      options:NSFileWrapperWritingAtomic | NSFileWrapperWritingWithNameUpdating
			  originalContentsURL:nil
					error:NULL];
		leftovers = [manager contentsOfDirectoryAtPath:@PROBE_ROOT error:NULL];
		check("fw-atomic-writing-leaves-no-temporary",
		      wrote && [[child filename] isEqualToString:@"inside.txt"] &&
		      [manager fileExistsAtPath:fn_path(@"atomic-dir/inside.txt")] &&
		      [fn_text(fn_path(@"atomic-dir/inside.txt")) isEqualToString:@"atomic"] &&
		      ![leftovers containsObject:@"atomic-dir.fnw-tmp-0"] &&
		      ![leftovers containsObject:[NSString stringWithFormat:@"atomic-dir.fnw-tmp-%d",
						(int)getpid()]],
		      [NSString stringWithFormat:@"wrote=%d child-name=%@ leftovers=%@", (int)wrote,
			[child filename], [leftovers componentsJoinedByString:@" "]]);
	}

	{
		/* AND THE HARD LINK: -writeToURL:'s originalContentsURL is for NOT REWRITING what did not
		 * change, so a child whose bytes equal the original's is LINKED into place (the same inode)
		 * while a child whose bytes moved is COPIED (a new one). Two files, two answers, one call. */
		NSFileWrapper *tree = [[NSFileWrapper alloc] initWithPath:source];
		NSFileWrapper *untouched = [[tree fileWrappers] objectForKey:@"hello.txt"];
		NSFileWrapper *changed = [tree fileWrappers];
		NSString *target = fn_path(@"linked");
		struct stat originalStat;
		struct stat untouchedStat;
		struct stat changedStat;
		BOOL wrote;

		/* ONE CHILD'S BYTES ARE REPLACED WITH DIFFERENT ONES, so the write has something to copy. */
		[untouched setPreferredFilename:@"hello.txt"];
		changed = [[NSFileWrapper alloc] initRegularFileWithContents:
				fn_bytes(@"different")];
		[changed setPreferredFilename:@"changed.txt"];
		[tree addFileWrapper:changed];
		wrote = [tree writeToURL:fn_url(target)
				 options:0
		     originalContentsURL:fn_url(source)
				  error:NULL];
		check("fw-unchanged-contents-are-linked-and-changed-ones-copied",
		      wrote &&
		      stat([fn_path(@"source/hello.txt") UTF8String], &originalStat) == 0 &&
		      stat([fn_path(@"linked/hello.txt") UTF8String], &untouchedStat) == 0 &&
		      stat([fn_path(@"linked/changed.txt") UTF8String], &changedStat) == 0 &&
		      originalStat.st_ino == untouchedStat.st_ino &&
		      changedStat.st_ino != originalStat.st_ino &&
		      [fn_text(fn_path(@"linked/changed.txt")) isEqualToString:@"different"],
		      [NSString stringWithFormat:@"wrote=%d inodes source=%llu unchanged=%llu changed=%llu",
			(int)wrote, (unsigned long long)originalStat.st_ino,
			(unsigned long long)untouchedStat.st_ino,
			(unsigned long long)changedStat.st_ino]);
	}

	{
		/* ---- W8 SLICE 4b: THE WHOLE TREE AS DATA, AND BACK --------------------------------------- */
		NSFileWrapper *tree = [[NSFileWrapper alloc] initWithPath:source];
		NSData *representation = [tree serializedRepresentation];
		NSFileWrapper *rebuilt = representation != nil ?
			[[NSFileWrapper alloc] initWithSerializedRepresentation:representation] : nil;
		NSDictionary *children = [rebuilt fileWrappers];
		id plain = representation != nil ?
			[NSPropertyListSerialization propertyListWithData:representation
								  options:0
								   format:NULL
								    error:NULL] : nil;

		/* THE FORM IS APPLE'S AND THE SCHEMA IS OURS: the data IS a property list (which is the half
		 * Apple's own page states - "the format used by the NSFileWrapper pasteboard type"), and the
		 * tree survives a round trip through it node for node. */
		check("fw-serialization-is-a-property-list",
		      representation != nil && [representation length] > 0 &&
		      [plain isKindOfClass:[NSDictionary class]],
		      [NSString stringWithFormat:@"data=%lu bytes plist=%s",
			(unsigned long)(representation != nil ? [representation length] : 0),
			[plain isKindOfClass:[NSDictionary class]] ? "yes" : "no"]);

		check("fw-serialization-round-trips-the-tree",
		      rebuilt != nil && [rebuilt isDirectory] && [children count] == 4 &&
		      [[(NSFileWrapper *)[children objectForKey:@"hello.txt"] regularFileContents]
			isEqualToData:[NSData dataWithBytes:"changed!" length:8]] &&
		      [[children objectForKey:@"nested"] isDirectory] &&
		      [[children objectForKey:@"link-rel"] isSymbolicLink] &&
		      [[[children objectForKey:@"link-rel"] symbolicLinkDestination]
			isEqualToString:@"hello.txt"] &&
		      [[[children objectForKey:@"dangling"] symbolicLinkDestination]
			isEqualToString:fn_path(@"nowhere-at-all")],
		      [NSString stringWithFormat:@"rebuilt=%d children=%lu hello=%@ link=%@",
			(int)(rebuilt != nil), (unsigned long)(children != nil ? [children count] : 0),
			[[children objectForKey:@"hello.txt"] regularFileContents] != nil ? @"data" : @"nil",
			[[children objectForKey:@"link-rel"] symbolicLinkDestination]]);
	}

	{
		/* AND THE REFUSALS, WHICH ARE THE HALF THAT KEEPS A DAMAGED DOCUMENT FROM BECOMING A DAMAGED
		 * OBJECT: bytes that are not a plist, a plist that is not a dictionary, and a dictionary whose
		 * Type is missing or unknown all answer nil. */
		NSData *garbage = [@"this is not a property list at all" dataUsingEncoding:NSUTF8StringEncoding];
		NSData *array = [NSPropertyListSerialization dataWithPropertyList:
					[NSArray arrayWithObject:@"no"]
								format:NSPropertyListXMLFormat_v1_0
							       options:0
								 error:NULL];
		NSData *typeless = [NSPropertyListSerialization dataWithPropertyList:
					[NSDictionary dictionaryWithObject:@"x" forKey:@"Something"]
								format:NSPropertyListXMLFormat_v1_0
							       options:0
								 error:NULL];

		check("fw-serialization-refuses-what-is-not-ours",
		      [[NSFileWrapper alloc] initWithSerializedRepresentation:garbage] == nil &&
		      [[NSFileWrapper alloc] initWithSerializedRepresentation:array] == nil &&
		      [[NSFileWrapper alloc] initWithSerializedRepresentation:typeless] == nil &&
		      [[NSFileWrapper alloc] initWithSerializedRepresentation:nil] == nil,
		      @"bytes that are not a plist, a plist that is not a dictionary, a dictionary with no Type "
		      @"and nil are all refused");
	}

	{
		/* AND THE FAILURE APPLE NAMES ITSELF: -serializedRepresentation "may be nil if the user modifies
		 * the contents of the file system node after you call ... but before -serializedRepresentation
		 * has read the contents of the file" - which is exactly the LAZY wrapper whose file has gone. */
		NSFileWrapper *lazy = [[NSFileWrapper alloc] initWithPath:fn_path(@"source/nested/deep.txt")];

		[manager removeItemAtPath:fn_path(@"source/nested/deep.txt") error:NULL];
		check("fw-serialization-is-nil-when-the-bytes-are-gone",
		      [lazy serializedRepresentation] == nil,
		      @"a lazy wrapper whose file was deleted serializes to nil, which is Apple's own sentence");
	}

	{
		/* ---- W8 SLICE 4c: THE SAME TREE THROUGH THE CODER --------------------------------------- */
		NSFileWrapper *codedTree = [[NSFileWrapper alloc] initWithPath:source];
		id archive = [NSKeyedArchiver archivedDataWithRootObject:codedTree];
		id decoded = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;
		NSDictionary *codedChildren = [decoded fileWrappers];

		/* THREE THINGS AT ONCE, AND THEY BELONG TOGETHER: Apple lists this class under NSCoding AND
		 * NSSecureCoding (so the declaration answers and the class method says YES), and the tree
		 * survives the coder exactly as it survives the plist. */
		check("fw-coding-round-trips-the-tree",
		      [NSFileWrapper supportsSecureCoding] &&
		      [codedTree conformsToProtocol:@protocol(NSSecureCoding)] &&
		      decoded != nil && [decoded isDirectory] && [codedChildren count] == 4 &&
		      [[(NSFileWrapper *)[codedChildren objectForKey:@"hello.txt"] regularFileContents]
			isEqualToData:[NSData dataWithBytes:"changed!" length:8]] &&
		      [[codedChildren objectForKey:@"nested"] isDirectory] &&
		      [[codedChildren objectForKey:@"link-rel"] isSymbolicLink] &&
		      [[[codedChildren objectForKey:@"link-rel"] symbolicLinkDestination]
			isEqualToString:@"hello.txt"] &&
		      [[[codedChildren objectForKey:@"dangling"] symbolicLinkDestination]
			isEqualToString:fn_path(@"nowhere-at-all")],
		      [NSString stringWithFormat:@"secure=%d decoded=%d children=%lu hello=%@", 
			(int)[NSFileWrapper supportsSecureCoding], (int)(decoded != nil),
			(unsigned long)(codedChildren != nil ? [codedChildren count] : 0),
			[[codedChildren objectForKey:@"hello.txt"] regularFileContents] != nil ? @"data" : @"nil"]);
	}

	{
		/* AND THE ONE POINT WHERE THE TWO TRANSPORTS DIFFER, ARGUED RATHER THAN ACCIDENTAL: a regular
		 * file whose bytes cannot be read makes -serializedRepresentation answer NIL (Apple's own
		 * sentence about the lazy form) while the coder WRITES THE NIL IT WAS GIVEN. The fixture is made
		 * here rather than borrowed, because a check that depends on another leg's file is a check that
		 * depends on the order of two legs - a lesson this probe has already paid for once. */
		NSFileWrapper *lazy;
		id archive;
		id decoded;

		fn_make_file(fn_path(@"gone.txt"), "soon");
		lazy = [[NSFileWrapper alloc] initWithPath:fn_path(@"gone.txt")];
		[manager removeItemAtPath:fn_path(@"gone.txt") error:NULL];
		archive = [NSKeyedArchiver archivedDataWithRootObject:lazy];
		decoded = archive != nil ? [NSKeyedUnarchiver unarchiveObjectWithData:archive] : nil;
		check("fw-coding-writes-what-it-was-given",
		      archive != nil && decoded != nil && [decoded isRegularFile] &&
		      [decoded regularFileContents] == nil && [lazy serializedRepresentation] == nil,
		      [NSString stringWithFormat:@"archive=%lu bytes decoded=%d contents=%s plist=%s",
			(unsigned long)(archive != nil ? [archive length] : 0), (int)(decoded != nil),
			[decoded regularFileContents] == nil ? "nil" : "data",
			[lazy serializedRepresentation] == nil ? "nil" : "data"]);
	}

	{
		NSError *cleanupError = nil;
		BOOL removed = [manager removeItemAtPath:@PROBE_ROOT error:&cleanupError];

		check("probe-tree-removed", removed && ![manager fileExistsAtPath:@PROBE_ROOT],
		      cleanupError != nil ? [cleanupError localizedDescription] : @"the tree is still there");
	}

	printf("FOUNDATION-FILEWRAPPER RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEWRAPPER DONE\n");
	return failc == 0 ? 0 : 1;
}
