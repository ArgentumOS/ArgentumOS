/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFileManager — the file system, as a service. F13.14, docs/design/foundation-plan.md §10.
 *
 * THE SHAPE OF THE API IS WHAT MAKES IT A SERVICE RATHER THAN A WRAPPER, and there are three parts
 * to it that a wrapper would not have:
 *
 *   1. AN ERROR, NOT AN ERRNO. Every operation that can fail answers NO and, when the caller offers
 *      one, fills in an `NSError` whose code is the POSIX `errno` and whose description is that
 *      errno's own text. The errno itself is never left for the caller to read out of a global;
 *   2. ATTRIBUTES AS A DICTIONARY, keyed by Cocoa's names (NSFileType, NSFileSize,
 *      NSFileModificationDate, NSFilePosixPermissions, the owner ids), so a caller asks ONE question
 *      and gets a description rather than calling stat(2) and decoding a bitfield;
 *   3. A COPY that recurses. POSIX has no copy — an operation this central is spelled out here, and
 *      it is the reason the directory walk exists in this file at all.
 *
 * PATHS ARE POSIX PATHS, as they are throughout this library (NSURL's file forms are F8's business),
 * and this system's paths are FSH paths — /System/Applications, /System/Temporary Files — with the
 * spaces and capitals they are spelled with. Nothing here translates them.
 *
 * WHAT IS NOT HERE, named: NSURL-taking forms (they promise URL RESOURCE VALUES, which the ledger
 * still owes as a family of their own - §60 says why they wait), extended attributes, and mounting.
 * Each is a real part of Cocoa's NSFileManager and none of them is half-built here.
 *
 * AND WHAT WAS ON THAT LIST AND IS NOT ANY MORE: `-enumeratorAtPath:`, `-subpathsAtPath:` and
 * `-subpathsOfDirectoryAtPath:error:` (W8 slice 1, foundation-plan.md §60). The line used to read
 * "the walk is here, the enumerator objects are not" - the walk was this file's private recursion for
 * -copyItemAtPath:, and NSDirectoryEnumerator is now the walk's public form. AND, in slice 2, THE
 * DELEGATE: `NSFileManagerDelegate` (all 16 selectors) plus `-delegate`/`-setDelegate:`, which the
 * same sentence used to name as absent.
 */

#ifndef FOUNDATION_NSFILEMANAGER_H
#define FOUNDATION_NSFILEMANAGER_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSData;
@class NSDate;
@class NSDictionary;
@class NSDirectoryEnumerator;
@class NSError;
@class NSString;
@class NSURL;

/* SAID BEFORE THE PROTOCOL, because that protocol's methods take the manager as their first argument
 * and the @interface further down has not been read yet at that point. */
@class NSFileManager;

NS_ASSUME_NONNULL_BEGIN

/* Cocoa's attribute keys, spelled as Cocoa spells them — the VALUES are the names. */
extern NSString *const NSFileType;
extern NSString *const NSFileSize;
extern NSString *const NSFileModificationDate;
extern NSString *const NSFilePosixPermissions;
extern NSString *const NSFileOwnerAccountID;
extern NSString *const NSFileGroupOwnerAccountID;
/* W8 slice 3d: the same two facts as NAMES. They read through the account database and they WRITE
 * through it as well (-setAttributes: resolves them with getpwnam/getgrnam). */
extern NSString *const NSFileOwnerAccountName;
extern NSString *const NSFileGroupOwnerAccountName;

/* ---- THE FLAG KEYS WITH NO SUBSTRATE HERE, AND THEY ARE PUBLISHED ANYWAY (W8 slice 3e) ----------
 *
 * EVERY ONE OF THESE IS A NAME WHOSE DICTIONARY ENTRY CAN ONLY BE ABSENT ON THIS SYSTEM, and that is
 * the same position NSFileCreationDate already holds: an absent key is how a file system says it has no
 * such attribute, which is exactly what a caller sees on an Apple volume that does not keep one. They
 * are declared because the API surface is the specification - a caller may look any of them up, and a
 * header that omits them would be a hole rather than a boundary. THE GROUND, ONE LINE EACH:
 */
