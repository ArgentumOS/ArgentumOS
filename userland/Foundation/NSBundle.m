/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundle.m — a bundle is a directory containing an Info.plist, and the manifest is a real property list.
 *
 * THE DEFINITION THIS SYSTEM USES IS IN THE HEADER. Here is what the code adds to it: the layout probe (flat vs
 * Contents/), the lazily-read manifest, the Apple-named keys, the resource lookup over the resource directory,
 * and the code-loading half (dlopen/objc_getClass). Nothing here invents a key or a directory name.
 */

/* FIRST, AND BEFORE ANY INCLUDE, BECAUSE IT HAS TO BE: +bundleForClass: and -classNamed: ask `dladdr` where a
 * class's image lives, and `dladdr`/`Dl_info` are the GNU extension of <dlfcn.h> -- glibc declares neither
 * unless something asks. The guest's musl answers to the same switch, and guarding it keeps a caller's own
 * definition intact (the same preamble NSCalendarDate.m needs for timegm). */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif

#import <Foundation/NSBundle.h>
#import <Foundation/NSAttributedStringMarkdown.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSString.h>
#import <Foundation/NSError.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSNumber.h>
#include <objc/runtime.h>
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* The Keys, named once: they are Apple's spellings and nothing below invents one. */
static NSString *const FnBundleIdentifierKey = @"CFBundleIdentifier";
static NSString *const FnBundleExecutableKey = @"CFBundleExecutable";
static NSString *const FnPrincipalClassKey = @"NSPrincipalClass";
static NSString *const FnBundleLocalizationsKey = @"CFBundleLocalizations";

/* Open bundles, in creation order: what +bundleWithIdentifier: searches. */
static NSMutableArray *fn_open_bundles = nil;

@interface NSBundle (FNPrivate)
- (instancetype)fnInitBareWithPath:(NSString *)path;
- (NSString *)fnLayoutDirectory;
- (NSDictionary *)fnLoadInfo;
- (NSDictionary *)fnStringTableNamed:(NSString *)tableName localization:(NSString *)localization;
@end

/* THE TWO CONSTANTS THIS CLASS NOW POSTS, restored where the previous constants-only unit had them: the
 * notification a successful -load posts, and the userInfo key carrying the classes it brought in. */
NSNotificationName const NSBundleDidLoadNotification = @"NSBundleDidLoadNotification";
NSString *const NSLoadedClasses = @"NSLoadedClasses";

@implementation NSBundle

/* THE LAYOUT PROBE, AND THE WHOLE OF "WHAT IS A BUNDLE": a directory with Contents/Info.plist is a Contents
 * bundle; one with Info.plist is a flat bundle; one with neither is not a bundle at all. */
static NSString *fn_layout_for_path(NSString *path)
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *contents = [path stringByAppendingPathComponent:@"Contents"];
	BOOL isDir = NO;

	if ([fm fileExistsAtPath:[contents stringByAppendingPathComponent:@"Info.plist"]]) {
		return contents;
	}
	if ([fm fileExistsAtPath:[path stringByAppendingPathComponent:@"Info.plist"] isDirectory:&isDir] &&
	    !isDir) {
		return path;
	}
	return nil;
}

/* THE RESOURCE DIRECTORY for a bundle at `path` whose layout directory is `layout` (nil for the bare door): a
 * Contents bundle's is Contents/Resources, a flat one's is Resources, and when NEITHER exists the bundle
 * directory itself is the answer -- this is exactly -resourcePath's rule, factored so the class-method doors
 * (which have no NSBundle object to ask) can share it without creating one and polluting +allBundles. */
static NSString *fn_resource_dir_for_path(NSString *path, NSString *layout)
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *resources;

	if (layout == nil) {
		return path;
	}
	resources = [path stringByAppendingPathComponent:
		[layout isEqualToString:path] ? @"Resources" : @"Contents/Resources"];
	if ([fm fileExistsAtPath:resources]) {
		return resources;
	}
	return path;
}

/* The resource directory for a bundle named only by its PATH, or nil when that path is not a bundle at all
 * (no Info.plist in either layout). This is the ...inBundleWithURL: doors' entry point. */
static NSString *fn_resource_dir_for_bundle_path(NSString *path)
{
	NSString *layout = path != nil ? fn_layout_for_path(path) : nil;

	return layout != nil ? fn_resource_dir_for_path(path, layout) : nil;
}

/* ONE lookup inside ONE resource directory: name + ext + subpath -> path, or nil. `name` == nil with a non-nil
 * `ext` builds ".<ext>" (the same meaning -pathForResource:ofType: already gives a nil name), and a nil name
 * with a nil ext is the nil answer. Factored so the instance and class doors cannot drift. */
