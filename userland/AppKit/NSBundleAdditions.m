/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundleAdditions.m — the image and sound resource doors, moved out of Foundation (§63.52).
 *
 * THE TYPE SETS ARE THIS TIER'S, and they have to be: Apple's `-pathForImageResource:` answers files
 * "recognized by the NSImage class" and `-pathForSoundResource:` files "recognized by the NSSound class", and
 * this tree's `NSImage` is a real class while `NSSound` is absent. **The lists below stand in for that test —
 * the same lists Foundation used, moved here unchanged rather than re-chosen, because the moment of a MOVE is
 * not the moment to change behaviour.**
 *
 * THE LOOKUP FOLLOWS THE SYSTEM: the bundle's own resource directory, which is the same one every other
 * resource door uses, so a hit obeys THIS tree's layout. Only public Foundation API is used — this library is
 * its own framework, and reaching into Foundation's internals would put the boundary back where it was.
 */

#import <AppKit/NSBundleAdditions.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSURL.h>

static NSArray *fn_image_resource_extensions(void)
{
	return [NSArray arrayWithObjects:@"tiff", @"tif", @"jpg", @"jpeg", @"gif", @"png", @"bmp",
					 @"ico", @"pict", @"pct", @"pdf", @"eps", @"xbm", @"heic", nil];
}

static NSArray *fn_sound_resource_extensions(void)
{
	return [NSArray arrayWithObjects:@"aiff", @"aif", @"aifc", @"au", @"snd", @"wav", @"wave",
					 @"caf", @"mp3", @"m4a", @"aac", @"adts", @"flac", nil];
}

/* THE LOOKUP the two doors share. The name is tried AS GIVEN first — its extension is optional, and when one is
 * present the literal file wins — then as name.<ext> for each extension in the set, so both `logo.png` and
 * `logo` find `Resources/logo.png`. */
static NSString *fn_find_named_resource(NSBundle *bundle, NSString *name, NSArray *extensions)
{
	NSString *dir = [bundle resourcePath];
	NSFileManager *fm = [NSFileManager defaultManager];
	NSUInteger i;

	if (dir == nil || name == nil || [name length] == 0) {
		return nil;
	}
	{
		NSString *literal = [dir stringByAppendingPathComponent:name];

		if ([fm fileExistsAtPath:literal]) {
			return literal;
		}
	}
	for (i = 0; i < [extensions count]; i++) {
		NSString *candidate = [[dir stringByAppendingPathComponent:name]
					stringByAppendingPathExtension:[extensions objectAtIndex:i]];

		if ([fm fileExistsAtPath:candidate]) {
			return candidate;
		}
	}
	return nil;
}

@implementation NSBundle (NSBundleAppKitAdditions)

- (NSString * _Nullable)pathForImageResource:(NSString *)name
{
	return fn_find_named_resource(self, name, fn_image_resource_extensions());
}

- (NSURL * _Nullable)URLForImageResource:(NSString *)name
{
	NSString *path = [self pathForImageResource:name];

	return (path != nil) ? [NSURL fileURLWithPath:path] : nil;
}

- (NSString * _Nullable)pathForSoundResource:(NSString *)name
{
	return fn_find_named_resource(self, name, fn_sound_resource_extensions());
}

@end
