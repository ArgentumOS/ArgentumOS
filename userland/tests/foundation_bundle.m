/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_bundle — NSBundle against REAL fixtures in the image: a Contents/ bundle, a flat one, a plain
 * directory that must be rejected, and the loaded program's own mainBundle.
 *
 * THE FIXTURE'S Info.plist IS A REAL PROPERTY LIST (the mk writes it with the same printf the system's own
 * *.conf files use), so what this probe exercises is the whole path: file -> plist parse -> Apple's keys.
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <objc/runtime.h>

/* THE FIXTURES ARE BUILT HERE, not shipped in the image: a Contents/ bundle, a flat bundle and a directory with
 * no manifest. The manifest is a REAL property list written as bytes - the same text the system's own *.conf
 * files carry - so what the probe exercises is file -> plist parse -> Apple's keys, with no test-only format. */
static const char *FN_ROOT = "/System/Temporary Files/fn_bundle_probe";

static void fn_write(const char *path, const char *text)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	if (fd >= 0) {
		(void)write(fd, text, strlen(text));
		close(fd);
	}
}

/* A BINARY COPY, so the fixture's executable is the very file the linker produced. */
static void fn_copy(const char *from, const char *to)
{
	char buffer[4096];
	int in = open(from, O_RDONLY);
	int out;
	ssize_t got;

	if (in < 0) {
		return;
	}
	out = open(to, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (out < 0) {
		close(in);
		return;
	}
	while ((got = read(in, buffer, sizeof buffer)) > 0) {
		(void)write(out, buffer, (size_t)got);
	}
	close(out);
	close(in);
}

static void fn_mkdirs(const char *path)
{
	char buffer[512];
	size_t i;

	snprintf(buffer, sizeof buffer, "%s", path);
	for (i = 1; i < strlen(buffer); i++) {
		if (buffer[i] == '/') {
			buffer[i] = '\0';
			(void)mkdir(buffer, 0755);
			buffer[i] = '/';
		}
	}
	(void)mkdir(buffer, 0755);
}

static const char *FN_CONTENTS_PLIST =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
	"\t<key>CFBundleIdentifier</key>\n\t<string>org.argentum.probe.fixture</string>\n"
	"\t<key>CFBundleName</key>\n\t<string>Bundle Fixture</string>\n"
	"\t<key>CFBundleExecutable</key>\n\t<string>foundation_bundle_payload.so</string>\n"
	"\t<key>NSPrincipalClass</key>\n\t<string>BundleFixturePrincipal</string>\n"
	"</dict>\n</plist>\n";

static const char *FN_FLAT_PLIST =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
	"\t<key>CFBundleIdentifier</key>\n\t<string>org.argentum.probe.flat</string>\n"
	"\t<key>CFBundleExecutable</key>\n\t<string>bundle_fixture</string>\n"
	"</dict>\n</plist>\n";

static void fn_build_fixtures(void)
{
	char path[512];

	fn_mkdirs(FN_ROOT);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/MacOS", FN_ROOT); fn_mkdirs(path);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/en.lproj", FN_ROOT); fn_mkdirs(path);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/fr.lproj", FN_ROOT); fn_mkdirs(path);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Info.plist", FN_ROOT); fn_write(path, FN_CONTENTS_PLIST);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/hello.txt", FN_ROOT); fn_write(path, "the fixture resource\n");
	/* THE PAYLOAD IS COPIED IN, BYTES AND ALL: the bundle's executable must BE the shared library the mk built
	 * (dlopen opens a real file), so a text placeholder cannot stand in for it any more. */
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/MacOS/foundation_bundle_payload.so", FN_ROOT);
	fn_copy("/System/Shared/tests/foundation_bundle_payload.so", path);
	snprintf(path, sizeof path, "%s/FlatFixture.app", FN_ROOT); fn_mkdirs(path);
	snprintf(path, sizeof path, "%s/FlatFixture.app/Info.plist", FN_ROOT); fn_write(path, FN_FLAT_PLIST);
	snprintf(path, sizeof path, "%s/FlatFixture.app/hello.txt", FN_ROOT); fn_write(path, "a flat bundle keeps its resources at its root\n");
	/* the flat bundle's EXECUTABLE is that text file: what "a payload that is not code" means here. */
	snprintf(path, sizeof path, "%s/FlatFixture.app/bundle_fixture", FN_ROOT); fn_write(path, "this is a text file, not an object file\n");
	snprintf(path, sizeof path, "%s/NotABundle", FN_ROOT); fn_mkdirs(path);
}