static NSString *fn_find_in_dir(NSString *dir, NSString *name, NSString *ext, NSString *subpath)
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *file = name;
	NSString *candidate;

	if (dir == nil || (name == nil && ext == nil)) {
		return nil;
	}
	if (name == nil) {
		file = [NSString stringWithFormat:@".%@", ext];
	} else if (ext != nil && [ext length] > 0) {
		file = [NSString stringWithFormat:@"%@.%@", name, ext];
	}
	if (subpath != nil && [subpath length] > 0) {
		dir = [dir stringByAppendingPathComponent:subpath];
	}
	candidate = [dir stringByAppendingPathComponent:file];
	return [fm fileExistsAtPath:candidate] ? candidate : nil;
}

/* EVERY file in one resource directory whose name ends in ".<ext>" (or every file when ext is nil/empty), as
 * the plural door's answer. A listing that cannot be read is an EMPTY array, which is the same shape
 * -pathsForResourcesOfType:inDirectory: already gives. */
static NSArray *fn_list_in_dir(NSString *dir, NSString *ext, NSString *subpath)
{
	NSMutableArray *paths = [NSMutableArray array];
	NSFileManager *fm = [NSFileManager defaultManager];
	NSArray *listing;
	NSUInteger i;

	if (dir == nil) {
		return paths;
	}
	if (subpath != nil && [subpath length] > 0) {
		dir = [dir stringByAppendingPathComponent:subpath];
	}
	listing = [fm contentsOfDirectoryAtPath:dir error:NULL];
	if (listing == nil) {
		return paths;
	}
	for (i = 0; i < [listing count]; i++) {
		NSString *entry = [listing objectAtIndex:i];
		NSString *suffix = (ext != nil && [ext length] > 0)
			? [NSString stringWithFormat:@".%@", ext] : nil;

		if (suffix == nil || [entry hasSuffix:suffix]) {
			[paths addObject:[dir stringByAppendingPathComponent:entry]];
		}
	}
	return paths;
}

/* A file URL for a path, or nil for a nil path (the URL doors' whole translation). */
static NSURL *fn_url_for_path(NSString *path)
{
	return path != nil ? [NSURL fileURLWithPath:path] : nil;
}

/* The plural URL door's whole translation. */
static NSArray *fn_urls_for_paths(NSArray *paths)
{
	NSMutableArray *urls = [NSMutableArray array];

	for (NSUInteger i = 0; i < [paths count]; i++) {
		/* Through a local, whose type carries no nullability: addObject: wants a nonnull object and
		 * fileURLWithPath: is declared nullable, so this keeps the two honest without a cast. */
		NSURL *url = [NSURL fileURLWithPath:[paths objectAtIndex:i]];

		[urls addObject:url];
	}
	return urls;
}

- (instancetype _Nullable)initWithPath:(NSString *)path
{
	NSString *layout;

	if (path == nil) {
		return nil;
	}
	layout = fn_layout_for_path(path);
	if (layout == nil) {
		/* NOT A BUNDLE, and the contract is nil rather than an object that answers nothing: a caller checks
		 * for the bundle's existence with "did I get one". */
		return nil;
	}
	self = [super init];
	if (self != nil) {
		_path = [path copy];
		_layout = [layout copy];
		if (fn_open_bundles == nil) {
			fn_open_bundles = [[NSMutableArray alloc] init];
		}
		[fn_open_bundles addObject:self];
	}
	return self;
}

/* THE BARE DOOR: a bundle with a path and NO manifest. -initWithPath: answers nil for a directory that is not
 * a bundle, which is right for a caller naming a path - but Apple's -mainBundle for a command-line tool is a
 * bundle whose path is the executable's DIRECTORY and whose infoDictionary is nil, so that case needs a door
 * that does not demand a manifest. This one is private and named for what it is. */
- (instancetype)fnInitBareWithPath:(NSString *)path
{
	self = [super init];
	if (self != nil) {
		_path = [path copy];
		_layout = nil;
		if (fn_open_bundles == nil) {
			fn_open_bundles = [[NSMutableArray alloc] init];
		}
		[fn_open_bundles addObject:self];
	}
	return self;
}

- (instancetype)init
{
	return [self fnInitBareWithPath:@""];
}

+ (instancetype _Nullable)bundleWithPath:(NSString *)path
{
	return [[[self alloc] initWithPath:path] autorelease];
}

