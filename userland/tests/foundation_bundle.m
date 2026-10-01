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
	"\t<key>CFBundleDevelopmentRegion</key>\n\t<string>en</string>\n"
	"</dict>\n</plist>\n";

static const char *FN_FLAT_PLIST =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
	"\t<key>CFBundleIdentifier</key>\n\t<string>org.argentum.probe.flat</string>\n"
	"\t<key>CFBundleExecutable</key>\n\t<string>bundle_fixture</string>\n"
	"</dict>\n</plist>\n";

/* A BUNDLE WHOSE EXECUTABLE IS NOT THERE: the error-reporting loading doors must fail on it and say why. */
static const char *FN_MISSING_PLIST =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
	"\t<key>CFBundleIdentifier</key>\n\t<string>org.argentum.probe.missing</string>\n"
	"\t<key>CFBundleExecutable</key>\n\t<string>not-here</string>\n"
	"</dict>\n</plist>\n";

/* THE TWO STRING TABLES the explicit-localization string door reads: one inside en.lproj, one at the resource
 * root, both real property lists (the reader this library ships is proved on that form). Two DIFFERENT values
 * for one key is what makes the localization hit and the flat fallback distinguishable. */
static const char *FN_EN_TABLE =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
	"\t<key>k</key>\n\t<string>en-value</string>\n"
	"</dict>\n</plist>\n";

