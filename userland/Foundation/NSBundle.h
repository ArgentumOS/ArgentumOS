/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundle.h — a bundle this system defines, and the vocabulary around its executable architecture.
 *
 * THE CLASS IS HERE, AND AN EARLIER REVISION OF THIS COMMENT WAS WRONG TO SAY OTHERWISE: it claimed this
 * system's bundles are "AGFS directories with a libconfig manifest ... so there is no NSBundle to hang
 * -executableArchitectures off." That stopped being true the moment the class below landed; what a bundle IS
 * is stated at the class (a directory containing an Info.plist, in either layout Apple documents). The five
 * architecture codes and the two notification names remain part of the vocabulary.
 *
 * AND THE ARCHITECTURE CODES CARRY A WARNING WORTH READING: Apple's five values ARE the Mach-O cputype numbers,
 * because a program compares them against a Mach-O header. THIS SYSTEM'S EXECUTABLES ARE ELF, so the names are
 * here with THIS LIBRARY'S values (§11.6.1 D2) rather than Mach-O numbers. But the comparison they were
 * designed for NO LONGER has "no counterpart here": -executableArchitectures reads the ELF header's e_machine
 * and answers one of these codes, so a caller maps an ELF header the same way this door does rather than
 * comparing a Mach-O cputype. That is stated rather than left to be discovered.
 */

#ifndef FOUNDATION_NSBUNDLE_H
#define FOUNDATION_NSBUNDLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSAttributedString.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSNotification.h>

@class NSURL;	/* only named here; NSBundle.m imports NSURL.h, and a forward declaration keeps the two headers from importing each other */

NS_ASSUME_NONNULL_BEGIN

/* Five codes, distinct from each other; the 64-bit marker Apple puts in its high word is NOT reproduced, so
 * these are THIS LIBRARY'S values rather than Mach-O cputype numbers (-executableArchitectures maps an ELF
 * e_machine onto them, which is the one place a header IS read here). */
typedef enum {
	NSBundleExecutableArchitectureI386 = 1,
	NSBundleExecutableArchitecturePPC = 2,
	NSBundleExecutableArchitectureX86_64 = 3,
	NSBundleExecutableArchitecturePPC64 = 4,
	NSBundleExecutableArchitectureARM64 = 5
} NSBundleExecutableArchitecture;

/* The notification a bundle posts when its code loads, and the userInfo key it carries (value == name, this
 * library's convention for these constants - see NSNotification.h). -load POSTS IT: when dlopen brings the
 * bundle's executable in, the classes it names under NSLoadedClasses arrive with this notification. */
extern NSNotificationName const NSBundleDidLoadNotification;
extern NSString *const NSLoadedClasses;

/* ---- THE CLASS, AND THE DEFINITION OF A BUNDLE THIS SYSTEM NOW USES ---------------------------------
 *
 * A BUNDLE IS A DIRECTORY THAT CONTAINS AN Info.plist, and the manifest is a REAL PROPERTY LIST read through
 * NSPropertyListSerialization. Two layouts are accepted, because Apple has both: a FLAT bundle
 * (Foo.app/Info.plist, the iOS shape) and a Contents/ bundle (Foo.app/Contents/Info.plist, the shape Apple
 * documents for a macOS application, with Contents/MacOS for the executable and Contents/Resources for the
 * resources). Nothing else about a directory makes it a bundle, and -initWithPath: answers nil when the
 * directory is not one.
 *
 * THE KEYS ARE APPLE'S: CFBundleIdentifier, CFBundleExecutable, CFBundleName, CFBundleLocalizations and
 * NSPrincipalClass. A bundle that is MISSING a key is not an error - every accessor below answers nil or an
 * empty collection - because Apple's NSBundle does the same and a caller is expected to cope.
 *
 * THE ONE THING THAT IS NOT A KEY: -load and -principalClass RUN CODE (dlopen on the bundle's executable, and
 * objc_getClass on NSPrincipalClass). That is the security-sensitive half of a bundle and it is why this class
 * takes the payload's own path rather than accepting a path from a caller.
 */
@interface NSBundle : NSObject
{
	NSMutableDictionary *_preservation;	/* §63.212: tag -> priority, made on first use */
	NSString *_path;		/* the bundle directory as it was given */
	NSString *_layout;		/* the directory holding Info.plist: the bundle, or its Contents/ */
	NSDictionary *_info;		/* the manifest, read once and kept */
	void *_handle;			/* dlopen's handle, or NULL when the code is not loaded */
}

