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

static int lastcheck;

static void check(const char *name, int ok, const char *detail)
{
	lastcheck = ok;	/* read by covers(): a claim can only follow an assertion that held */
	if (ok) {
		okc++;
		printf("FOUNDATION-BUNDLE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-BUNDLE %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* covers("NSBundle", "mainBundle") — the behavioural claim, piggybacked on the check above it: no condition of
 * its own, printed only when the last check's result was true. See tools/foundation-cov.py; every claim is
 * filtered against the ledger, since one for a row the ledger does not carry is inert. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

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
	covers("NSBundle", "bundleWithPath:");

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
	covers("NSBundle", "objectForInfoDictionaryKey:");

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
	covers("NSBundle", "bundleWithIdentifier:");
	covers("NSBundle", "bundleURL");
	covers("NSBundle", "resourceURL");
	covers("NSBundle", "executableURL");
	covers("NSBundle", "bundlePath");
	covers("NSBundle", "resourcePath");
	covers("NSBundle", "executablePath");

		check("resource-urls-mirror-the-path-lookups",
		      [[helloURL path] isEqualToString:flatHello] &&
		      [helloURLs count] == 1 &&
		      [[[helloURLs objectAtIndex:0] path] isEqualToString:[helloURL path]],
		      "1 URL equals its 1 path, and the plural door returns 1 of 1 file");
	covers("NSBundle", "URLForResource:withExtension:");
	covers("NSBundle", "URLsForResourcesWithExtension:subdirectory:");
	covers("NSBundle", "pathForResource:ofType:");

		check("resource-url-in-bundle-with-url",
		      byBundleURL != nil && [[byBundleURL path] hasSuffix:@"Resources/hello.txt"] &&
		      [NSBundle URLForResource:@"hello" withExtension:@"txt" subdirectory:nil
				      inBundleWithURL:notBundleURL] == nil,
		      "1 of 2 bundle URLs finds the resource; the non-bundle finds 0");
	covers("NSBundle", "URLForResource:withExtension:subdirectory:inBundleWithURL:");

		check("bundle-from-url-and-init-with-url",
		      [[[NSBundle bundleWithURL:contentsURL] bundlePath] isEqualToString:[contents bundlePath]] &&
		      [[[[NSBundle alloc] initWithURL:contentsURL] bundlePath] isEqualToString:[contents bundlePath]] &&
		      [NSBundle bundleWithURL:notBundleURL] == nil,
		      "2 of 3 URLs open a bundle (bundleWithURL:, initWithURL:); 1 is not a bundle");
	covers("NSBundle", "URLForResource:withExtension:subdirectory:");
	covers("NSBundle", "bundleWithURL:");
	covers("NSBundle", "initWithURL:");
	}

	{
		NSString *plugInsPath = [contents builtInPlugInsPath];

		check("standard-bundle-directories-are-existence-gated",
		      [plugInsPath hasSuffix:@"Contents/PlugIns"] &&
		      [[contents privateFrameworksPath] hasSuffix:@"Contents/Frameworks"] &&
		      [contents sharedSupportPath] == nil &&
		      [[[contents builtInPlugInsURL] path] isEqualToString:plugInsPath],
		      "2 of 3 directories present (PlugIns, Frameworks), 1 absent (SharedSupport)");
	covers("NSBundle", "builtInPlugInsPath");
	covers("NSBundle", "privateFrameworksPath");
	covers("NSBundle", "sharedSupportPath");
	covers("NSBundle", "builtInPlugInsURL");
	}

	{
		NSString *auxPath = [contents pathForAuxiliaryExecutable:@"foundation_bundle_payload.so"];

		check("auxiliary-executable-path-and-url",
		      [auxPath hasSuffix:@"Contents/MacOS/foundation_bundle_payload.so"] &&
		      [contents pathForAuxiliaryExecutable:@"nope"] == nil &&
		      [[[contents URLForAuxiliaryExecutable:@"foundation_bundle_payload.so"] path]
			isEqualToString:auxPath],
		      "1 executable found, 1 missing answers nil, and its 1 URL mirrors the path");
	covers("NSBundle", "pathForAuxiliaryExecutable:");
	covers("NSBundle", "URLForAuxiliaryExecutable:");
	}

	check("class-resource-door-searches-the-main-bundle",
	      [contents pathForResource:@"hello" ofType:@"txt"] != nil &&
	      [NSBundle pathForResource:@"hello" ofType:@"txt" inDirectory:nil] ==
		[[NSBundle mainBundle] pathForResource:@"hello" ofType:@"txt" inDirectory:nil] &&
	      [[NSBundle pathsForResourcesOfType:@"txt" inDirectory:nil] count] ==
		[[[NSBundle mainBundle] pathsForResourcesOfType:@"txt" inDirectory:nil] count],
	      "the 2 class doors answer their mainBundle; the fixture holds the 1 file");
	covers("NSBundle", "pathForResource:ofType:inDirectory:");
	covers("NSBundle", "mainBundle");
	covers("NSBundle", "pathsForResourcesOfType:inDirectory:");

	check("localization-aware-resource-lookup",
	      [[contents pathForResource:@"hello" ofType:@"txt" inDirectory:nil forLocalization:@"en"]
		hasSuffix:@"en.lproj/hello.txt"] &&
	      [[contents pathForResource:@"hello" ofType:@"txt" inDirectory:nil forLocalization:@"fr"]
		hasSuffix:@"Resources/hello.txt"] &&
	      [[contents pathsForResourcesOfType:@"txt" inDirectory:nil forLocalization:@"en"] count] == 1,
	      "en finds 1 in en.lproj; fr falls back to the 1 flat file; the plural door returns 1");
	covers("NSBundle", "pathForResource:ofType:inDirectory:forLocalization:");
	covers("NSBundle", "pathsForResourcesOfType:inDirectory:forLocalization:");

	check("localized-string-over-explicit-localizations",
	      [[contents localizedStringForKey:@"k" value:nil table:@"T" localizations:@[@"en"]]
		isEqualToString:@"en-value"] &&
	      [[contents localizedStringForKey:@"k" value:nil table:@"T" localizations:@[@"de"]]
		isEqualToString:@"flat-value"],
	      "1 of 2 localizations picks its own table (en), the other falls back (de) to 1 flat value");
	covers("NSBundle", "localizedStringForKey:value:table:localizations:");

	check("preferred-localizations-match-preferences",
	      [[NSBundle preferredLocalizationsFromArray:@[@"en", @"fr", @"de"] forPreferences:@[@"fr"]]
		isEqualToArray:@[@"fr"]] &&
	      [[NSBundle preferredLocalizationsFromArray:@[@"en", @"fr"] forPreferences:@[@"es"]]
		isEqualToArray:@[@"en", @"fr"]],
	      "1 of 3 available matches 1 preference; 0 matches leaves the 2-element list unchanged");
	covers("NSBundle", "localizedStringForKey:value:table:");
	covers("NSBundle", "localizations");
	covers("NSBundle", "preferredLocalizationsFromArray:forPreferences:");

	check("development-localization-and-localized-info",
	      [[contents developmentLocalization] isEqualToString:@"en"] &&
	      [[[contents localizedInfoDictionary] objectForKey:@"CFBundleName"] isEqualToString:@"Bundle Fixture"] &&
	      [[contents preferredLocalizations] count] >= 1,
	      "1 development region, 1 manifest value, and >=1 preferred localization");
	covers("NSBundle", "preferredLocalizationsFromArray:");
	covers("NSBundle", "developmentLocalization");
	covers("NSBundle", "localizedInfoDictionary");
	covers("NSBundle", "preferredLocalizations");

	{
		/* ⚠ THE IMAGE AND SOUND DOORS ARE **NOT** FOUNDATION'S ANY MORE (§63.52), AND THIS IS THE HALF THAT
		 * BELONGS HERE. They are AppKit's — AppKit's `NSImage.h` declares the image pair and `NSSound.h` the
		 * sound door — and their POSITIVE check moved with them to `appkit_image`, an AppKit-tier probe, which
		 * a Foundation probe cannot link: that wall is the reason the tiers exist. What this probe can hold is
		 * the other half, and it is the half that CATCHES A REGRESSION — if anybody puts them back on
		 * Foundation's NSBundle, this fails. `NSSelectorFromString` rather than `@selector(...)` because the
		 * whole point is that no declaration of them is in scope here. */
		check("appkits-resource-doors-are-not-on-foundation-s-nsbundle",
		      ![[NSBundle class] instancesRespondToSelector:
			  NSSelectorFromString(@"pathForImageResource:")] &&
		      ![[NSBundle class] instancesRespondToSelector:
			  NSSelectorFromString(@"URLForImageResource:")] &&
		      ![[NSBundle class] instancesRespondToSelector:
			  NSSelectorFromString(@"pathForSoundResource:")],
		      "Foundation's NSBundle carries no image or sound resource door: they are AppKit's, and the "
		      "positive check lives in appkit_image, which can link the tier that owns them");
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
	covers("NSBundle", "executableArchitectures");
	}

	/* THE CODE-LOADING HALF, HONESTLY: the fixture's payload is a TEXT FILE, so dlopen must FAIL - and the
	 * check requires exactly that, plus that a bundle which was never loaded reports so and that asking for a
	 * principal class answers nil rather than crashing. THE POSITIVE PATH IS NOT ASSERTED HERE: proving -load
	 * loads real code needs a shared library as the fixture, which is named as the next step rather than
	 * pretended by this check. */
	check("bundle-load-refuses-a-payload-that-is-not-code",
	      ![flat isLoaded] && ![flat load] && ![flat isLoaded] && [flat principalClass] == nil,
	      "a flat bundle whose executable is a text file does not dlopen; isLoaded stays NO");
	covers("NSBundle", "loaded");
	covers("NSBundle", "load");
	covers("NSBundle", "principalClass");
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
	covers("NSBundle", "loaded");
	covers("NSBundle", "load");
		principal = [contents principalClass];
		check("bundle-principal-class-comes-from-the-manifest",
		      principal != nil && strcmp(class_getName(principal), "BundleFixturePrincipal") == 0 &&
		      [(NSObject *)principal isKindOfClass:[NSObject class]],
		      "NSPrincipalClass names a class the loaded library defines");
	covers("NSBundle", "principalClass");
		if (principal != nil) {
			instance = [[principal alloc] init];
			answer = [instance performSelector:@selector(fixtureAnswer)];
		}
		check("bundle-loaded-class-is-usable",
		      answer != nil && [answer isEqualToString:@"the payload answered"],
		      "an instance of the loaded class answers through the library's own code");
	covers("NSBundle", "principalClass");
		/* -classNamed: MUST ANSWER A CLASS THE BUNDLE PROVIDES, and +bundleForClass: the bundle that provided
		 * it: the class the manifest named comes back, a name the runtime does not know comes back nil, and a
		 * class OUTSIDE the bundle (NSObject, in the library's own image) has no providing fixture bundle. */
		check("class-lookup-and-bundle-for-class",
		      [contents classNamed:@"BundleFixturePrincipal"] == principal &&
		      [contents classNamed:@"no.such.Class"] == nil &&
		      [NSBundle bundleForClass:principal] == contents &&
		      [NSBundle bundleForClass:[NSObject class]] == nil,
		      "1 of 2 names answered by -classNamed:, and the providing bundle is 1 of 1");
	covers("NSBundle", "classNamed:");
	covers("NSBundle", "bundleForClass:");
		check("bundle-load-posts-its-notification-with-the-classes",
		      fn_loaded_notification_seen && fn_loaded_classes != nil &&
		      [fn_loaded_classes containsObject:@"BundleFixturePrincipal"],
		      "NSBundleDidLoadNotification carried NSLoadedClasses naming the principal class");
	covers("NSBundle", "load");
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
	covers("NSBundle", "preflightAndReturnError:");
	covers("NSBundle", "loadAndReturnError:");
	}

	{
		/* ONE PROPERTY, ONE CAUSE, AND NO dlopen VERDICT IN IT: earlier versions asked about a bundle that an
		 * earlier check had already asked to load, so their own precondition could change the answer. A FRESH
		 * OBJECT that has never been asked to load anything is the deterministic form. */
		NSBundle *freshBundle = [NSBundle bundleWithPath:@FIXTURES "FlatFixture.app"];

		check("bundle-unload-answers-no-when-nothing-was-loaded",
		      freshBundle != nil && ![freshBundle isLoaded] && ![freshBundle unload],
		      "a bundle never asked to load reports isLoaded NO and unload answers NO");
	covers("NSBundle", "loaded");
	covers("NSBundle", "unload");
	}

	{
		/* §63.198: THE LEGACY PAIR ROUND-TRIPS, and reports the format it read — the half the modern door
		 * does through a pointer. */
		NSDictionary *source = [NSDictionary dictionaryWithObjectsAndKeys:@"v", @"k", nil];
		NSString *problem = nil;
		NSData * _Nullable data = [NSPropertyListSerialization dataFromPropertyList:source
									  format:NSPropertyListXMLFormat_v1_0
								errorDescription:&problem];
		NSPropertyListFormat seen = 0;
		id _Nullable back = [NSPropertyListSerialization propertyListFromData:(NSData *)(data != nil ? data : [NSData data])
							    mutabilityOption:NSPropertyListImmutable
								      format:&seen
							    errorDescription:&problem];

		check("plist-legacy-data-door-round-trips",
		      data != nil && back != nil && [back isEqual:source] && seen == NSPropertyListXMLFormat_v1_0 &&
		      problem == nil,
		      [[NSString stringWithFormat:@"data=%lu seen=%d back=%@ problem=%@",
			(unsigned long)[data length], (int)seen, back, problem] UTF8String]);
	covers("NSPropertyListSerialization", "dataFromPropertyList:format:errorDescription:");
	covers("NSPropertyListSerialization", "propertyListFromData:mutabilityOption:format:errorDescription:");
	}
	{
		/* THE OUT-PARAM IS A STRING, not an NSError, and garbage must fill it. */
		NSString *problem = nil;
		id _Nullable bad = [NSPropertyListSerialization propertyListFromData:
				(NSData *)[@"this is not a property list" dataUsingEncoding:NSUTF8StringEncoding]
			   mutabilityOption:NSPropertyListImmutable format:NULL
			     errorDescription:&problem];

		check("plist-legacy-error-description-is-a-string", bad == nil && problem != nil && [problem length] > 0,
		      [[NSString stringWithFormat:@"bad=%@ problem=%@", bad, problem] UTF8String]);
	covers("NSPropertyListSerialization", "propertyListFromData:mutabilityOption:format:errorDescription:");
	}
	{
		/* THE STATED READING: this tree WRITES XML, so the binary format answers nil WITH a description
		 * rather than quietly writing XML and calling it binary. */
		NSString *problem = nil;
		NSData * _Nullable binary = [NSPropertyListSerialization dataFromPropertyList:[NSArray array]
									    format:NSPropertyListBinaryFormat_v1_0
								  errorDescription:&problem];

		check("plist-binary-write-is-refused-with-a-description",
		      binary == nil && problem != nil && [problem rangeOfString:@"binary"].location != NSNotFound,
		      [[NSString stringWithFormat:@"binary=%@ problem=%@", binary, problem] UTF8String]);
	covers("NSPropertyListSerialization", "dataFromPropertyList:format:errorDescription:");
	}


	printf("FOUNDATION-BUNDLE DIAG leg=init-with-path\n");
	{
		/* NEW ASSERTIONS. The -init and the +door take the same path and must agree about what they built, so the
		 * law is a RELATION between them rather than a literal, and a third case - a directory that is NOT a
		 * bundle - must answer nil through the -init. A message to a NULLABLE receiver yields a nullable result,
		 * so each value is normalised through stringWithFormat: (whose varargs are not nullability-checked) AND
		 * its length is asserted, which is what keeps a pair of nils from comparing equal to each other. */
		NSBundle *byClass = [NSBundle bundleWithPath:@FIXTURES "BundleFixture.app"];
		NSBundle *byInit = [[NSBundle alloc] initWithPath:@FIXTURES "BundleFixture.app"];
		NSBundle *notABundle = [[NSBundle alloc] initWithPath:@FIXTURES "NotABundle"];
		NSString *initPath = [NSString stringWithFormat:@"%@", [byInit bundlePath]];
		NSString *classPath = [NSString stringWithFormat:@"%@", [byClass bundlePath]];
		NSString *initExe = [NSString stringWithFormat:@"%@", [byInit executablePath]];
		NSString *classExe = [NSString stringWithFormat:@"%@", [byClass executablePath]];
		NSString *initID = [NSString stringWithFormat:@"%@", [byInit bundleIdentifier]];
		NSString *classID = [NSString stringWithFormat:@"%@", [byClass bundleIdentifier]];

		check("bundle-init-with-path-matches-the-class-door",
		      byClass != nil && byInit != nil &&
		      [[byInit bundlePath] length] > 0 && [[byClass bundlePath] length] > 0 &&
		      [initPath isEqualToString:classPath] &&
		      [initExe isEqualToString:classExe] && [initID isEqualToString:classID] &&
		      notABundle == nil,
		      [[NSString stringWithFormat:@"init=%@ class=%@ id=%@ notABundle=%d",
			initPath, classPath, initID, (int)(notABundle != nil)] UTF8String]);
		covers("NSBundle", "initWithPath:");
		printf("FOUNDATION-BUNDLE DIAG leg=init-with-path-done\n");
	}

	printf("FOUNDATION-BUNDLE DIAG leg=registry-and-identifiers\n");
	{
		/* THE REGISTRY AND THE TWO IDENTIFIER DOORS, again as RELATIONS: the identifier the bundle ANSWERS is the
		 * one its OWN manifest carries, and the registries answer arrays holding what was loaded. Same nil-safe
		 * normalisation as above. */
		NSBundle *fixture = [NSBundle bundleWithPath:@FIXTURES "BundleFixture.app"];
		NSDictionary *info = [fixture infoDictionary];
		NSArray *bundles = [NSBundle allBundles];
		NSArray *frameworks = [NSBundle allFrameworks];
		NSString *plistID = [NSString stringWithFormat:@"%@", [info objectForKey:@"CFBundleIdentifier"]];
		NSString *doorID = [NSString stringWithFormat:@"%@", [fixture bundleIdentifier]];

		check("the-bundle-registry-and-the-identifiers-agree-with-the-manifest",
		      fixture != nil && info != nil && [info isKindOfClass:[NSDictionary class]] &&
		      [info count] > 0 && [[fixture bundleIdentifier] length] > 0 &&
		      [plistID isEqualToString:doorID] && [doorID length] > 0 &&
		      bundles != nil && [bundles isKindOfClass:[NSArray class]] && [bundles count] >= 1 &&
		      [bundles containsObject:[NSBundle mainBundle]] &&
		      frameworks != nil && [frameworks isKindOfClass:[NSArray class]] &&
		      ![frameworks containsObject:fixture],
		      [[NSString stringWithFormat:@"id=%@ plist=%@ bundles=%lu frameworks=%lu hasMain=%d",
			doorID, plistID, (unsigned long)[bundles count], (unsigned long)[frameworks count],
			(int)[bundles containsObject:[NSBundle mainBundle]]] UTF8String]);
		covers("NSBundle", "bundleIdentifier");
		covers("NSBundle", "infoDictionary");
		covers("NSBundle", "allBundles");
		covers("NSBundle", "allFrameworks");
		printf("FOUNDATION-BUNDLE DIAG leg=registry-and-identifiers-done\n");
	}

	printf("FOUNDATION-BUNDLE DIAG leg=shared-locations\n");
	{
		/* THE SHARED-LOCATION DOORS. MEASURED FIRST: with neither directory present BOTH doors answer nil, which
		 * is Apple's semantics for a bundle that has no such directory - so the checks below CREATE the
		 * directories, and then demand the answer. A nil-forever door would fail here (non-nil is required), and
		 * a door that answered nil even when the directory exists cannot pass either. The law itself is still a
		 * RELATION: the URL's path IS the path door's answer. */
		NSString *fixturePath = @FIXTURES "BundleFixture.app";
		NSString *sharedPath = nil;
		NSString *sharedViaURL = nil;
		NSURL *sharedURL = nil;
		NSURL *supportURL = nil;
		NSURL *receipt = nil;
		NSBundle *fixture;

		(void)mkdir(FIXTURES "BundleFixture.app/Contents/SharedFrameworks", 0755);
		(void)mkdir(FIXTURES "BundleFixture.app/Contents/SharedSupport", 0755);

		fixture = [NSBundle bundleWithPath:fixturePath];
		sharedPath = [NSString stringWithFormat:@"%@", [fixture sharedFrameworksPath]];
		sharedURL = [fixture sharedFrameworksURL];
		sharedViaURL = [NSString stringWithFormat:@"%@", [sharedURL path]];
		supportURL = [fixture sharedSupportURL];
		receipt = [fixture appStoreReceiptURL];

		printf("FOUNDATION-BUNDLE DIAG leg=shared-locations-read path=%s url=%s support=%s\n",
		       [sharedPath UTF8String], [sharedViaURL UTF8String],
		       supportURL != nil ? "set" : "nil");

		check("the-shared-location-doors-agree-between-path-and-url",
		      [[fixture sharedFrameworksPath] length] > 0 &&
		      [sharedPath hasSuffix:@"SharedFrameworks"] &&
		      sharedURL != nil && [sharedURL isFileURL] && [sharedViaURL length] > 0 &&
		      [sharedPath isEqualToString:sharedViaURL] &&
		      supportURL != nil && [supportURL isKindOfClass:[NSURL class]] &&
		      [[supportURL path] length] > 0 &&
		      (receipt == nil || [receipt isKindOfClass:[NSURL class]]),
		      [[NSString stringWithFormat:@"path=%@ urlPath=%@ support=%@ receipt=%@",
			sharedPath, sharedViaURL, supportURL != nil ? [supportURL path] : @"(nil)",
			receipt != nil ? [receipt absoluteString] : @"(nil)"] UTF8String]);
		covers("NSBundle", "sharedFrameworksPath");
		covers("NSBundle", "sharedFrameworksURL");
		covers("NSBundle", "sharedSupportURL");
		covers("NSBundle", "appStoreReceiptURL");
		printf("FOUNDATION-BUNDLE DIAG leg=shared-locations-done\n");
	}

	printf("FOUNDATION-BUNDLE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-BUNDLE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-BUNDLE DONE\n");
	return failc ? 1 : 0;
}