static const char *FN_FLAT_TABLE =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n<dict>\n"
	"\t<key>k</key>\n\t<string>flat-value</string>\n"
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
	/* THE STANDARD SUBDIRECTORIES the directory doors name: PlugIns and Frameworks exist, SharedSupport does
	 * not, so the existence gate is exercised on both sides of one check. */
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/PlugIns", FN_ROOT); fn_mkdirs(path);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Frameworks", FN_ROOT); fn_mkdirs(path);
	/* A RESOURCE INSIDE en.lproj, so the localization-aware lookup has a hit in one localization and none in
	 * the other (fr), which is what makes the fallback observable. */
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/en.lproj/hello.txt", FN_ROOT); fn_write(path, "the english fixture resource\n");
	/* TWO STRING TABLES for the explicit-localization string door: one inside en.lproj, one at the root. */
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/en.lproj/T.strings", FN_ROOT); fn_write(path, FN_EN_TABLE);
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/T.strings", FN_ROOT); fn_write(path, FN_FLAT_TABLE);
	/* THE IMAGE AND SOUND RESOURCES the name-with-optional-extension doors look for: a .png asked for by its
	 * STEM ("pic"), a .png asked for by its FULL NAME ("logo.png"), and a .wav. The CONTENT is irrelevant --
	 * the doors answer PATHS, not decoded images -- so a tiny placeholder stands in for the bytes. */
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/pic.png", FN_ROOT); fn_write(path, "png bytes\n");
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/logo.png", FN_ROOT); fn_write(path, "png bytes\n");
	snprintf(path, sizeof path, "%s/BundleFixture.app/Contents/Resources/beep.wav", FN_ROOT); fn_write(path, "wav bytes\n");
	/* A BUNDLE WHOSE EXECUTABLE IS ABSENT: the error-reporting loading doors must fail and say why. */
	snprintf(path, sizeof path, "%s/MissingExe.app", FN_ROOT); fn_mkdirs(path);
	snprintf(path, sizeof path, "%s/MissingExe.app/Info.plist", FN_ROOT); fn_write(path, FN_MISSING_PLIST);
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

	/* ---- THE URL AND STANDARD-DIRECTORY DOORS. Each is built on the path vocabulary the checks above proved,
	 * so a pass here is a pass of the URL/directory half in particular. ---- */
	{
		NSURL *contentsURL = [NSURL fileURLWithPath:@FIXTURES "BundleFixture.app"];
		NSURL *notBundleURL = [NSURL fileURLWithPath:@FIXTURES "NotABundle"];
		NSURL *helloURL = [contents URLForResource:@"hello" withExtension:@"txt"];
		NSArray *helloURLs = [contents URLsForResourcesWithExtension:@"txt" subdirectory:nil];
		NSURL *byBundleURL = [NSBundle URLForResource:@"hello" withExtension:@"txt"
						subdirectory:nil inBundleWithURL:contentsURL];
		/* The nullable path doors come back into LOCALS whose types carry no nullability: isEqualToString:
		 * wants a nonnull string and these doors may answer nil, so binding them first keeps both honest. */
		NSString *bundlePath = [contents bundlePath];
		NSString *resourcePath = [contents resourcePath];
		NSString *executablePath = [contents executablePath];
		NSString *flatHello = [contents pathForResource:@"hello" ofType:@"txt"];

		check("bundle-urls-follow-their-paths",
		      [[[contents bundleURL] path] isEqualToString:bundlePath] &&
		      [[[contents resourceURL] path] isEqualToString:resourcePath] &&
		      [[[contents executableURL] path] isEqualToString:executablePath],
		      "3 URLs (bundle, resource, executable) each equal their path");

		check("resource-urls-mirror-the-path-lookups",
		      [[helloURL path] isEqualToString:flatHello] &&
		      [helloURLs count] == 1 &&
		      [[[helloURLs objectAtIndex:0] path] isEqualToString:[helloURL path]],
		      "1 URL equals its 1 path, and the plural door returns 1 of 1 file");

		check("resource-url-in-bundle-with-url",
		      byBundleURL != nil && [[byBundleURL path] hasSuffix:@"Resources/hello.txt"] &&
		      [NSBundle URLForResource:@"hello" withExtension:@"txt" subdirectory:nil
				      inBundleWithURL:notBundleURL] == nil,
		      "1 of 2 bundle URLs finds the resource; the non-bundle finds 0");

		check("bundle-from-url-and-init-with-url",
		      [[[NSBundle bundleWithURL:contentsURL] bundlePath] isEqualToString:[contents bundlePath]] &&
		      [[[[NSBundle alloc] initWithURL:contentsURL] bundlePath] isEqualToString:[contents bundlePath]] &&
		      [NSBundle bundleWithURL:notBundleURL] == nil,
		      "2 of 3 URLs open a bundle (bundleWithURL:, initWithURL:); 1 is not a bundle");
	}

	{
		NSString *plugInsPath = [contents builtInPlugInsPath];

		check("standard-bundle-directories-are-existence-gated",
		      [plugInsPath hasSuffix:@"Contents/PlugIns"] &&
		      [[contents privateFrameworksPath] hasSuffix:@"Contents/Frameworks"] &&
		      [contents sharedSupportPath] == nil &&
		      [[[contents builtInPlugInsURL] path] isEqualToString:plugInsPath],
		      "2 of 3 directories present (PlugIns, Frameworks), 1 absent (SharedSupport)");
	}

	{
		NSString *auxPath = [contents pathForAuxiliaryExecutable:@"foundation_bundle_payload.so"];

		check("auxiliary-executable-path-and-url",
		      [auxPath hasSuffix:@"Contents/MacOS/foundation_bundle_payload.so"] &&
		      [contents pathForAuxiliaryExecutable:@"nope"] == nil &&
		      [[[contents URLForAuxiliaryExecutable:@"foundation_bundle_payload.so"] path]
			isEqualToString:auxPath],
		      "1 executable found, 1 missing answers nil, and its 1 URL mirrors the path");
	}

	check("class-resource-door-searches-the-main-bundle",
	      [contents pathForResource:@"hello" ofType:@"txt"] != nil &&
	      [NSBundle pathForResource:@"hello" ofType:@"txt" inDirectory:nil] ==
		[[NSBundle mainBundle] pathForResource:@"hello" ofType:@"txt" inDirectory:nil] &&
	      [[NSBundle pathsForResourcesOfType:@"txt" inDirectory:nil] count] ==
		[[[NSBundle mainBundle] pathsForResourcesOfType:@"txt" inDirectory:nil] count],
	      "the 2 class doors answer their mainBundle; the fixture holds the 1 file");

	check("localization-aware-resource-lookup",
	      [[contents pathForResource:@"hello" ofType:@"txt" inDirectory:nil forLocalization:@"en"]
		hasSuffix:@"en.lproj/hello.txt"] &&
	      [[contents pathForResource:@"hello" ofType:@"txt" inDirectory:nil forLocalization:@"fr"]
		hasSuffix:@"Resources/hello.txt"] &&
	      [[contents pathsForResourcesOfType:@"txt" inDirectory:nil forLocalization:@"en"] count] == 1,
	      "en finds 1 in en.lproj; fr falls back to the 1 flat file; the plural door returns 1");

	check("localized-string-over-explicit-localizations",
	      [[contents localizedStringForKey:@"k" value:nil table:@"T" localizations:@[@"en"]]
		isEqualToString:@"en-value"] &&
	      [[contents localizedStringForKey:@"k" value:nil table:@"T" localizations:@[@"de"]]
		isEqualToString:@"flat-value"],
	      "1 of 2 localizations picks its own table (en), the other falls back (de) to 1 flat value");

	check("preferred-localizations-match-preferences",
	      [[NSBundle preferredLocalizationsFromArray:@[@"en", @"fr", @"de"] forPreferences:@[@"fr"]]
		isEqualToArray:@[@"fr"]] &&
	      [[NSBundle preferredLocalizationsFromArray:@[@"en", @"fr"] forPreferences:@[@"es"]]
		isEqualToArray:@[@"en", @"fr"]],
	      "1 of 3 available matches 1 preference; 0 matches leaves the 2-element list unchanged");

	check("development-localization-and-localized-info",
	      [[contents developmentLocalization] isEqualToString:@"en"] &&
	      [[[contents localizedInfoDictionary] objectForKey:@"CFBundleName"] isEqualToString:@"Bundle Fixture"] &&
	      [[contents preferredLocalizations] count] >= 1,
	      "1 development region, 1 manifest value, and >=1 preferred localization");

	{
		/* THE IMAGE AND SOUND DOORS. The name's extension is OPTIONAL: "pic" finds pic.png (the extension is
		 * appended), and "logo.png" finds itself (the literal name wins). A miss is nil. The URL door mirrors
		 * the path door, the way every other URL door in this file does. */
		NSString *byStem = [contents pathForImageResource:@"pic"];
		NSString *byFullName = [contents pathForImageResource:@"logo.png"];
		NSString *sound = [contents pathForSoundResource:@"beep"];
		NSURL *imageURL = [contents URLForImageResource:@"pic"];

		check("bundle-finds-image-and-sound-resources-by-name",
		      byStem != nil && [byStem hasSuffix:@"Resources/pic.png"] &&
		      byFullName != nil && [byFullName hasSuffix:@"Resources/logo.png"] &&
		      sound != nil && [sound hasSuffix:@"Resources/beep.wav"] &&
		      [contents pathForImageResource:@"no-such-image"] == nil &&
		      [[imageURL path] isEqualToString:byStem],
		      "2 of 3 image lookups hit (stem pic.png, full name logo.png), 1 sound (beep.wav), 1 miss nil, "
		      "and 1 URL == its path");
	}

	{
		/* THE EXECUTABLE'S ARCHITECTURE, read from the ELF header of the bundle's executable. The Contents
		 * bundle's executable is a REAL shared object (an x86-64 ELF, whose e_machine is EM_X86_64 == 62),
		 * so it answers exactly one code; the flat bundle's executable is a TEXT file, so it is not an ELF and
		 * answers nil -- Apple's "no Mach-O executable" case. */
		NSArray *arch = [contents executableArchitectures];
		NSArray *flatArch = [flat executableArchitectures];

		check("bundle-executable-architectures-from-the-elf-header",
		      arch != nil && [arch count] == 1 &&
		      [[arch objectAtIndex:0] isKindOfClass:[NSNumber class]] &&
		      [(NSNumber *)[arch objectAtIndex:0] intValue] == NSBundleExecutableArchitectureX86_64 &&
		      flatArch == nil,
		      "1 architecture code read from the payload's ELF header (EM_X86_64), and the text-file "
		      "executable answers nil (0 codes)");
	}

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
		/* -classNamed: MUST ANSWER A CLASS THE BUNDLE PROVIDES, and +bundleForClass: the bundle that provided
		 * it: the class the manifest named comes back, a name the runtime does not know comes back nil, and a
		 * class OUTSIDE the bundle (NSObject, in the library's own image) has no providing fixture bundle. */
		check("class-lookup-and-bundle-for-class",
		      [contents classNamed:@"BundleFixturePrincipal"] == principal &&
		      [contents classNamed:@"no.such.Class"] == nil &&
		      [NSBundle bundleForClass:principal] == contents &&
		      [NSBundle bundleForClass:[NSObject class]] == nil,
		      "1 of 2 names answered by -classNamed:, and the providing bundle is 1 of 1");
		check("bundle-load-posts-its-notification-with-the-classes",
		      fn_loaded_notification_seen && fn_loaded_classes != nil &&
		      [fn_loaded_classes containsObject:@"BundleFixturePrincipal"],
		      "NSBundleDidLoadNotification carried NSLoadedClasses naming the principal class");
	}

	/* THE ERROR-REPORTING LOADING DOORS. flat's executable is a text file that EXISTS, so -preflightAndReturnError:
	 * is YES (a readable file) while -loadAndReturnError: is NO with an NSError (dlopen refuses it); a bundle
	 * whose executable is ABSENT fails BOTH, each still carrying an NSError. */
	{
		NSBundle *missing = [NSBundle bundleWithPath:@FIXTURES "MissingExe.app"];
		NSError *err = nil;
		BOOL preflight = [flat preflightAndReturnError:&err];
		BOOL loaded = [flat loadAndReturnError:&err];

		check("loading-doors-report-their-error",
		      missing != nil && preflight && !loaded && err != nil &&
		      ![missing preflightAndReturnError:&err] && err != nil &&
		      ![missing loadAndReturnError:&err] && err != nil,
		      "3 error paths (flat preflight read-yes, flat load-no, missing exe) each carry 1 NSError");
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