extern NSString *const NSFileImmutable;		/* UF_IMMUTABLE: this kernel has no chflags(2) */
extern NSString *const NSFileAppendOnly;	/* UF_APPEND likewise */
extern NSString *const NSFileBusy;		/* the Finder's busy bit, which is not a file-system fact */
extern NSString *const NSFileExtensionHidden;	/* ... nor is the Finder's extension-hiding bit */
extern NSString *const NSFileHFSCreatorCode;	/* no HFS here, so no creator code to report */
extern NSString *const NSFileHFSTypeCode;	/* ... nor an HFS type code */
extern NSString *const NSFileProtectionKey;	/* no data-protection classes on this system */
extern NSString *const NSFileProtectionComplete;
extern NSString *const NSFileProtectionCompleteUnlessOpen;
extern NSString *const NSFileProtectionCompleteUntilFirstUserAuthentication;
extern NSString *const NSFileProtectionNone;

/* AND THE THREE TYPED ALIASES Apple spells for these dictionaries, which are the reason its own
 * signatures read as typed dictionaries rather than as `NSDictionary *`. */
typedef NSString *NSFileAttributeKey;
typedef NSString *NSFileAttributeType;
typedef NSString *NSFileProtectionType;

/* ---- THE KEYS THE SUBSTRATE CAN ANSWER (W8 slice 3c, foundation-plan.md §60) --------------------
 *
 * THREE MORE ITEM KEYS, each named by Apple's own page as the stat(2) FIELD ITSELF - "the value of
 * st_ino, as returned by stat(2)" - so there is nothing to interpret: the inode, the link count, and
 * the device. They are filled by -attributesOfItemAtPath: along with the six above.
 */
extern NSString *const NSFileSystemFileNumber;	/* st_ino */
extern NSString *const NSFileReferenceCount;	/* st_nlink */
extern NSString *const NSFileDeviceIdentifier;	/* st_dev */

/* AND THE FIVE FILE-SYSTEM KEYS, which -attributesOfFileSystemForPath:error: answers with. Two units
 * are worth stating twice, because Apple states them: the SIZES ARE BYTES ("the size of the file
 * system in bytes"), and the NUMBER is `st_dev` ("the value corresponds to the value of st_dev, as
 * returned by stat(2)") rather than the statfs(2) field one would reach for first. */
extern NSString *const NSFileSystemSize;
extern NSString *const NSFileSystemFreeSize;
extern NSString *const NSFileSystemNodes;
extern NSString *const NSFileSystemFreeNodes;
extern NSString *const NSFileSystemNumber;

/* A KEY THAT IS PUBLISHED AND NEVER FILLED, and that is a fact about this substrate rather than an
 * omission: Apple's dictionary simply has no entry when the file system keeps no creation time, and
 * this one keeps only the modification time and the inode change time (musl's `struct stat` has no
 * birth time at all). The NAME is still Cocoa's API - a caller may look it up - so it is declared
 * here and left absent from every dictionary this class builds. */
extern NSString *const NSFileCreationDate;

/* ... and the values NSFileType takes, which a caller compares against. A file that is neither a
 * directory, a regular file nor a link is now NAMED rather than called unknown: the three below were
 * missing, and the kernel's stat(2) answers all three. */
extern NSString *const NSFileTypeRegular;
extern NSString *const NSFileTypeDirectory;
extern NSString *const NSFileTypeSymbolicLink;
extern NSString *const NSFileTypeBlockSpecial;
extern NSString *const NSFileTypeCharacterSpecial;
extern NSString *const NSFileTypeSocket;
extern NSString *const NSFileTypeUnknown;