+ (NSBundle *)mainBundle
{
	/* WALKING UP FROM argv[0], which is available because this system publishes /proc/self/cmdline and
	 * NSProcessInfo already reads it. A tool that lives outside any bundle gets a bundle whose path is its own
	 * directory - Apple's answer for a command-line tool - and its manifest is nil, which the accessors below
	 * report the same way they report a missing key. */
	static NSBundle *fn_main = nil;

	if (fn_main != nil) {
		return fn_main;
	}
	{
		NSArray *arguments = nil;
		NSString *executable = nil;

		@try {
			arguments = [(id)[[NSProcessInfo processInfo] performSelector:@selector(arguments)] retain];
		} @catch (NSException *e) {
			arguments = nil;
		}
		if ([arguments count] > 0) {
			char resolved[PATH_MAX];

			executable = [arguments objectAtIndex:0];
			if (realpath([executable UTF8String], resolved) != NULL) {
				executable = [NSString stringWithUTF8String:resolved];
			}
		}
		if (executable == nil) {
			fn_main = [[NSBundle alloc] initWithPath:@"/"];
		} else {
			NSString *directory = [executable stringByDeletingLastPathComponent];
			NSString *candidate = directory;
			NSBundle *found = nil;

			while ([candidate length] > 1 && found == nil) {
				found = [[NSBundle alloc] initWithPath:candidate];
				if (found == nil) {
					candidate = [candidate stringByDeletingLastPathComponent];
				}
			}
			if (found != nil) {
				fn_main = found;
			} else {
				/* NO BUNDLE ANCESTOR: THE TOOL CASE, through the bare door - a bundle whose path is the
				 * executable's directory and whose manifest is nil, which is Apple's own answer for a
				 * command-line tool. Falling back to -initWithPath: here would answer nil, because that
				 * directory is not a bundle by this class's own definition. */
				fn_main = [[NSBundle alloc] fnInitBareWithPath:directory];
			}
		}
	}
	return fn_main;
}

+ (NSBundle * _Nullable)bundleWithIdentifier:(NSString *)identifier
{
	NSUInteger i;

	for (i = 0; i < [fn_open_bundles count]; i++) {
		NSBundle *bundle = [fn_open_bundles objectAtIndex:i];

		if ([[bundle bundleIdentifier] isEqualToString:identifier]) {
			return bundle;
		}
	}
	return nil;
}

+ (NSArray *)allBundles
{
	return fn_open_bundles != nil ? fn_open_bundles : [NSArray array];
}

- (NSString *)bundlePath
{
	return _path;
}

- (NSString *)fnLayoutDirectory
{
	return _layout;
}

- (NSDictionary *)fnLoadInfo
{
	if (_info == nil && _layout != nil) {
		NSData *data = [NSData dataWithContentsOfFile:
			[[_layout stringByAppendingPathComponent:@"Info.plist"] description]];

		if (data != nil) {
			id parsed = nil;

			@try {
				parsed = [NSPropertyListSerialization propertyListWithData:data
										   options:0
										    format:NULL
										     error:NULL];
			} @catch (NSException *e) {
				parsed = nil;
			}
			if ([parsed isKindOfClass:[NSDictionary class]]) {
				_info = [(NSDictionary *)parsed retain];
			}
		}
		if (_info == nil) {
			/* AN UNREADABLE OR ABSENT MANIFEST IS NOT A CRASH: the bundle exists, its keys do not. */
			_info = [[NSDictionary alloc] init];
		}
	}
	return _info;
}

- (NSDictionary * _Nullable)infoDictionary
{
	NSDictionary *info = [self fnLoadInfo];

	return [info count] > 0 ? info : nil;
}

- (id _Nullable)objectForInfoDictionaryKey:(NSString *)key
{
	return key != nil ? [[self fnLoadInfo] objectForKey:key] : nil;
}

- (NSString * _Nullable)bundleIdentifier
{
	id value = [self objectForInfoDictionaryKey:FnBundleIdentifierKey];

	return [value isKindOfClass:[NSString class]] ? value : nil;
}

- (NSString * _Nullable)executablePath
{
	id name = [self objectForInfoDictionaryKey:FnBundleExecutableKey];

	if (![name isKindOfClass:[NSString class]] || [name length] == 0) {
		return nil;
	}
	if ([_layout isEqualToString:_path]) {
		/* FLAT: the executable sits in the bundle directory itself. */
		return [_path stringByAppendingPathComponent:name];
	}
	return [[[_path stringByAppendingPathComponent:@"Contents"] stringByAppendingPathComponent:@"MacOS"]
		stringByAppendingPathComponent:name];
}

- (NSString * _Nullable)resourcePath
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *resources = [_path stringByAppendingPathComponent:
		[_layout isEqualToString:_path] ? @"Resources" : @"Contents/Resources"];

	if ([fm fileExistsAtPath:resources]) {
		return resources;
	}
	/* A bundle without a Resources directory still has resources: they are at its root, which is where a flat
	 * bundle keeps them. */
	return _path;
}

