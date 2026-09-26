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

#import <Foundation/NSBundle.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSString.h>
#import <Foundation/NSError.h>
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
		NSArray *listing = [fm directoryContentsAtPath:resources];

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
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *resources = [self resourcePath];
	NSString *file = name;
	NSString *candidate;

	if (resources == nil || (name == nil && ext == nil)) {
		return nil;
	}
	if (name == nil) {
		file = [NSString stringWithFormat:@".%@", ext];
	} else if (ext != nil && [ext length] > 0) {
		file = [NSString stringWithFormat:@"%@.%@", name, ext];
	}
	if (subpath != nil && [subpath length] > 0) {
		resources = [resources stringByAppendingPathComponent:subpath];
	}
	candidate = [resources stringByAppendingPathComponent:file];
	return [fm fileExistsAtPath:candidate] ? candidate : nil;
}

- (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
				     inDirectory:(NSString * _Nullable)subpath
{
	NSMutableArray *paths = [NSMutableArray array];
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *resources = [self resourcePath];
	NSArray *listing;

	if (resources == nil) {
		return paths;
	}
	if (subpath != nil && [subpath length] > 0) {
		resources = [resources stringByAppendingPathComponent:subpath];
	}
	listing = [fm directoryContentsAtPath:resources];
	if (listing == nil) {
		return paths;
	}
	for (NSUInteger i = 0; i < [listing count]; i++) {
		NSString *entry = [listing objectAtIndex:i];
		NSString *suffix = (ext != nil && [ext length] > 0)
			? [NSString stringWithFormat:@".%@", ext] : nil;

		if (suffix == nil || [entry hasSuffix:suffix]) {
			[paths addObject:[resources stringByAppendingPathComponent:entry]];
		}
	}
	return paths;
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

- (void)dealloc
{
	[_path release];
	[_layout release];
	[_info release];
	[super dealloc];
}

@end