/* ---- THE DELEGATE (W8 slice 2, foundation-plan.md §60) ----------------------------------------
 *
 * EVERY MEMBER IS OPTIONAL ("The NSFileManagerDelegate protocol defines optional methods for managing
 * operations involving the copying, moving, linking, or removal of files and directories"), and the
 * file manager asks whether an operation "should begin at all" and whether it "should proceed when an
 * error occurs". FOUR RULES, each taken from Apple's own pages:
 *
 *   THE URL FORM IS PREFERRED, NOT MERELY ACCEPTED: "the file manager always prefers methods that take
 *   an NSURL object over those that take an NSString object" - so each family asks its URL selector
 *   first and its path selector only when the URL one is absent;
 *
 *   THE QUESTION IS ASKED ONCE PER ITEM for a copy and a remove - "for a directory, this method is
 *   called once for the directory and once for each item in the directory" - and ONLY FOR THE ITEM
 *   ITSELF for a move, which Apple states as the difference: "if the item being moved is a directory,
 *   the file manager notifies the delegate only for the directory itself and not for any of its
 *   contents";
 *
 *   A VETO (NO) SKIPS THE ITEM, and for a directory that means its contents too, because the recursion
 *   is never entered - Apple's own sentence for the remove case says exactly that ("returning NO
 *   prevents both the directory and its children from being deleted"). The doctrine is stated in full
 *   where it is implemented, including why a veto is a SKIP and not a failure;
 *
 *   THE ERROR DOOR IS ASKED ONLY WHEN IT EXISTS, and YES means the error is IGNORED ("the file manager
 *   continues copying any other items and ignores the error"). An absent door leaves the error
 *   standing, which is what keeps a delegate-less file manager behaving exactly as it did before.
 */
@protocol NSFileManagerDelegate <NSObject>

@optional

/* COPYING. */
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldCopyItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldCopyItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
  copyingItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
   copyingItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL;

/* MOVING - the item itself, and NOT its contents. */
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldMoveItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldMoveItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
   movingItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
    movingItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL;

/* REMOVING. */
- (BOOL)fileManager:(NSFileManager *)fileManager shouldRemoveItemAtPath:(NSString *)path;
- (BOOL)fileManager:(NSFileManager *)fileManager shouldRemoveItemAtURL:(NSURL *)URL;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
 removingItemAtPath:(NSString *)path;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
  removingItemAtURL:(NSURL *)URL;

/* LINKING. */
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldLinkItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldLinkItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
  linkingItemAtPath:(NSString *)srcPath
	     toPath:(NSString *)dstPath;
- (BOOL)fileManager:(NSFileManager *)fileManager
shouldProceedAfterError:(NSError *)error
   linkingItemAtURL:(NSURL *)srcURL
	      toURL:(NSURL *)dstURL;

@end

/* HOW ONE ITEM RELATES TO ANOTHER, declared HERE rather than with the search-path types below, because
 * `-getRelationship:ofDirectoryAtPath:toItemAtPath:error:` NAMES IT in its signature: a type a method
 * takes has to be read before that method, and the rest of the enums in this header are read after the
 * @interface. "Is it inside, is it the same, or neither" - Apple's own three answers. */
typedef enum {
	NSURLRelationshipContains = 0,
	NSURLRelationshipSame = 1,
	NSURLRelationshipOther = 2
} NSURLRelationship;

typedef enum {
	NSVolumeEnumerationSkipHiddenVolumes = 1 << 0,
	NSVolumeEnumerationProduceFileReferenceURLs = 1 << 1
} NSVolumeEnumerationOptions;

@interface NSFileManager : NSObject
{
	/* ASSIGN, NOT WEAK, and that is MEASURED rather than chosen: Apple's own declaration comes back
	 * from its page as `unowned(unsafe) var delegate`, which is an unretained, NON-ZEROING reference.
	 * The delegate must outlive the manager. */
	id <NSFileManagerDelegate> _delegate;
}

+ (NSFileManager *)defaultManager;