/* The bundle the running program lives in: the nearest ancestor directory of the executable that IS a bundle,
 * found by walking up from /proc/self/cmdline's argv[0]. A program that is not inside a bundle gets a bundle
 * whose bundlePath is the executable's own directory and whose infoDictionary is nil, which is what Apple's
 * mainBundle does for a command-line tool. */
+ (NSBundle *)mainBundle;

+ (instancetype _Nullable)bundleWithPath:(NSString *)path;
- (instancetype _Nullable)initWithPath:(NSString *)path;

/* The bundles this process has OPENED, which is what Apple's +bundleWithIdentifier: searches. A bundle that
 * has never been created cannot be found by identifier, and the header says so rather than implying a search of
 * the file system. */
+ (NSBundle * _Nullable)bundleWithIdentifier:(NSString *)identifier;
+ (NSArray *)allBundles;

- (NSString *)bundlePath;
- (NSString * _Nullable)bundleIdentifier;
- (NSString * _Nullable)executablePath;
- (NSString * _Nullable)resourcePath;
- (NSDictionary * _Nullable)infoDictionary;
- (id _Nullable)objectForInfoDictionaryKey:(NSString *)key;
- (NSArray *)localizations;

/* THE STRING-TABLE DOORS (§62.108). THE CLASSIC ONE WAS MISSING, and that was a real defect rather than a
 * gap: the four `NSLocalizedString` macros have been SHIPPED for a long time (they are macros, so they are
 * not compiled until used) and every one of them calls this method — which no header declared and no
 * implementation defined. A caller who used one got a compile warning and an unrecognised selector at
 * runtime. It is here now, with Apple's own contract: the table's value for the key, else `value` when it is
 * non-empty, else THE KEY ITSELF.
 *
 * AND THE LOOKUP'S ONE BOUNDARY, named rather than implied: this library performs no LANGUAGE NEGOTIATION.
 * Apple's door consults the user's preferred languages against the bundle's `.lproj` directories; this one
 * reads the table the bundle's own resource lookup finds (`-pathForResource:ofType:`, which is flat). A
 * `.lproj`-aware search is a later slice, and until it lands a localized bundle should carry the table its
 * default language wants. */
- (NSString *)localizedStringForKey:(NSString *)key
			     value:(nullable NSString *)value
			     table:(nullable NSString *)tableName;

/* THE ATTRIBUTED DOOR (§62.108), which is what the four `NSLocalizedAttributedString` macros call: the same
 * lookup, with the localized value then PARSED AS MARKDOWN — which is Apple's contract for it and the reason
 * these four rows were owed for as long as they were. It answers a string rather than nil for the same reason
 * the classic door does: a missing key falls back to the value, then to the key. */
- (NSAttributedString *)localizedAttributedStringForKey:(NSString *)key
						 value:(nullable NSString *)value
						 table:(nullable NSString *)tableName;

- (NSString * _Nullable)pathForResource:(NSString * _Nullable)name ofType:(NSString * _Nullable)ext;
- (NSString * _Nullable)pathForResource:(NSString * _Nullable)name
				ofType:(NSString * _Nullable)ext
			   inDirectory:(NSString * _Nullable)subpath;
