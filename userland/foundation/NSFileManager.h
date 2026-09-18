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

#import <foundation/NSObject.h>

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

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEMANAGER_H */
