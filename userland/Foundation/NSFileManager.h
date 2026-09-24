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

/* ... and the values NSFileType takes, which a caller compares against. */
extern NSString *const NSFileTypeRegular;
extern NSString *const NSFileTypeDirectory;
extern NSString *const NSFileTypeSymbolicLink;
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

- (nullable NSDictionary *)attributesOfItemAtPath:(NSString *)path
					    error:(NSError ** _Nullable)error;
- (nullable NSString *)destinationOfSymbolicLinkAtPath:(NSString *)path
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

typedef enum {
	NSVolumeEnumerationSkipHiddenVolumes = 1 << 0,
	NSVolumeEnumerationProduceFileReferenceURLs = 1 << 1
} NSVolumeEnumerationOptions;

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

/* How one item relates to another: is it inside, is it the same, or neither. */
typedef enum {
	NSURLRelationshipContains = 0,
	NSURLRelationshipSame = 1,
	NSURLRelationshipOther = 2
} NSURLRelationship;

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
