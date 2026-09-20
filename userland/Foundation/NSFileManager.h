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
 * WHAT IS NOT HERE, named: NSURL-taking forms, the delegate, `-enumeratorAtPath:` and
 * `-subpathsAtPath:` (the walk is here, the enumerator objects are not), extended attributes, and
 * mounting. Each is a real part of Cocoa's NSFileManager and none of them is half-built here.
 */

#ifndef FOUNDATION_NSFILEMANAGER_H
#define FOUNDATION_NSFILEMANAGER_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSData;
@class NSDate;
@class NSDictionary;
@class NSError;
@class NSString;

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

@interface NSFileManager : NSObject

+ (NSFileManager *)defaultManager;

/* EXISTENCE, and the second door answers the question a caller usually means by it. */
- (BOOL)fileExistsAtPath:(NSString *)path;
- (BOOL)fileExistsAtPath:(NSString *)path isDirectory:(nullable BOOL *)isDirectory;
- (BOOL)isReadableFileAtPath:(NSString *)path;
- (BOOL)isWritableFileAtPath:(NSString *)path;
- (BOOL)isExecutableFileAtPath:(NSString *)path;
- (BOOL)isDeletableFileAtPath:(NSString *)path;

/* THE NAMES IN A DIRECTORY, not the full paths: Cocoa's answer, and the caller joins them. */
- (nullable NSArray *)contentsOfDirectoryAtPath:(NSString *)path error:(NSError ** _Nullable)error;

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

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEMANAGER_H */