- (NSArray *)localizations
{
	NSMutableArray *names = [NSMutableArray array];
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *resources = [self resourcePath];
	if (resources != nil) {
		NSArray *listing = [fm contentsOfDirectoryAtPath:resources error:NULL];

		/* A LISTING THAT COULD NOT BE READ IS AN EMPTY ANSWER, NOT A CRASH: the method contract is "the
		 * localizations this bundle has", and a bundle whose Resources cannot be listed has none. */
		if (listing == nil) {
			return names;
		}
		for (NSUInteger i = 0; i < [listing count]; i++) {
			NSString *entry = [listing objectAtIndex:i];

			if ([entry hasSuffix:@".lproj"]) {
				[names addObject:[entry stringByDeletingPathExtension]];
			}
		}
	}
	return names;
}

- (NSString * _Nullable)pathForResource:(NSString * _Nullable)name ofType:(NSString * _Nullable)ext
{
	return [self pathForResource:name ofType:ext inDirectory:nil];
}

- (NSString * _Nullable)pathForResource:(NSString * _Nullable)name
				ofType:(NSString * _Nullable)ext
			   inDirectory:(NSString * _Nullable)subpath
{
	return fn_find_in_dir([self resourcePath], name, ext, subpath);
}

- (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
				     inDirectory:(NSString * _Nullable)subpath
{
	return fn_list_in_dir([self resourcePath], ext, subpath);
}

/* ---- THE LOCALIZATION-AWARE RESOURCE DOORS --------------------------------------------------------- */

- (NSString * _Nullable)pathForResource:(NSString * _Nullable)name
				ofType:(NSString * _Nullable)ext
			   inDirectory:(NSString * _Nullable)subpath
		       forLocalization:(NSString * _Nullable)localization
{
	NSString *resources = [self resourcePath];
	NSString *localized;

	if (resources != nil && localization != nil && [localization length] > 0) {
		localized = [resources stringByAppendingPathComponent:
			[NSString stringWithFormat:@"%@.lproj", localization]];
		{
			NSString *found = fn_find_in_dir(localized, name, ext, subpath);

			if (found != nil) {
				return found;
			}
		}
	}
	/* THE FALLBACK IS THE FLAT LOOKUP: a bundle that does not carry the localization, or carries the file
	 * only at its root, still answers - the same "missing is not an error" contract the flat door keeps. */
	return fn_find_in_dir(resources, name, ext, subpath);
}

- (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
			 inDirectory:(NSString * _Nullable)subpath
		     forLocalization:(NSString * _Nullable)localization
{
	NSString *resources = [self resourcePath];

	if (resources != nil && localization != nil && [localization length] > 0) {
		NSString *localized = [resources stringByAppendingPathComponent:
			[NSString stringWithFormat:@"%@.lproj", localization]];
		NSArray *found = fn_list_in_dir(localized, ext, subpath);

		if ([found count] > 0) {
			return found;
		}
	}
	return fn_list_in_dir(resources, ext, subpath);
}

- (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
{
	return fn_url_for_path([self pathForResource:name ofType:ext]);
}

- (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
		       subdirectory:(NSString * _Nullable)subpath
{
	return fn_url_for_path([self pathForResource:name ofType:ext inDirectory:subpath]);
}

- (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
		       subdirectory:(NSString * _Nullable)subpath
		       localization:(NSString * _Nullable)localization
{
	return fn_url_for_path([self pathForResource:name ofType:ext inDirectory:subpath
					      forLocalization:localization]);
}

- (NSArray *)URLsForResourcesWithExtension:(NSString * _Nullable)ext
			      subdirectory:(NSString * _Nullable)subpath
{
	return fn_urls_for_paths([self pathsForResourcesOfType:ext inDirectory:subpath]);
}

- (NSArray *)URLsForResourcesWithExtension:(NSString * _Nullable)ext
			      subdirectory:(NSString * _Nullable)subpath
			      localization:(NSString * _Nullable)localization
{
	return fn_urls_for_paths([self pathsForResourcesOfType:ext inDirectory:subpath
						       forLocalization:localization]);
}

/* ---- THE IMAGE AND SOUND RESOURCE DOORS MOVED TO APPKIT (2026-10-01, §63.52) -----------------------
 *
 * `-pathForImageResource:`, `-URLForImageResource:` and `-pathForSoundResource:` are **AppKit's** — the image
 * pair is declared by AppKit's `NSImage.h` and the sound door by AppKit's `NSSound.h` (measured against the
 * macOS 14.5 SDK headers), because what counts as an image or a sound resource is decided by classes that are
 * not Foundation's. THE TYPE SETS AND THE LOOKUP MOVED WITH THEM, unchanged: `userland/AppKit/
 * NSBundleAdditions.m`. This file no longer knows about images or sounds.
 */

/* The two CLASS doors that search the MAIN bundle (Apple's contract), and the two that search a bundle named
 * by URL. The URL doors read the URL's path as a bundle and answer nil for one that is not -- WITHOUT creating
 * an NSBundle, so +allBundles is not grown by a lookup. */
+ (NSString * _Nullable)pathForResource:(NSString * _Nullable)name
				ofType:(NSString * _Nullable)ext
			   inDirectory:(NSString * _Nullable)subpath
{
	return [[self mainBundle] pathForResource:name ofType:ext inDirectory:subpath];
}

+ (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
			 inDirectory:(NSString * _Nullable)subpath
{
	return [[self mainBundle] pathsForResourcesOfType:ext inDirectory:subpath];
}

+ (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
		       subdirectory:(NSString * _Nullable)subpath
		  inBundleWithURL:(NSURL *)bundleURL
{
	return fn_url_for_path(fn_find_in_dir(fn_resource_dir_for_bundle_path([bundleURL path]),
					      name, ext, subpath));
}

+ (NSArray *)URLsForResourcesWithExtension:(NSString * _Nullable)ext
			      subdirectory:(NSString * _Nullable)subpath
			 inBundleWithURL:(NSURL *)bundleURL
{
	return fn_urls_for_paths(fn_list_in_dir(fn_resource_dir_for_bundle_path([bundleURL path]),
						ext, subpath));
}

/* ---- THE STANDARD BUNDLE DIRECTORIES AND THEIR URLS ------------------------------------------------- */

- (NSURL *)bundleURL
{
	return fn_url_for_path([self bundlePath]);
}

- (NSURL * _Nullable)executableURL
{
	return fn_url_for_path([self executablePath]);
}

- (NSURL *)resourceURL
{
	return fn_url_for_path([self resourcePath]);
}

/* A standard directory hangs off the LAYOUT directory (the bundle itself for a flat bundle or the bare door,
 * its Contents/ for a Contents bundle) and is EXISTENCE-GATED: the path when the directory is there, nil
 * otherwise. This is the rule the header names; it is stated once here because every directory door shares it. */
- (NSString *)fnStandardDirectory:(NSString *)name
{
	NSString *base = _layout != nil ? _layout : _path;
	NSString *dir = [base stringByAppendingPathComponent:name];

	return [[NSFileManager defaultManager] fileExistsAtPath:dir] ? dir : nil;
}

- (NSString * _Nullable)pathForAuxiliaryExecutable:(NSString *)executableName
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *candidate;

	if (executableName == nil || [executableName length] == 0) {
		return nil;
	}
	if (_layout == nil || [_layout isEqualToString:_path]) {
		/* FLAT (or the bare door): the executable sits in the bundle directory itself, as -executablePath
		 * already places it. */
		candidate = [_path stringByAppendingPathComponent:executableName];
	} else {
		candidate = [[[_path stringByAppendingPathComponent:@"Contents"]
			stringByAppendingPathComponent:@"MacOS"] stringByAppendingPathComponent:executableName];
	}
	/* EXISTENCE-GATED: Apple's door answers nil for an executable the bundle does not contain. */
	return [fm fileExistsAtPath:candidate] ? candidate : nil;
}

- (NSURL * _Nullable)URLForAuxiliaryExecutable:(NSString *)executableName
{
	return fn_url_for_path([self pathForAuxiliaryExecutable:executableName]);
}

- (NSString * _Nullable)builtInPlugInsPath
{
	return [self fnStandardDirectory:@"PlugIns"];
}

- (NSURL * _Nullable)builtInPlugInsURL
{
	return fn_url_for_path([self builtInPlugInsPath]);
}

- (NSString * _Nullable)privateFrameworksPath
{
	return [self fnStandardDirectory:@"Frameworks"];
}

- (NSURL * _Nullable)privateFrameworksURL
{
	return fn_url_for_path([self privateFrameworksPath]);
}

- (NSString * _Nullable)sharedFrameworksPath
{
	return [self fnStandardDirectory:@"SharedFrameworks"];
}

- (NSURL * _Nullable)sharedFrameworksURL
{
	return fn_url_for_path([self sharedFrameworksPath]);
}

- (NSString * _Nullable)sharedSupportPath
{
	return [self fnStandardDirectory:@"SharedSupport"];
}

- (NSURL * _Nullable)sharedSupportURL
{
	return fn_url_for_path([self sharedSupportPath]);
}

/* ---- CREATING A BUNDLE FROM A CLASS OR A URL ------------------------------------------------------- */

+ (NSBundle * _Nullable)bundleForClass:(Class)aClass
{
	Dl_info info;
	NSUInteger i;

	if (aClass == nil) {
		return nil;
	}
	/* A BUNDLE THAT NAMES THIS CLASS as its NSPrincipalClass is matched first, WITHOUT loading it (the
	 * manifest key is read, not -principalClass, so no dlopen happens as a side effect of the query). */
	for (i = 0; i < [fn_open_bundles count]; i++) {
		NSBundle *bundle = [fn_open_bundles objectAtIndex:i];
		id name = [bundle objectForInfoDictionaryKey:FnPrincipalClassKey];

		if ([name isKindOfClass:[NSString class]] &&
		    strcmp(class_getName(aClass), [name UTF8String]) == 0) {
			return bundle;
		}
	}
	/* ELSE THE CLASS'S IMAGE decides: dladdr on the class object gives the shared object ELF put it in, and
	 * the open bundle whose path is a prefix of that image provided it. */
	if (dladdr((void *)aClass, &info) != 0 && info.dli_fname != NULL) {
		NSString *image = [NSString stringWithUTF8String:info.dli_fname];

		for (i = 0; i < [fn_open_bundles count]; i++) {
			NSBundle *bundle = [fn_open_bundles objectAtIndex:i];

			if ([image hasPrefix:[bundle bundlePath]]) {
				return bundle;
			}
		}
	}
	return nil;
}

+ (NSBundle * _Nullable)bundleWithURL:(NSURL *)url
{
	return [[[self alloc] initWithURL:url] autorelease];
}

- (instancetype _Nullable)initWithURL:(NSURL *)url
{
	/* A URL with no path (one that is not a file URL in this library) names no directory, so it is not a
	 * bundle - and -initWithPath: answers nil for nil. */
	return [self initWithPath:[url path]];
}

/* ---- LOCALIZATION INFORMATION, AND A LOCALIZED STRING OVER AN EXPLICIT LIST ------------------------- */

+ (NSArray *)preferredLocalizationsFromArray:(NSArray *)localizationsArray
{
	return [self preferredLocalizationsFromArray:localizationsArray forPreferences:nil];
}

+ (NSArray *)preferredLocalizationsFromArray:(NSArray *)localizationsArray
			      forPreferences:(NSArray *)preferencesArray
{
	NSMutableArray *matches = [NSMutableArray array];
	NSArray *preferences = preferencesArray;
	NSUInteger i, j;

	if (localizationsArray == nil) {
		return [NSArray array];
	}
	if (preferences == nil || [preferences count] == 0) {
		/* THE ONLY LANGUAGE SOURCE THIS LIBRARY HAS: the current locale's identifier. Apple consults a
		 * user preferred-languages list; this system keeps none, and the header names that. */
		preferences = [NSArray arrayWithObject:[[NSLocale currentLocale] localeIdentifier]];
	}
	for (i = 0; i < [preferences count]; i++) {
		NSString *preference = [preferences objectAtIndex:i];
		NSString *prefLanguage = [[preference componentsSeparatedByString:@"-"] objectAtIndex:0];

		prefLanguage = [[prefLanguage componentsSeparatedByString:@"_"] objectAtIndex:0];
		for (j = 0; j < [localizationsArray count]; j++) {
			NSString *candidate = [localizationsArray objectAtIndex:j];
			NSString *candLanguage = [[candidate componentsSeparatedByString:@"-"] objectAtIndex:0];

			candLanguage = [[candLanguage componentsSeparatedByString:@"_"] objectAtIndex:0];
			if (([candidate isEqualToString:preference] ||
			     [candLanguage isEqualToString:prefLanguage]) &&
			    ![matches containsObject:candidate]) {
				[matches addObject:candidate];
			}
		}
	}
	/* NOTHING MATCHED answers the array unchanged (Apple's best-effort fallback), so a caller with no
	 * preference still receives a usable list rather than an empty one. */
	return [matches count] > 0 ? matches : localizationsArray;
}

- (NSString * _Nullable)developmentLocalization
{
	id value = [self objectForInfoDictionaryKey:@"CFBundleDevelopmentRegion"];

	return [value isKindOfClass:[NSString class]] ? value : nil;
}

- (NSDictionary * _Nullable)localizedInfoDictionary
{
	/* NO NEGOTIATION, and this is the door where that shows: Apple merges the current localization's
	 * InfoPlist.strings into the manifest, and this library has no current localization to merge. It answers
	 * the manifest itself rather than inventing a merged one; a caller that wants a localized VALUE asks
	 * -localizedStringForKey:value:table:. */
	return [self infoDictionary];
}

- (NSArray *)preferredLocalizations
{
	return [NSBundle preferredLocalizationsFromArray:[self localizations]];
}

- (NSString *)localizedStringForKey:(NSString *)key
			     value:(NSString *)value
			     table:(NSString *)tableName
		     localizations:(NSArray *)localizations
{
	NSString *name = (tableName != nil && [tableName length] > 0) ? tableName : @"Localizable";
	NSString *found = nil;
	NSUInteger i;

	for (i = 0; i < [localizations count] && found == nil; i++) {
		NSDictionary *table = [self fnStringTableNamed:name localization:[localizations objectAtIndex:i]];

		found = (table != nil) ? [table objectForKey:key] : nil;
	}
	if (found == nil) {
		NSDictionary *table = [self fnStringTableNamed:name localization:nil];

		found = (table != nil) ? [table objectForKey:key] : nil;
	}
	if (found != nil) {
		return found;
	}
	if (value != nil && [value length] > 0) {
		NSString *fallback = value;	/* a local: the door promises a string and `value` is nullable */

		return fallback;
	}
	return key;
}

/* ---- CLASS LOOKUP, AND CODE LOADING THAT REPORTS ITS ERROR ------------------------------------------ */

- (_Nullable Class)classNamed:(NSString *)className
{
	Class cls;
	Dl_info info;

	if (className == nil) {
		return nil;
	}
	cls = objc_getClass([className UTF8String]);
	if (cls == nil) {
		return nil;
	}
	/* THE CLASS MUST BELONG TO THIS BUNDLE: dladdr places its image, and this bundle's path must be a prefix
	 * of it. When dladdr cannot place the class the check is skipped and the class is answered -- the runtime
	 * knows it, which is as much as this system can say (the header names this boundary). */
	if (dladdr((void *)cls, &info) != 0 && info.dli_fname != NULL) {
		NSString *image = [NSString stringWithUTF8String:info.dli_fname];

		if (![image hasPrefix:_path]) {
			return nil;
		}
	}
	return cls;
}

/* THE ERROR-REPORTING FORMS OF -load. A bundle that names no executable, or whose executable the loader
 * refuses, is an NSCocoaErrorDomain / NSFileNoSuchFileError with the executable's path in NSFilePathErrorKey
 * -- the ONE error this class can name, because the manifest's CFBundleExecutable is the only file it knows. */
- (NSError *)fnLoadingError:(NSString *)reason
{
	NSMutableDictionary *info = [NSMutableDictionary dictionary];
	NSString *path = [self executablePath];

	[info setObject:reason forKey:NSLocalizedDescriptionKey];
	if (path != nil) {
		[info setObject:path forKey:NSFilePathErrorKey];
	}
	return [NSError errorWithDomain:NSCocoaErrorDomain
				   code:NSFileNoSuchFileError
			       userInfo:info];
}

- (BOOL)loadAndReturnError:(NSError **)error
{
	if ([self load]) {
		return YES;
	}
	if (error != NULL) {
		*error = [self fnLoadingError:@"The bundle's executable could not be loaded."];
	}
	return NO;
}

- (BOOL)preflightAndReturnError:(NSError **)error
{
	NSString *path = [self executablePath];
	NSString *reason = nil;

	if (path == nil) {
		reason = @"The bundle names no executable to load.";
	} else if (![[NSFileManager defaultManager] isReadableFileAtPath:path]) {
		reason = @"The bundle's executable is not a readable file.";
	}
	if (reason != nil) {
		if (error != NULL) {
			*error = [self fnLoadingError:reason];
		}
		return NO;
	}
	return YES;
}

- (BOOL)load
{
	NSString *executable = [self executablePath];
	const char *path;

	if (_handle != NULL) {
		return YES;	/* already loaded: Apple's -load is idempotent */
	}
	if (executable == nil) {
		return NO;
	}
	path = [executable UTF8String];
	_handle = dlopen(path, RTLD_NOW);
	if (_handle == NULL) {
		return NO;
	}
	/* THE NOTIFICATION AND ITS userInfo KEY, which is what the two constants this class shipped with are FOR:
	 * the classes the load brought in, named by their principal class because that is what a bundle names. */
	{
		id principal = [self objectForInfoDictionaryKey:FnPrincipalClassKey];
		NSMutableDictionary *info = [NSMutableDictionary dictionary];
		NSArray *loaded = [NSArray array];

		if ([principal isKindOfClass:[NSString class]]) {
			loaded = [NSArray arrayWithObject:principal];
		}
		[info setObject:loaded forKey:NSLoadedClasses];
		[[NSNotificationCenter defaultCenter] postNotificationName:NSBundleDidLoadNotification
								    object:self
								  userInfo:info];
	}
	return YES;
}

- (BOOL)isLoaded
{
	return _handle != NULL;
}

- (_Nullable Class)principalClass
{
	id name = [self objectForInfoDictionaryKey:FnPrincipalClassKey];

	if (![name isKindOfClass:[NSString class]]) {
		/* NO NSPrincipalClass: Apple answers the class of the bundle's principal object, and this system has
		 * none to ask - so nil, which is the same answer Apple gives a bundle without one. */
		return nil;
	}
	if (_handle == NULL && ![self load]) {
		return nil;
	}
	return objc_getClass([name UTF8String]);
}

- (BOOL)unload
{
	if (_handle == NULL) {
		return NO;
	}
	if (dlclose(_handle) != 0) {
		return NO;
	}
	_handle = NULL;
	return YES;
}

/* THE EXECUTABLE'S ARCHITECTURE. Apple scans a Mach-O executable's headers and answers each cputype it finds;
 * THIS SYSTEM'S EXECUTABLES ARE ELF, so this reads the ELF header instead -- 0x7f "ELF", then e_machine at
 * offset 18 in the byte order e_ident[EI_DATA] names -- and maps the ELF machine onto the codes the header
 * declares. A bundle whose executable is absent, unreadable, not ELF, or an ELF machine this library has no
 * code for answers nil: that is Apple's "no Mach-O executable" case, answered the way THIS format says it. The
 * ANSWERED values are THIS LIBRARY'S enum values (NSBundle.h says the values are ours), so a caller comparing
 * against a header maps an ELF e_machine the same way rather than against a Mach-O cputype. */
- (NSArray * _Nullable)executableArchitectures
{
	NSString *path = [self executablePath];
	NSData *data;
	const unsigned char *bytes;
	unsigned short machine;
	int code = 0;

	if (path == nil) {
		return nil;
	}
	data = [NSData dataWithContentsOfFile:path];
	if (data == nil || [data length] < 20) {
		return nil;
	}
	bytes = [data bytes];
	if (bytes[0] != 0x7f || bytes[1] != 'E' || bytes[2] != 'L' || bytes[3] != 'F') {
		return nil;	/* not ELF: no architecture this system can name */
	}
	/* e_machine, 2 bytes at offset 18, in e_ident[EI_DATA]'s byte order (1 little, 2 big). */
	if (bytes[5] == 2) {
		machine = (unsigned short)((bytes[18] << 8) | bytes[19]);
	} else {
		machine = (unsigned short)((bytes[19] << 8) | bytes[18]);
	}
	switch (machine) {
	case 3:		code = NSBundleExecutableArchitectureI386;	break;	/* EM_386 */
	case 20:	code = NSBundleExecutableArchitecturePPC;	break;	/* EM_PPC */
	case 21:	code = NSBundleExecutableArchitecturePPC64;	break;	/* EM_PPC64 */
	case 62:	code = NSBundleExecutableArchitectureX86_64;	break;	/* EM_X86_64 */
	case 183:	code = NSBundleExecutableArchitectureARM64;	break;	/* EM_AARCH64 */
	default:	code = 0;					break;
	}
	if (code == 0) {
		return nil;	/* an ELF machine this library has no code for */
	}
	return [NSArray arrayWithObject:[NSNumber numberWithInt:code]];
}

- (void)dealloc
{
	[_path release];
	[_layout release];
	[_info release];
	[super dealloc];
}

/* THE TABLE A NAME REFERS TO, read with this library's own property-list reader: a `.strings` file IS an
 * old-style property list whose dictionary is the table. nil (an absent or unreadable file) is not an error
 * here — the door's contract has two fallbacks after it. */
- (NSDictionary *)fnStringTableNamed:(NSString *)tableName localization:(NSString *)localization
{
	NSString *path = [self pathForResource:tableName ofType:@"strings" inDirectory:nil forLocalization:localization];
	NSData *data;
	id parsed;

	if (path == nil) {
		return nil;
	}
	data = [NSData dataWithContentsOfFile:path];
	if (data == nil) {
		return nil;
	}
	parsed = [NSPropertyListSerialization propertyListWithData:data options:0 format:NULL error:NULL];
	return [parsed isKindOfClass:[NSDictionary class]] ? parsed : nil;
}

- (NSString *)localizedStringForKey:(NSString *)key
			     value:(nullable NSString *)value
			     table:(nullable NSString *)tableName
{
	NSDictionary *table = [self fnStringTableNamed:((tableName != nil && [tableName length] > 0)
						       ? tableName : @"Localizable") localization:nil];
	NSString *found = (table != nil) ? [table objectForKey:key] : nil;

	if (found != nil) {
		return found;
	}
	if (value != nil && [value length] > 0) {
		NSString *fallback = value;	/* a local: the door promises a string and `value` is nullable */

		return fallback;
	}
	return key;
}

- (NSAttributedString *)localizedAttributedStringForKey:(NSString *)key
						 value:(nullable NSString *)value
						 table:(nullable NSString *)tableName
{
	NSString *localized = [self localizedStringForKey:key value:value table:tableName];

	/* Through a local for the same reason: initWithMarkdownString: is declared nullable and this door's
	 * contract is a string, so the two are kept honest without a cast. */
	NSAttributedString *answer = [[NSAttributedString alloc] initWithMarkdownString:localized
										options:nil
										baseURL:nil
										  error:NULL];
	return answer;
}

@end