/* Apple's own advice, kept because it is a hazard and not a style note: "assign a delegate to the file
 * manager object only if you allocated and initialized the object yourself. Avoid assigning a delegate
 * to the shared file manager" - a delegate on +defaultManager is consulted by EVERY caller of the
 * process. The default value is nil. */
- (nullable id <NSFileManagerDelegate>)delegate;
- (void)setDelegate:(nullable id <NSFileManagerDelegate>)delegate;

/* EXISTENCE, and the second door answers the question a caller usually means by it. */
- (BOOL)fileExistsAtPath:(NSString *)path;
- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(nullable BOOL *)isDirectory;
- (BOOL)isReadableFileAtPath:(NSString *)path;
- (BOOL)isWritableFileAtPath:(NSString *)path;
- (BOOL)isExecutableFileAtPath:(NSString *)path;
- (BOOL)isDeletableFileAtPath:(NSString *)path;

/* THE NAMES IN A DIRECTORY, not the full paths: Cocoa's answer, and the caller joins them. */
- (nullable NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError ** _Nullable)error;

/* ---- THE DEEP WALK (W8 slice 1, foundation-plan.md §60), AND ITS THREE ANSWERS ARE THREE RULES ----
 *
 * -enumeratorAtPath: hands back an NSDirectoryEnumerator whose ITEMS ARE RELATIVE TO `path` (Apple's
 * own class page: "These pathnames are relative to the directory") and whose -level counts THIS
 * directory as 0, so its immediate children are 1. A path that names a FILE is not an error and does
 * not answer nil: Apple's word is that the enumerator then "enumerates no files - the first call to
 * -nextObject will return nil". A nil PATH is the one case that answers nil.
 *
 * -subpathsAtPath: and -subpathsOfDirectoryAtPath:error: are the same walk collected into an array -
 * the items in the whole subtree, in the SAME relative form, with the same refusal to recurse through
 * a symlinked directory - and they differ only in the error channel, which is exactly how Apple
 * describes the pair ("In macOS 10.5 and later, use -subpathsOfDirectoryAtPath:error: instead"):
 * NEITHER is deprecated, so both ship. A path that cannot be OPENED AS A DIRECTORY - a file, a name
 * that is not there - answers nil rather than an empty array, which is also what
 * -contentsOfDirectoryAtPath:error: does with the same path, and one of the two doors fills in the
 * error while the other has none to fill.
 */
- (nullable NSDirectoryEnumerator *)enumeratorAtPath:(NSString *)path;
- (nullable NSArray *)subpathsAtPath:(NSString *)path;
- (nullable NSArray *)subpathsOfDirectoryAtPath:(NSString *)path error:(NSError ** _Nullable)error;

- (BOOL)createDirectoryAtPath:(NSString *)path
  withIntermediateDirectories:(BOOL)createIntermediates
		   attributes:(nullable NSDictionary *)attributes
			error:(NSError ** _Nullable)error;
- (BOOL)createFileAtPath:(NSString *)path
		contents:(nullable NSData *)contents
	      attributes:(nullable NSDictionary *)attributes;

- (BOOL)removeItemAtPath:(NSString *)path error:(NSError ** _Nullable)error;
- (BOOL)moveItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error;
- (BOOL)copyItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error;

/* A HARD LINK, and it lands with the delegate rather than after it because the protocol's LINKING
 * family (four selectors) would otherwise have no caller at all - a declared question nobody can ever
 * be asked. link(2) is the whole implementation: it makes a second NAME for an inode, which is a
 * different operation from -createSymbolicLinkAtPath: (that one makes a new inode that points at a
 * path, and it is slice 3's row, not this one's). */
- (BOOL)linkItemAtPath:(NSString *)sourcePath
		toPath:(NSString *)destinationPath
		 error:(NSError ** _Nullable)error;