static int okc, failc;
static int fn_loaded_notification_seen;
static id fn_loaded_classes;

@interface FnBundleObserver : NSObject
- (void)bundleDidLoad:(NSNotification *)notification;
@end

@implementation FnBundleObserver
- (void)bundleDidLoad:(NSNotification *)notification
{
	fn_loaded_notification_seen = 1;
	fn_loaded_classes = [[notification userInfo] objectForKey:@"NSLoadedClasses"];
}
@end

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-BUNDLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-BUNDLE %s FAIL %s\n", name, detail ? detail : "");
	}
}

#define FIXTURES "/System/Temporary Files/fn_bundle_probe/"

int main(void)
{
	NSBundle *contents;
	NSBundle *flat;
	NSBundle *plain;

	fn_build_fixtures();
	contents = [NSBundle bundleWithPath:@FIXTURES "BundleFixture.app"];
	flat = [NSBundle bundleWithPath:@FIXTURES "FlatFixture.app"];
	plain = [NSBundle bundleWithPath:@FIXTURES "NotABundle"];

	/* A DIRECTORY WITH NO MANIFEST IS NOT A BUNDLE, and the contract is nil rather than an object that answers
	 * nothing - a caller learns the bundle's existence from "did I get one". */
	check("bundle-rejects-a-directory-with-no-manifest", plain == nil,
	      "a directory with no Info.plist answers nil");

	check("bundle-accepts-both-layouts",
	      contents != nil && flat != nil &&
	      [[contents bundlePath] hasSuffix:@"BundleFixture.app"] &&
	      [[flat bundlePath] hasSuffix:@"FlatFixture.app"],
	      "Contents/ and flat bundles both open, and each reports the path it was given");

	/* THE MANIFEST IS A PLIST, READ THROUGH NSPropertyListSerialization, AND THE KEYS ARE APPLE'S. */
	check("bundle-reads-its-plist-manifest",
	      [[contents bundleIdentifier] isEqualToString:@"org.argentum.probe.fixture"] &&
	      [[contents objectForInfoDictionaryKey:@"CFBundleName"] isEqualToString:@"Bundle Fixture"] &&
	      [[contents infoDictionary] count] > 0,
	      "CFBundleIdentifier and CFBundleName come back from the manifest");

	check("bundle-finds-its-executable",
	      [[contents executablePath] hasSuffix:@"Contents/MacOS/foundation_bundle_payload.so"] &&
	      [[flat executablePath] hasSuffix:@"FlatFixture.app/bundle_fixture"],
	      "the executable path follows the layout: Contents/MacOS for one, the bundle root for the other");

	/* RESOURCE LOOKUP: a hit, a MISS (which must be nil, not a path that nearly matches), and the plural form
	 * over the same directory. */
	{
		NSString *hit = [contents pathForResource:@"hello" ofType:@"txt"];
		NSString *miss = [contents pathForResource:@"no-such-file" ofType:@"txt"];
		NSArray *all = [contents pathsForResourcesOfType:@"txt" inDirectory:nil];

		check("bundle-resource-lookup",
		      hit != nil && [hit hasSuffix:@"Resources/hello.txt"] && miss == nil && [all count] == 1 &&
		      [[contents resourcePath] hasSuffix:@"Contents/Resources"],
		      "one resource found, one miss answered nil, and resourcePath points at Contents/Resources");
	}

	check("bundle-localizations-come-from-lproj-directories",
	      [[contents localizations] containsObject:@"en"] &&
	      [[contents localizations] containsObject:@"fr"],
	      "en and fr are read from the *.lproj directories in Resources");

	/* THE RUNNING PROGRAM'S OWN BUNDLE: this probe is NOT inside a bundle, so its mainBundle is the directory
	 * the executable lives in - which is Apple's answer for a command-line tool, and NOT nil. */
	{
		NSBundle *main = [NSBundle mainBundle];

		check("main-bundle-exists-even-for-a-plain-tool",
		      main != nil && [[main bundlePath] length] > 0,
		      "mainBundle is an object whose path is the executable's directory, per Apple");
	}

	check("bundle-by-identifier-searches-the-opened-ones",
	      [NSBundle bundleWithIdentifier:@"org.argentum.probe.fixture"] == contents &&
	      [NSBundle bundleWithIdentifier:@"org.argentum.no.such.bundle"] == nil,
	      "the identifier answers the opened bundle, and an unknown identifier answers nil");

	/* THE CODE-LOADING HALF, HONESTLY: the fixture's payload is a TEXT FILE, so dlopen must FAIL - and the
	 * check requires exactly that, plus that a bundle which was never loaded reports so and that asking for a
	 * principal class answers nil rather than crashing. THE POSITIVE PATH IS NOT ASSERTED HERE: proving -load
	 * loads real code needs a shared library as the fixture, which is named as the next step rather than
	 * pretended by this check. */
	check("bundle-load-refuses-a-payload-that-is-not-code",
	      ![flat isLoaded] && ![flat load] && ![flat isLoaded] && [flat principalClass] == nil,
	      "a flat bundle whose executable is a text file does not dlopen; isLoaded stays NO");
	/* THE POSITIVE LOAD PATH: dlopen a REAL shared library, then objc_getClass the NSPrincipalClass the
	 * manifest names - and the notification carrying NSLoadedClasses. An observer is registered FIRST, because
	 * a notification posted before the observer exists is a notification nobody sees. */
	{
		FnBundleObserver *observer = [[FnBundleObserver alloc] init];
		Class principal = nil;
		id instance = nil;
		id answer = nil;

		[[NSNotificationCenter defaultCenter] addObserver:observer
							 selector:@selector(bundleDidLoad:)
							     name:NSBundleDidLoadNotification
							   object:contents];
		check("bundle-load-brings-in-real-code",
		      [contents load] && [contents isLoaded],
		      "dlopen of the bundle's own shared library answers YES and isLoaded follows");
		principal = [contents principalClass];
		check("bundle-principal-class-comes-from-the-manifest",
		      principal != nil && strcmp(class_getName(principal), "BundleFixturePrincipal") == 0 &&
		      [(NSObject *)principal isKindOfClass:[NSObject class]],
		      "NSPrincipalClass names a class the loaded library defines");
		if (principal != nil) {
			instance = [[principal alloc] init];
			answer = [instance performSelector:@selector(fixtureAnswer)];
		}
		check("bundle-loaded-class-is-usable",
		      answer != nil && [answer isEqualToString:@"the payload answered"],
		      "an instance of the loaded class answers through the library's own code");
		check("bundle-load-posts-its-notification-with-the-classes",
		      fn_loaded_notification_seen && fn_loaded_classes != nil &&
		      [fn_loaded_classes containsObject:@"BundleFixturePrincipal"],
		      "NSBundleDidLoadNotification carried NSLoadedClasses naming the principal class");
	}

	{
		/* ONE PROPERTY, ONE CAUSE, AND NO dlopen VERDICT IN IT: earlier versions asked about a bundle that an
		 * earlier check had already asked to load, so their own precondition could change the answer. A FRESH
		 * OBJECT that has never been asked to load anything is the deterministic form. */
		NSBundle *freshBundle = [NSBundle bundleWithPath:@FIXTURES "FlatFixture.app"];

		check("bundle-unload-answers-no-when-nothing-was-loaded",
		      freshBundle != nil && ![freshBundle isLoaded] && ![freshBundle unload],
		      "a bundle never asked to load reports isLoaded NO and unload answers NO");
	}

	printf("FOUNDATION-BUNDLE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-BUNDLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-BUNDLE DONE\n");
	return failc ? 1 : 0;
}
