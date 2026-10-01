/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSItemProvider.h — a payload, the representations it can be handed over as, and who may see it. §62.23 of
 * docs/design/foundation-plan.md; the App Support / Attachments family's centre.
 *
 * THE CLASS USED TO BE ABSENT ON PURPOSE, AND THE DECISION IS REVERSED HERE RATHER THAN QUIETLY CHANGED. §62.10
 * shipped this header's VOCABULARY with the note that "an item provider exists to hand a payload to another
 * process, and this system's interprocess story is its own... the door is absent rather than stubbed". What the
 * class turned out to be, on measurement, is THREE THINGS WHOSE SUBSTRATE IS ALL IN-PROCESS: a list of
 * type-identifier registrations (an ordered list), the LOADING of one of them (data, a file, an archived object,
 * or a caller's handler), and the OBJECT CONVERSION the two protocols below describe. None of those needs a second
 * process, and the caller that made the case is this system's own desktop: the pasteboard decision records ONE
 * session pasteboard service and a drag-and-drop design that carries data between windows IN ONE SESSION. The
 * process-crossing half of Apple's class is what stays absent, and it is named below rather than implied.
 *
 * TWO THINGS ABOUT THE ASYNCHRONY, STATED FIRST BECAUSE A CALLER HAS TO KNOW BOTH: Apple's loads are ASYNCHRONOUS
 * and this library has no work queue to defer them to, so THE COMPLETION HANDLER IS CALLED BEFORE THE LOAD METHOD
 * RETURNS — the answer is already there when the handler runs, so a caller that expects it later still works and a
 * caller that would have BLOCKED on it does not deadlock. AND THE `NSProgress` OBJECT A LOAD ANSWERS WITH IS
 * ALREADY FINISHED (`totalUnitCount` 1, `completedUnitCount` 1), because there is nothing left to report progress
 * about by the time anybody can read it.
 *
 * WHAT A PROVIDER HOLDS IS AN ORDERED LIST OF REPRESENTATIONS, one per type identifier, in registration order —
 * which is Apple's own sentence for `-registeredTypeIdentifiers` ("in the same order they were registered") and the
 * reason the store is a list rather than a dictionary. Registering the SAME type identifier twice REPLACES the
 * earlier registration and keeps its POSITION (Apple publishes no rule; the alternative would answer one
 * identifier twice and make the order meaningless). A LOAD FOR AN IDENTIFIER NOTHING WAS REGISTERED FOR answers
 * `NSItemProviderItemUnavailableError`.
 *
 * THE COERCIONS THE LOADING DOORS ARE DOCUMENTED TO DO ARE DONE, AND THEY ARE THE INTERESTING PART:
 *
 *   * `-loadDataRepresentationForTypeIdentifier:completionHandler:` answers the BYTES of whatever is registered —
 *     a data-backed registration's data, a FILE-backed one's contents read off the file system, an item-backed
 *     one's `NSKeyedArchiver` encoding, or what the registered handler produced. A handler that answers something
 *     which is not `NSData` is refused with `NSItemProviderUnavailableCoercionError`, which is what that code is
 *     for;
 *   * `-loadFileRepresentationForTypeIdentifier:completionHandler:` answers a URL — the original file when the
 *     registration is file-backed, and otherwise a COPY IN THE TEMPORARY DIRECTORY, which is Apple's sentence
 *     ("writes a copy of the provided, typed data to a temporary file"). THE FILE IS NAMED FROM `suggestedName`
 *     when there is one (that property's documented purpose) and from the LAST DOT-SEPARATED COMPONENT of the type
 *     identifier otherwise, and a name already taken gets `-1`, `-2` … so two loads cannot overwrite each other. A
 *     suggested name is reduced to its LAST PATH COMPONENT first, because a payload does not get to choose a
 *     directory;
 *   * `-loadInPlaceFileRepresentationForTypeIdentifier:completionHandler:` is where the `OpenInPlace` OPTION means
 *     something: a file registration made WITH it answers the ORIGINAL url and `isInPlace` YES, and anything else
 *     answers a temporary copy and NO. That is the whole content of the option here — this system has no sandbox
 *     to grant access through;
 *   * `-loadObjectOfClass:completionHandler:` is the door the two PROTOCOLS exist for: the class's
 *     `+readableTypeIdentifiersForItemProvider` list is matched against the registered identifiers, the data is
 *     loaded, and `+objectWithItemProviderData:typeIdentifier:error:` builds the object. `-canLoadObjectOfClass:`
 *     is the same match without the loading. AND THE FOUR CLASSES APPLE LISTS AS CONFORMING (`NSString`, `NSURL`,
 *     `NSAttributedString`, `NSUserActivity`) DO NOT CONFORM YET — the identifiers and encodings they need are a
 *     TABLE and a format this library would have to match exactly, so they are their own step rather than a silent
 *     half of this one; the probe's own fixture conforms, which is what proves the door;
 *   * `-loadItemForTypeIdentifier:options:completionHandler:` hands the registered ITEM back untouched (the object
 *     for an item-backed registration, the data, or the url), and ONE BOUNDARY IS NAMED AT IT: Apple derives the
 *     class to coerce to from the CALLER'S COMPLETION BLOCK SIGNATURE, which this library cannot read, so the
 *     `expectedValueClass` a load handler is given is always `Nil` and no class-based coercion happens at this
 *     door. A caller that needs an object uses `-loadObjectOfClass:`.
 *
 * THE VISIBILITY LEVELS ARE RECORDED AND ANSWERED AND GATE NOTHING, and that follows from the reversal above:
 * `All`/`Team`/`Group`/`OwnProcess` say WHO MAY SEE AN ITEM WHEN A DRAG CROSSES A PROCESS BOUNDARY, and there is
 * no other process here to hide an item from.
 *
 * WHAT IS ABSENT, NAMED WITH ITS GROUND:
 *
 *   * every door that takes a **`UTType`** — `-initWithContentsOfURL:contentType:openInPlace:coordinated:visibility:`,
 *     the two `…ForContentType:` loads, the two `…ForContentType:` registrations, `registeredContentTypes`,
 *     `registeredContentTypesForOpenInPlace` and `registeredContentTypesConformingToContentType:` — because THIS
 *     SYSTEM HAS NO `UTType` AND NO UNIFORM TYPE IDENTIFIER DATABASE AT ALL (measured: no `UTType` name is in this
 *     library's ledger and no `public.*` identifier table exists anywhere in the tree). The identifier-based doors
 *     are the same API one type-spelling older, and all of them ship;
 *   * the five **CloudKit** share registrations, which need `CKShare`/`CKContainer` — a dependency this system
 *     lacks (§11.6's ground (ii)), and a service besides;
 *   * **`preferredPresentationStyle`**, whose type `UIPreferredPresentationStyle` is UIKit's and has no home in
 *     this library's Foundation;
 *   * AND `-initWithContentsOfURL:`, WHICH DOES SHIP BUT WITH ONE NAMED DEVIATION: the type identifier it records is
 *     **`public.file-url`**, because the CONTENT type a file has comes from the identifier DATABASE (ground (ii))
 *     and deriving one from an extension without it would be inventing the table. So a provider built from
 *     `photo.jpg` answers NO to `-hasItemConformingToTypeIdentifier:@"public.jpeg"` — a caller that knows the
 *     content type passes it, which is what `-registerFileRepresentationForTypeIdentifier:…` is for.
 */

#ifndef FOUNDATION_NSITEMPROVIDER_H
#define FOUNDATION_NSITEMPROVIDER_H

#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>	/* NSSecureCoding lives here */

/* APPLE'S GENERIC ELEMENT TYPE IS `NSArray<NSString *>` AND IT IS `NSArray *` HERE, as everywhere in this tree:
 * our NSArray carries no type parameters (NSListFormatter.h and NSTextCheckingResult.h record the same
 * deviation), so the element type is stated in prose — every array this class answers holds NSString type
 * identifiers. */

@class NSData;
@class NSProgress;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* THE TWO BLOCK SIGNATURES A REGISTRATION IS MADE OF, and both carry NULLABLE results because a load that found
 * nothing answers an error and no item. The first is what a load is ANSWERED with; the second is what a
 * registration IS — it receives the caller's completion handler, the class the caller wants (always `Nil` here,
 * see the file comment) and the caller's options. */
typedef void (^NSItemProviderCompletionHandler)(__kindof id<NSSecureCoding> _Nullable item,
						NSError * _Nullable error);
typedef void (^NSItemProviderLoadHandler)(NSItemProviderCompletionHandler completionHandler,
					  Class _Nullable expectedValueClass, NSDictionary *options);

/* What a provider hands out and who may see it. Bit sets, as their names say. */
typedef enum {
	NSItemProviderFileOptionOpenInPlace = 1 << 0
} NSItemProviderFileOptions;

typedef enum {
	NSItemProviderRepresentationVisibilityAll = 1 << 0,
	NSItemProviderRepresentationVisibilityTeam = 1 << 1,
	NSItemProviderRepresentationVisibilityGroup = 1 << 2,
	NSItemProviderRepresentationVisibilityOwnProcess = 1 << 3
} NSItemProviderRepresentationVisibility;

/* The error codes, sequential because nothing in this system compares them to Apple's numbers (D2). */
typedef enum {
	NSItemProviderUnknownError = 0,
	NSItemProviderItemUnavailableError = 1,
	NSItemProviderUnexpectedValueClassError = 2,
	NSItemProviderUnavailableCoercionError = 3
} NSItemProviderErrorCode;

extern NSString *const NSItemProviderErrorDomain;
extern NSString *const NSItemProviderPreferredImageSizeKey;
extern NSString *const NSExtensionJavaScriptPreprocessingResultsKey;
extern NSString *const NSExtensionJavaScriptFinalizeArgumentKey;

/* ---- THE TWO PROTOCOLS A CLASS ADOPTS TO TRAVEL IN A PROVIDER ------------------------------------
 *
 * THE READING HALF IS A CLASS-SIDE CONVERSION (data in, an instance out) and the WRITING HALF IS AN INSTANCE-SIDE
 * one (an instance in, data out), which is why one protocol's doors are `+` and the other's are `-` where it
 * matters. `NSItemProviderWriting`'s CLASS property is REQUIRED and its INSTANCE property of the same name is
 * OPTIONAL — Apple declares both, the class one being what a provider asks before it has an object and the optional
 * one letting an instance narrow its own list. The visibility doors are optional on both sides, and this library
 * asks the INSTANCE one when it has an object and falls back to the class one, then to `All`. */
@protocol NSItemProviderReading <NSObject>
@required
+ (nullable instancetype)objectWithItemProviderData:(NSData *)data
				     typeIdentifier:(NSString *)typeIdentifier
					      error:(NSError ** _Nullable)outError;
+ (NSArray *)readableTypeIdentifiersForItemProvider;
@end

@protocol NSItemProviderWriting <NSObject>
@required
+ (NSArray *)writableTypeIdentifiersForItemProvider;
- (nullable NSProgress *)loadDataWithTypeIdentifier:(NSString *)typeIdentifier
			 forItemProviderCompletionHandler:(void (^)(NSData * _Nullable data,
								    NSError * _Nullable error))completionHandler;
@optional
@property (readonly, copy, nullable) NSArray *writableTypeIdentifiersForItemProvider;
+ (NSItemProviderRepresentationVisibility)itemProviderVisibilityForRepresentationWithTypeIdentifier:
	(NSString *)typeIdentifier;
- (NSItemProviderRepresentationVisibility)itemProviderVisibilityForRepresentationWithTypeIdentifier:
	(NSString *)typeIdentifier;
@end

/* ---- THE PROVIDER ---------------------------------------------------------------------------------
 *
 * AN EMPTY ONE IS MADE FIRST AND REGISTRATIONS ARE ADDED — Apple's `-init` is an "empty item provider to which you
 * can later register a data or file representation" — and the four other initializers are conveniences that
 * register one thing apiece. COPYING one gives an independent provider over the same registrations: the records
 * are immutable, so the copy shares them and adding to one provider cannot change the other. */
@interface NSItemProvider : NSObject <NSCopying>
{
	NSMutableArray *_representations;	/* ordered: registration order IS the answer's order */
	NSItemProviderLoadHandler _previewImageHandler;
	NSString *_suggestedName;
	NSData *_teamData;
	/* §63.51: `_preferredPresentationSize` is gone with its door — UIKit's property, not Foundation's. */
}

- (instancetype)init;

/* THE FILE ITSELF, recorded under `public.file-url` (the file comment says why that identifier and not a content
 * type). The file is NOT read here; a load reads it. */
- (instancetype)initWithContentsOfURL:(NSURL *)fileURL;

/* AN OBJECT, ARCHIVED. The type identifier is the caller's, and a load answers the OBJECT itself at
 * `-loadItemForTypeIdentifier:…` while the data doors answer its `NSKeyedArchiver` encoding. */
- (nullable instancetype)initWithItem:(id<NSSecureCoding>)item typeIdentifier:(nullable NSString *)typeIdentifier;

/* AN OBJECT THAT KNOWS ITS OWN TYPES: its `+writableTypeIdentifiersForItemProvider` are all registered, each one
 * loading through the object's own `-loadDataWithTypeIdentifier:forItemProviderCompletionHandler:`. */
- (nullable instancetype)initWithObject:(id<NSItemProviderWriting>)object;

/* ---- WHAT THE PROVIDER SAYS ABOUT ITSELF ---------------------------------------------------------- */

@property (atomic, copy, nullable) NSString *suggestedName;
@property (atomic, copy, nullable) NSData *teamData;
/* ⚠ THE DRAG-GEOMETRY TRIO CAME OUT (the user's decision, 2026-10-01, §63.51): `preferredPresentationSize`,
 * `sourceFrame` and `containerFrame` are **UIKit's** — `NSItemProvider`'s UIKit category, where a DRAG has a
 * source window and a container — and this tree has no UIKit at all. They were declared here and answered
 * their ZERO values, with the boundary stated rather than implied: "nothing in this system positions a drag,
 * so nothing ever sets them". **A door that can only answer zero is the shape §62.57 calls a stub, and the
 * ledger asked for these three because it attributes a FOREIGN FRAMEWORK'S CATEGORY to the class it extends —
 * the same attribution that put the OBEX family under NSMutableDictionary (§63.42).** They are declined by
 * name in `tools/foundation-sweep.py` rather than left as work, which is the mechanism that ruling created. */

@property (atomic, copy, nullable) NSItemProviderLoadHandler previewImageHandler;

/* ---- REGISTERING ---------------------------------------------------------------------------------- */

- (void)registerItemForTypeIdentifier:(NSString *)typeIdentifier
			  loadHandler:(NSItemProviderLoadHandler)loadHandler;

- (void)registerDataRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					 visibility:(NSItemProviderRepresentationVisibility)visibility
					loadHandler:(NSProgress * _Nullable (^)(void (^completionHandler)(
							     NSData * _Nullable data,
							     NSError * _Nullable error)))loadHandler;

- (void)registerFileRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					fileOptions:(NSItemProviderFileOptions)fileOptions
					 visibility:(NSItemProviderRepresentationVisibility)visibility
					loadHandler:(NSProgress * _Nullable (^)(void (^completionHandler)(
							     NSURL * _Nullable url, BOOL coordinated,
							     NSError * _Nullable error)))loadHandler;

- (void)registerObject:(id<NSItemProviderWriting>)object
	    visibility:(NSItemProviderRepresentationVisibility)visibility;

- (void)registerObjectOfClass:(Class<NSItemProviderWriting>)aClass
		   visibility:(NSItemProviderRepresentationVisibility)visibility
		  loadHandler:(NSProgress * _Nullable (^)(void (^completionHandler)(
				      id<NSItemProviderWriting> _Nullable object,
				      NSError * _Nullable error)))loadHandler;

/* ---- ASKING WHAT IT HAS --------------------------------------------------------------------------- */

@property (atomic, copy, readonly) NSArray *registeredTypeIdentifiers;
- (NSArray *)registeredTypeIdentifiersWithFileOptions:(NSItemProviderFileOptions)fileOptions;
- (BOOL)hasItemConformingToTypeIdentifier:(NSString *)typeIdentifier;
- (BOOL)hasRepresentationConformingToTypeIdentifier:(NSString *)typeIdentifier
					fileOptions:(NSItemProviderFileOptions)fileOptions;
- (BOOL)canLoadObjectOfClass:(Class<NSItemProviderReading>)aClass;

/* ---- LOADING --------------------------------------------------------------------------------------
 *
 * EVERY ONE OF THESE ANSWERS BY CALLING ITS COMPLETION HANDLER BEFORE IT RETURNS (the file comment says why), and
 * every one answers an `NSItemProviderErrorDomain` error rather than nothing when there is no representation to
 * load. The `NSProgress` a representation load answers with is already finished. */
- (void)loadItemForTypeIdentifier:(NSString *)typeIdentifier
			  options:(nullable NSDictionary *)options
		completionHandler:(NSItemProviderCompletionHandler)completionHandler;

- (nullable NSProgress *)loadDataRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					       completionHandler:(void (^)(NSData * _Nullable data,
									   NSError * _Nullable error))completionHandler;

- (nullable NSProgress *)loadFileRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					       completionHandler:(void (^)(NSURL * _Nullable url,
									   NSError * _Nullable error))completionHandler;

- (nullable NSProgress *)loadInPlaceFileRepresentationForTypeIdentifier:(NSString *)typeIdentifier
						      completionHandler:(void (^)(NSURL * _Nullable url,
										  BOOL isInPlace,
										  NSError * _Nullable error))completionHandler;

- (nullable NSProgress *)loadObjectOfClass:(Class<NSItemProviderReading>)aClass
			 completionHandler:(void (^)(__kindof id<NSItemProviderReading> _Nullable object,
						     NSError * _Nullable error))completionHandler;

/* THE PREVIEW DOOR USES `previewImageHandler` WHEN THERE IS ONE and answers the item-unavailable error when there
 * is not: a preview is an ICON or a thumbnail in Apple's implementation, this system has no icon service to ask,
 * and inventing one here would be inventing a picture rather than a rule. */
- (void)loadPreviewImageWithOptions:(nullable NSDictionary *)options
		  completionHandler:(NSItemProviderCompletionHandler)completionHandler;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSITEMPROVIDER_H */