/* ---- A COPY AND A MOVE REFUSE AN EXISTING DESTINATION (W8 slice 3, AND BOTH ARE FIXES) ------------
 *
 * Apple says it in the copy's own discussion - "if a file with the same name already exists at dstPath,
 * this method STOPS THE COPY ATTEMPT AND RETURNS AN APPROPRIATE ERROR" - and says the same about a
 * move ("if an item with the same name already exists at dstPath, this method stops the move attempt
 * and returns an appropriate error"). THIS CLASS DID NEITHER UNTIL THIS SLICE: the copy wrote through
 * O_CREAT|O_TRUNC, silently REPLACING the file it found, and the move went through rename(2), which
 * replaces a destination file BY DESIGN. Both are data loss the caller cannot see coming, so the
 * destination is lstat(2)ed and an existing item is EEXIST - which is also the case Apple's
 * `shouldProceedAfterError:` doors exist for.
 */

/* A SYMBOLIC LINK (the other kind of link, and the difference matters): link(2) makes a second name for
 * an INODE, symlink(2) makes a NEW inode whose content is a path. Apple's own description is why the
 * target is never resolved here: "this method does not traverse symbolic links contained in `path`,
 * making it possible to create symbolic links to locations that DO NOT YET EXIST". */
- (BOOL)createSymbolicLinkAtPath:(NSString *)path
	     withDestinationPath:(NSString *)destPath
			   error:(NSError ** _Nullable)error;

/* THE FILE'S BYTES, and the exclusion is Apple's: a DIRECTORY answers nil ("if `path` specifies a
 * directory, or if some other error occurs, this method returns nil"), and there is no error channel -
 * this door has none in Cocoa either. The read follows a link, because what it answers is the CONTENTS
 * of the file the path names. */
- (nullable NSData *)contentsAtPath:(NSString *)path;

/* THE THREE-STEP RULE, IN APPLE'S OWN ORDER: "for files, this method checks to see if they're the same
 * file, then compares their size, and finally compares their contents"; directories are compared as
 * "the list of files and subdirectories each contains - contents of subdirectories are also compared";
 * and it "does not traverse symbolic links, but compares the links themselves", so two links are equal
 * when they point at the same target and a link never equals the file it points at. */
- (BOOL)contentsEqualAtPath:(NSString *)path1 andPath:(NSString *)path2;

- (nullable NSDictionary *)attributesOfItemAtPath:(NSString *)path
					    error:(NSError ** _Nullable)error;

/* THE MUTATOR (W8 slice 3d), and THREE OF APPLE'S OWN SENTENCES ARE ITS WHOLE DESIGN:
 *
 *   1. "THE METHOD ATTEMPTS TO MAKE ALL CHANGES SPECIFIED IN ATTRIBUTES AND IGNORES ANY REJECTION OF
 *      AN ATTEMPTED MODIFICATION" - so the error channel is about the ITEM (no such path, no
 *      dictionary) and never about a chmod(2) the kernel refused;
 *   2. "IF THE LAST COMPONENT OF THE PATH IS A SYMBOLIC LINK, THE SYSTEM TRAVERSES IT" - which is the
 *      one word that separates this door from -attributesOfItemAtPath: (that one asks a LINK about
 *      itself; this one acts on what the link NAMES);
 *   3. the NAME keys are honoured "only when NSFileType specifies a file" - so
 *      NSFileOwnerAccountName/NSFileGroupOwnerAccountName take effect only alongside
 *      NSFileType = NSFileTypeRegular, while the ID keys carry no such condition.
 */
- (BOOL)setAttributes:(NSDictionary *)attributes
	ofItemAtPath:(NSString *)path
	       error:(NSError ** _Nullable)error;

/* THE DISPLAY NAME (W8 slice 3e), and its rule HERE is a decision with two grounds. Apple: "the name of
 * the file or directory at path in a LOCALIZED FORM appropriate for presentation to the user", and the
 * discussion adds that display names "MAY also reflect other modifications, such as the removal of
 * filename extensions". THIS SYSTEM HAS NO LOCALIZATION DATABASE - there is no `.lproj` anywhere and no
 * language setting for a name to be looked up in - so a localized name HAS no value to take other than
 * the item's own, and that "MAY" is what makes the choice conforming rather than a shortcut. The
 * failure case is Apple's own sentence and it is exact: "if there is no file or directory at path, or if
 * an error occurs, RETURNS path AS IS" - the whole path, not a component of it. (Apple annotates this
 * NONNULL; a nil path is the one case its annotation does not cover, so the annotation here is the
 * permissive one.) */
