/*
 * NSFileVersion.h — W8 slice 8a: A FILE'S CURRENT VERSION, WHICH IS THE HALF THIS SYSTEM CAN TELL THE
 * TRUTH ABOUT. docs/design/foundation-plan.md §60.
 *
 * APPLE'S ABSTRACT: "A snapshot of a file at a specific point in time", with 25 published members measured
 * from the class's page. The class is written around something THIS SYSTEM DOES NOT HAVE - A VERSION STORE.
 * Apple's own pages name it: versions live in a store the system manages ("the system can save versions of a
 * file in a version store", used by the document model, by iCloud and by file providers). So the split this
 * header makes is between what a store is needed for and what it is not:
 *
 *   NO STORE NEEDED, AND THEREFORE IMPLEMENTED: the CURRENT version of an item - its URL, its name, its
 *   modification date, whether its contents are local - and the two collections that are TRUTHFULLY EMPTY
 *   without a store: +otherVersionsOfItemAtURL: and +unresolvedConflictVersionsOfItemAtURL: answer an
 *   EMPTY ARRAY, which is not a euphemism but the postcondition ("no versions other than the current one"
 *   is exactly true when nothing stores them). +removeOtherVersionsOfItemAtURL:error: likewise SUCCEEDS,
 *   because its postcondition already holds.
 *
 *   A STORE IS NEEDED, AND THEREFORE REGISTERED (§11.6.1 D14 did the same for the coordinator family):
 *   +addVersionOfItemAtURL:withContentsOfURL:options:error: (nothing to add a version TO),
 *   -replaceItemAtURL:options:error: and -removeAndReturnError: (both act on the store; the CURRENT
 *   version cannot be removed and says so with an error rather than by pretending),
 *   +temporaryDirectoryURLForNewVersionOfItemAtURL: (the directory exists to stage a version FOR THE
 *   STORE), and +versionOfItemAtURL:forPersistentIdentifier: beyond the identifier this class itself hands
 *   out. Each of those answers NIL AND AN ERROR where Apple gives the door an error parameter, so the
 *   refusal is NAMED rather than silent.
 *
 *   AND THE CONFLICT MACHINERY IS REGISTERED RATHER THAN DECLARED: -isResolved/-setResolved:,
 *   -localizedNameOfSavingComputer, -isDiscardable and the version browser's business exist only to talk
 *   about CONFLICT versions and about who saved what on which computer - and there are no conflict versions
 *   and no saving computers here. A getter that answered YES or NO to "is this conflict resolved" would be
 *   inventing a fact, which is worse than not having the getter.
 *
 * TWO CHOICES ARE OURS AND ARE WRITTEN AT THE DECLARATIONS: -localizedName is the item's own name (this
 * system has no localisation database, the same rule -displayNameAtPath: and NSURLLocalizedNameKey follow),
 * and -persistentIdentifier is an opaque string this class defines ("device:inode:modification-date") whose
 * one guarantee is the one Apple states - that it "can be used to refer to this version in the future",
 * which the probe checks by ROUND TRIPPING it through +versionOfItemAtURL:forPersistentIdentifier:.
 */

#ifndef FOUNDATION_NSFILEVERSION_H
#define FOUNDATION_NSFILEVERSION_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSDate;
@class NSError;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* Apple's names with our values, §11.6.1 D2 - and they are inert here for the same reason the doors that
 * take them are registered: there is no store to move a version into or out of. */
typedef enum {
	NSFileVersionAddingByMoving = 1 << 0,
} NSFileVersionAddingOptions;

typedef enum {
	NSFileVersionReplacingByMoving = 1 << 0,
} NSFileVersionReplacingOptions;

@interface NSFileVersion : NSObject
{
	NSURL *_url;			/* the item this version describes (retained) */
	NSDate *_modificationDate;	/* when the item was last modified (retained) */
	NSString *_identifier;		/* OURS, opaque: see -persistentIdentifier */
}

/* "Returns the version object representing the current version of the file or directory at the specified
 * URL" - and NIL when there is no such item, which is the shape Apple's nullable return allows and the
 * probe asserts. */
+ (nullable NSFileVersion *)currentVersionOfItemAtURL:(NSURL *)url;

/* "An array of the versions of the item OTHER than the current one" - EMPTY here, and that is the
 * postcondition rather than a degradation: nothing on this system stores older versions. */
+ (nullable NSArray *)otherVersionsOfItemAtURL:(NSURL *)url;

/* "An array of the conflict versions ... that have not been resolved" - EMPTY for the same reason: a
 * conflict version comes from a store too. */
+ (nullable NSArray *)unresolvedConflictVersionsOfItemAtURL:(NSURL *)url;

/* "Removes all versions other than the current one" - SUCCEEDS, because there are none to remove and the
 * postcondition is what the caller asked for. */
+ (BOOL)removeOtherVersionsOfItemAtURL:(NSURL *)url error:(NSError ** _Nullable)error;

/* The identifier a caller can keep and hand back. Only the identifiers THIS CLASS HANDS OUT are known:
 * anything else is refused with an error rather than answered with nil silently. */
+ (nullable NSFileVersion *)versionOfItemAtURL:(NSURL *)url
			 forPersistentIdentifier:(NSString *)persistentIdentifier;

/* THE STORE DOORS, REGISTERED AND REFUSED BY NAME (see the header's note). */
+ (nullable NSFileVersion *)addVersionOfItemAtURL:(NSURL *)url
				   withContentsOfURL:(NSURL *)contentsURL
					   options:(NSFileVersionAddingOptions)options
					     error:(NSError ** _Nullable)error;
+ (nullable NSURL *)temporaryDirectoryURLForNewVersionOfItemAtURL:(NSURL *)url;
+ (void)getNonlocalVersionsOfItemAtURL:(NSURL *)url
		     completionHandler:(void (^)(NSArray * _Nullable nonlocalVersions, NSError * _Nullable error))completionHandler;

/* "The URL of the version of the file or directory" */
- (NSURL *)URL;
/* "The name of the item" - and here it is the item's OWN name: no localisation database. */
- (nullable NSString *)localizedName;
/* "The modification date of the version" */
- (nullable NSDate *)modificationDate;
/* "An identifier that can be used to refer to this version in the future" - OURS and opaque. */
- (nullable NSString *)persistentIdentifier;
/* "Whether the version is a conflict version" - NO here, and not by convention: with no store there is
 * nothing for a conflict to be between. */
- (BOOL)isConflict;
/* "Whether the version's contents are available locally" */
- (BOOL)hasLocalContents;
/* "Whether the version has a thumbnail" - there is no thumbnail machinery here, so never. */
- (BOOL)hasThumbnail;
/* "Removes this version from the store" - REFUSED, and the current version is why: the store cannot lose
 * the version that IS the file. */
- (BOOL)removeAndReturnError:(NSError ** _Nullable)outError;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEVERSION_H */