- (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
				     inDirectory:(NSString * _Nullable)subpath;

/* THE CODE-LOADING HALF. -load dlopens the bundle's executable and posts NSBundleDidLoadNotification with the
 * principal class's name under NSLoadedClasses; -principalClass loads first, which is Apple's behaviour; and
 * -unload answers NO when the runtime cannot unload a bundle it has already used (dlclose on a handle whose
 * classes are still registered does not take it back). */
- (BOOL)load;
- (BOOL)isLoaded;
- (_Nullable Class)principalClass;
- (BOOL)unload;

/* THE EXECUTABLE'S ARCHITECTURE, read from the file its own format puts it in: Apple scans a Mach-O, this
 * system's executables are ELF, so the door reads the ELF header's e_machine and answers the matching code.
 * nil for a bundle whose executable is absent, unreadable, or not an ELF - Apple's "no Mach-O executable"
 * case, answered the way THIS format says it. */
- (NSArray * _Nullable)executableArchitectures;

/* ---- CREATING A BUNDLE FROM A CLASS OR A URL ------------------------------------------------------
 *
 * +bundleForClass: is Apple's "the bundle that provided this class". libobjc2 exposes `objc_getClassList`
 * but not `class_getImageName`, so the class's IMAGE is found with `dladdr` on the class object (ELF resolves
 * it to the shared object the linker put the class in), and the open bundle whose path is a prefix of that
 * image is the answer; a bundle that NAMES the class as its NSPrincipalClass is matched first, without an
 * image walk. A class whose image lies under no open bundle answers nil, and NO bundle is opened to find one:
 * Apple's door returns an already-associated bundle, which is what this one does.
 *
 * +bundleWithURL: and -initWithURL: are the URL spellings of the path doors: the URL's `-path` is the path,
 * so a URL that is not a file URL (its `-path` is nil) answers nil. */
+ (NSBundle * _Nullable)bundleForClass:(Class)aClass;
+ (NSBundle * _Nullable)bundleWithURL:(NSURL *)url;
- (instancetype _Nullable)initWithURL:(NSURL *)url;

/* ---- THE STANDARD BUNDLE DIRECTORIES AND THEIR URLS --------------------------------------------------
 *
 * Each names a directory the bundle layout defines, relative to the LAYOUT directory (the bundle itself for a
 * flat bundle, its Contents/ for a Contents bundle). ONE RULE IS SHARED AND IS A CHOICE: a directory door
 * answers its path ONLY WHEN THAT DIRECTORY EXISTS, and nil otherwise -- Apple's "the bundle's X directory" is
 * nil for a bundle that has none, rather than a path to a directory that is not there. The exceptions are
 * -bundleURL and -resourceURL, whose paths are the bundle's own and always answer, and -executableURL, which
 * follows the manifest's CFBundleExecutable whether or not the file is present (the manifest, not the disk, is
 * its source) -- the same split -executablePath already uses. */
- (NSURL *)bundleURL;
- (NSURL * _Nullable)executableURL;
- (NSURL *)resourceURL;
- (NSString * _Nullable)pathForAuxiliaryExecutable:(NSString *)executableName;
- (NSURL * _Nullable)URLForAuxiliaryExecutable:(NSString *)executableName;
- (NSString * _Nullable)builtInPlugInsPath;
- (NSURL * _Nullable)builtInPlugInsURL;
- (NSString * _Nullable)privateFrameworksPath;
- (NSURL * _Nullable)privateFrameworksURL;
- (NSString * _Nullable)sharedFrameworksPath;
- (NSURL * _Nullable)sharedFrameworksURL;
- (NSString * _Nullable)sharedSupportPath;
- (NSURL * _Nullable)sharedSupportURL;

/* ---- THE CLASS-METHOD RESOURCE DOORS, WHICH SEARCH THE MAIN BUNDLE -----------------------------------
 *
 * Apple's `+pathForResource:ofType:inDirectory:` and `+pathsForResourcesOfType:inDirectory:` are the class
 * spelling of the instance doors and search the MAIN bundle. The two `...inBundleWithURL:` doors take the
 * bundle to search as a URL: its path is read through this class's own bundle definition, so a URL that is not
 * a bundle answers nil (or an empty array) rather than a lookup into nothing. */
+ (NSString * _Nullable)pathForResource:(NSString * _Nullable)name
				ofType:(NSString * _Nullable)ext
			   inDirectory:(NSString * _Nullable)subpath;
+ (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
			 inDirectory:(NSString * _Nullable)subpath;
+ (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
		       subdirectory:(NSString * _Nullable)subpath
		  inBundleWithURL:(NSURL *)bundleURL;
+ (NSArray *)URLsForResourcesWithExtension:(NSString * _Nullable)ext
			      subdirectory:(NSString * _Nullable)subpath
			 inBundleWithURL:(NSURL *)bundleURL;

/* ---- THE LOCALIZATION-AWARE RESOURCE DOORS -----------------------------------------------------------
 *
 * These take an explicit LOCALIZATION and search that localization's `.lproj` directory first, falling back to
 * the flat lookup when it holds no such file. THE BOUNDARY IS THE ONE -localizedStringForKey:...: NAMES: this
 * library performs no NEGOTIATION, so a caller that wants "the language the user wants" asks
 * +preferredLocalizationsFromArray: and passes the answer here. */
- (NSString * _Nullable)pathForResource:(NSString * _Nullable)name
				ofType:(NSString * _Nullable)ext
			   inDirectory:(NSString * _Nullable)subpath
		       forLocalization:(NSString * _Nullable)localization;
- (NSArray *)pathsForResourcesOfType:(NSString * _Nullable)ext
			 inDirectory:(NSString * _Nullable)subpath
		     forLocalization:(NSString * _Nullable)localization;
- (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
		       subdirectory:(NSString * _Nullable)subpath
		       localization:(NSString * _Nullable)localization;
- (NSArray *)URLsForResourcesWithExtension:(NSString * _Nullable)ext
			      subdirectory:(NSString * _Nullable)subpath
			      localization:(NSString * _Nullable)localization;
- (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext;
- (NSURL * _Nullable)URLForResource:(NSString * _Nullable)name
		      withExtension:(NSString * _Nullable)ext
		       subdirectory:(NSString * _Nullable)subpath;
- (NSArray *)URLsForResourcesWithExtension:(NSString * _Nullable)ext
			      subdirectory:(NSString * _Nullable)subpath;

/* ---- THE IMAGE AND SOUND RESOURCE DOORS ARE NOT HERE (MOVED 2026-10-01, §63.52) --------------------
 *
 * They were `-pathForImageResource:`, `-URLForImageResource:` and `-pathForSoundResource:`, and the user's
 * directive — *"removing from our Foundation implementation anything which does not belong in Foundation"* —
 * is why they are gone: **AppKit's `NSImage.h` declares the image pair and AppKit's `NSSound.h` declares the
 * sound door** (measured against the macOS 14.5 SDK headers), because what counts as an image or a sound
 * resource is decided by classes that are not Foundation's. **Apple's documentation index has NO ROWS for any
 * of the three**, which is why the ledger never asked for them and why they sat here unchallenged. They now
 * live in `userland/AppKit/NSBundleAdditions.h`. */

/* ---- LOCALIZATION INFORMATION, AND A LOCALIZED STRING OVER AN EXPLICIT LIST --------------------------
 *
 * +preferredLocalizationsFromArray: answers which of the given localizations this system would use, matching
 * them against the current locale's identifier and language; with NO match it answers the array unchanged,
 * which is Apple's best-effort fallback. The `forPreferences:` door does the same over an explicit list. The
 * source of "the user's languages" is the ONE this library has -- NSLocale's current identifier -- and the
 * header names that rather than implying a full preferred-languages list. */
+ (NSArray *)preferredLocalizationsFromArray:(NSArray *)localizationsArray;
+ (NSArray *)preferredLocalizationsFromArray:(NSArray *)localizationsArray
			      forPreferences:(nullable NSArray *)preferencesArray;
- (NSString * _Nullable)developmentLocalization;
- (NSDictionary * _Nullable)localizedInfoDictionary;
- (NSArray *)preferredLocalizations;
- (NSString *)localizedStringForKey:(NSString *)key
			     value:(nullable NSString *)value
			     table:(nullable NSString *)tableName
		     localizations:(nullable NSArray *)localizations;

/* ---- CLASS LOOKUP, AND CODE LOADING THAT REPORTS ITS ERROR -------------------------------------------
 *
 * -classNamed: answers a class THE BUNDLE PROVIDES: the runtime is asked for the name, and the class is
 * answered only when dladdr places its image under this bundle's path (when dladdr cannot place it the class
 * is answered, which is as much as this system can say). -loadAndReturnError: and -preflightAndReturnError:
 * are the error-reporting forms of -load: a bundle that names no loadable executable fills `error` with an
 * NSCocoaErrorDomain / NSFileNoSuchFileError and answers NO. */
- (_Nullable Class)classNamed:(NSString *)className;
- (BOOL)loadAndReturnError:(NSError **)error;
- (BOOL)preflightAndReturnError:(NSError **)error;


/* §63.212: THE TAG-PRESERVATION PAIR. Apple's door tells a bundle's resource loader to keep the resources
 * carrying these tags; the VALUES are validated here (a priority outside 0.0-1.0 raises, as Apple documents)
 * and stored, and the reading is stated where they are stored: this tree has no on-demand resource loader yet,
 * so nothing consults them — the same honest shape NSThread's qualityOfService uses. */
- (double)preservationPriorityForTag:(NSUInteger)tag;
- (void)setPreservationPriority:(double)priority forTags:(NSIndexSet *)tags;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSBUNDLE_H */