- (nullable NSString *)displayNameAtPath:(NSString *)path;

/* AND THE SAME RULE COMPONENT BY COMPONENT: "an array of NSString objects representing the user-visible
 * components of path", and "returns nil if path does not exist" - so the two doors agree about the
 * failure case (one answers the path, the other nothing) and about doing no localization. ONE
 * DOCUMENTED DIFFERENCE from Apple's own example, whose first element is the VOLUME's name: this
 * system's paths begin at the root and a volume has no name here, so the array is the path's own
 * components with no synthesised first element. */
- (nullable NSArray *)componentsToDisplayForPath:(NSString *)path;

- (nullable NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path
						 error:(NSError ** _Nullable)error;

/* THE FILE SYSTEM'S OWN NUMBERS (W8 slice 3c), from statfs(2) - and Apple's sentence about what a
 * dictionary means is what makes the missing entries honest rather than lazy: a key that is ABSENT is
 * how a file system says it has no such attribute, so a caller sees the same thing here as it would on
 * any Apple volume that does not keep it. "This method does not traverse a terminal symbolic link",
 * which is why the lookup is lstat(2) first. */
/* A DIRECTORY LISTED AS URLs, WITH THE VALUES A CALLER ASKED FOR ALREADY IN THEM (W8 slice 6d).
 *
 * APPLE'S RULES, MEASURED FROM THE PAGE AND ALL THREE IMPLEMENTED: the result is an array of NSURL
 * objects, "each of which identifies a file, directory, or symbolic link contained in" the directory; an
 * EMPTY DIRECTORY ANSWERS AN EMPTY ARRAY rather than nil (only an error answers nil); and the listing
 * "does not return URLs for the current directory (\".\"), parent directory (\"..\"), or resource forks
 * (files that begin with \"._\") but it DOES return other hidden files" - so the dot rule and the `._` rule
 * are both tested FROM BOTH SIDES, and `includingPropertiesForKeys:` is the reason the door exists: those
 * values are PREFETCHED into each URL (NSURL's private prefetch door), which the probe proves by changing
 * a file AFTER the listing and watching the listed URL answer the OLD value.
 *
 * THE ORDER OF THE RESULT IS UNDEFINED, which is Apple's own sentence and therefore nothing any check may
 * assert on.
 */
- (nullable NSArray *)contentsOfDirectoryAtURL:(NSURL *)url
		     includingPropertiesForKeys:(nullable NSArray *)keys
					options:(NSUInteger)options
					  error:(NSError ** _Nullable)error;

/* "Returns the URLs of the mounted volumes", with the keys a caller asks for PREFETCHED into each - the
 * volume-list half of W8p, and the door whose option set is declared below with it. On this system the table
 * is `/proc/mounts` (measured: `device mountpoint fstype rw|ro 0 0` per line), and
 * NSVolumeEnumerationSkipHiddenVolumes is CARRIED because this system has no hidden volumes to skip - the
 * table lists what is mounted and nothing else. */
- (nullable NSArray *)mountedVolumeURLsIncludingResourceValuesForKeys:(nullable NSArray *)propertyKeys
							      options:(NSVolumeEnumerationOptions)options;

/* THE DEEP WALK AS A DOOR (W8 slice 6e), which is where Apple puts it: the enumerator CLASS is the
 * cursor, and the manager is what knows a directory.
 *
 * APPLE'S SENTENCES FOR THIS DOOR, ALL MEASURED FROM ITS PAGE: the enumerator deep-enumerates "the
 * contents of the directory at" the URL; "the values for these keys are cached in the corresponding NSURL
 * objects"; the handler is "an optional error handler block ... the handler block should return true if
 * you want the enumeration to continue or false if you want the enumeration to stop"; and "if url is a
 * filename, the method returns an enumerator object that enumerates no files - the first call to
 * -nextObject returns nil", which is why a FILE URL answers a SPENT ENUMERATOR rather than nil. A URL
 * that is not a file URL at all is refused with nil, and that is OURS: Apple's sentence covers a
 * filename, and this door has no path to walk without one.
 */
- (nullable NSDirectoryEnumerator *)enumeratorAtURL:(NSURL *)url
			  includingPropertiesForKeys:(nullable NSArray *)keys
					     options:(NSUInteger)options
					errorHandler:(nullable BOOL (^)(NSURL *url, NSError *error))handler;

/* THE LOOKUP FOR A FILE PROVIDER'S SERVICES (W8 slice 9), and it is Apple's own home for it: the SERVICE
 * object's page has two members and neither of them is this one. What this system answers is EMPTY, and
 * that is the postcondition rather than a degradation - a file provider extension is a subsystem this
 * system does not have, so there is no service to return for any item. The handler IS CALLED, so a caller
 * learns the answer instead of waiting for one. */
- (void)getFileProviderServicesForItemAtURL:(NSURL *)url
			  completionHandler:(void (^)(NSDictionary * _Nullable services,
						      NSError * _Nullable error))completionHandler;

- (nullable NSDictionary *)attributesOfFileSystemForPath:(NSString *)path
						   error:(NSError ** _Nullable)error;

/* WHERE ONE ITEM STANDS RELATIVE TO A DIRECTORY, in three answers: "the directory may CONTAIN the item,
 * it may be the SAME as the item, or it may not have a DIRECT relationship to the item." The directory
 * is the first argument in Apple's spelling (and the out-parameter is first in the selector), and the
 * comparison is a PATH one - this door is about locations, not about inodes. */
- (BOOL)getRelationship:(NSURLRelationship *)outRelationship
      ofDirectoryAtPath:(NSString *)directory
	    toItemAtPath:(NSString *)otherPath
		   error:(NSError ** _Nullable)error;

- (nullable NSString *)currentDirectoryPath;
- (BOOL)changeCurrentDirectoryPath:(NSString *)path;

@end

/*
 * THE ENUMERATION AND SEARCH TYPES (2026-09-20). Cocoa declares these here, and a
 * program that includes NSFileManager.h expects to find them here, so this is where
 * they live.
 *
 * THE CASE NAMES ARE APPLE'S, LOOKED UP RATHER THAN REMEMBERED: they come from the
 * documentation index's own nesting of each enumerator under its type (107 of
 * Foundation's 120 open enums resolve that way in one pass), and they match the
 * ledger's `case` rows name for name — a cross-check, not an assumption.
 *
 * THE VALUES ARE OURS, and that is §11.6.1 D2 rather than laziness: Apple publishes
 * these case NAMES and no numbers, GNUstep's reference documents the types as
 * `typedef NSInteger X;` with "Description forthcoming", and the ledger has no value
 * column at all. The choices follow one rule — a type whose name says Options,
 * Controls or Mask is a BIT SET (1 << n), anything else counts up from zero — which
 * is why the same names mean the same thing here as there.
 */

typedef enum {
	NSDirectoryEnumerationSkipsSubdirectoryDescendants = 1 << 0,
	NSDirectoryEnumerationSkipsPackageDescendants = 1 << 1,
	NSDirectoryEnumerationSkipsHiddenFiles = 1 << 2,
	NSDirectoryEnumerationIncludesDirectoriesPostOrder = 1 << 3,
	NSDirectoryEnumerationProducesRelativePathURLs = 1 << 4
} NSDirectoryEnumerationOptions;

/* (the NSVolumeEnumerationOptions declaration moved ABOVE the interface: the volume door takes it, and a
 * declaration used before it is declared is a compile error - the second time in this family that a
 * declaration's PLACE was the bug.) */

/* What -replaceItemAtURL:...: may do with the item it replaces. */
typedef enum {
	NSFileManagerItemReplacementUsingNewMetadataOnly = 1 << 0,
	NSFileManagerItemReplacementWithoutDeletingBackupItem = 1 << 1
} NSFileManagerItemReplacementOptions;

/* What happens to LOCAL changes when a synced item is resumed — a choice, not a set. */
typedef enum {
	NSFileManagerResumeSyncBehaviorPreserveLocalChanges = 0,
	NSFileManagerResumeSyncBehaviorAfterUploadWithFailOnConflict = 1,
	NSFileManagerResumeSyncBehaviorDropLocalChanges = 2
} NSFileManagerResumeSyncBehavior;

typedef enum {
	NSFileManagerSupportedSyncControlsPauseSync = 1 << 0,
	NSFileManagerSupportedSyncControlsFailUploadOnConflict = 1 << 1
} NSFileManagerSupportedSyncControls;

typedef enum {
	NSFileManagerUploadConflictPolicyDefault = 0,
	NSFileManagerUploadConflictPolicyFailOnConflict = 1
} NSFileManagerUploadLocalVersionConflictPolicy;

/* Unmounting: eject everything, or leave the user interface alone. */
typedef enum {
	NSFileManagerUnmountAllPartitionsAndEjectDisk = 1 << 0,
	NSFileManagerUnmountWithoutUI = 1 << 1
} NSFileManagerUnmountOptions;

/*
 * THE SEARCH PATH TYPES, and the one place where Apple's own value choices are
 * visible in the API's shape rather than in a header: -URLsForDirectory:inDomains:
 * takes the domain mask and ORs the bits together, so NSSearchPathDomainMask is a
 * bit set even though its cases do not say "Options".
 */
typedef enum {
	NSApplicationDirectory = 0,
	NSDemoApplicationDirectory,
	NSDeveloperApplicationDirectory,
	NSAdminApplicationDirectory,
	NSLibraryDirectory,
	NSDeveloperDirectory,
	NSUserDirectory,
	NSDocumentationDirectory,
	NSDocumentDirectory,
	NSCoreServiceDirectory,
	NSAutosavedInformationDirectory,
	NSDesktopDirectory,
	NSCachesDirectory,
	NSApplicationSupportDirectory,
	NSDownloadsDirectory,
	NSInputMethodsDirectory,
	NSMoviesDirectory,
	NSMusicDirectory,
	NSPicturesDirectory,
	NSPrinterDescriptionDirectory,
	NSSharedPublicDirectory,
	NSPreferencePanesDirectory,
	NSApplicationScriptsDirectory,
	NSItemReplacementDirectory,
	NSAllApplicationsDirectory,
	NSAllLibrariesDirectory,
	NSTrashDirectory
} NSSearchPathDirectory;

typedef enum {
	NSUserDomainMask = 1 << 0,
	NSLocalDomainMask = 1 << 1,
	NSNetworkDomainMask = 1 << 2,
	NSSystemDomainMask = 1 << 3,
	NSAllDomainsMask = 0xFFFF	/* every bit, as its name says */
} NSSearchPathDomainMask;

/* WHERE TEMPORARY FILES GO, as a FUNCTION rather than a method because that is how Apple declares it (the
 * ledger files it under NSFileManager's "Accessing user directories"). The trailing separator is Apple's
 * spelling, and the path is the FSH's own: `/System/Temporary Files`, the directory init recreates at mount
 * if a kill-replay left it anything but a directory. A class that wants somewhere to put a file asks HERE
 * rather than naming a policy of its own - which is the whole reason this ships before the download task.
 *
 * NSHomeDirectory() AND NSHomeDirectoryForUser() ARE STILL ABSENT, named: both need the account database,
 * which this tree reads through the passwd domain, and mapping a uid to a home directory is its own row
 * rather than a path constant. Only the temporary directory is answered here. */
NSString *NSTemporaryDirectory(void);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEMANAGER_H */
