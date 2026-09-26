/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSItemProvider.m — the provider, its ordered registration list, and the coercions the loading doors are
 * documented to do. §62.23 of docs/design/foundation-plan.md. MANUAL OWNERSHIP.
 *
 * ONE SHAPE FOR EVERY REGISTRATION, WHICH IS WHAT MAKES THE DOORS SHORT. A registration is a type identifier, a
 * visibility, its file options, and a way to be PRODUCED — and that way is normalized at registration time into
 * one of three block shapes (an item, data, or a file URL), with a payload kind saying what it natively is when it
 * is not lazy at all. Every loading door then asks for "the native value" and COERCES it, so the coercion lives in
 * exactly one function and `-loadDataRepresentationForTypeIdentifier:` is the only place that has to know that a
 * file becomes its bytes and an item becomes its archive.
 *
 * A BLOCK IS NEVER SENT AN OWNERSHIP MESSAGE HERE (this tree's gate refuses `copy`/`retain`/`release`/`autorelease`
 * to a block-typed name, because `-copy` makes the runtime read the block's ISA — a measured null-page fault in
 * the URL session). Registration blocks are `Block_copy`'d and `Block_release`'d, which are the runtime's own
 * entry points and cannot depend on the isa.
 *
 * AND THE LOADING DOORS ANSWER SYNCHRONOUSLY, as the header states: nothing is deferred, so a door can hand its
 * caller's completion handler straight to the registration and nothing has to survive a queue boundary.
 */

#import <Foundation/NSItemProvider.h>
#import <Foundation/NSData.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSKeyedArchiver.h>
#import <Foundation/NSProgress.h>
#import <Foundation/NSString.h>
#import <Foundation/NSURL.h>

#include <Block.h>

NSString *const NSItemProviderErrorDomain = @"NSItemProviderErrorDomain";
NSString *const NSItemProviderPreferredImageSizeKey = @"NSItemProviderPreferredImageSizeKey";
NSString *const NSExtensionJavaScriptPreprocessingResultsKey = @"NSExtensionJavaScriptPreprocessingResultsKey";
NSString *const NSExtensionJavaScriptFinalizeArgumentKey = @"NSExtensionJavaScriptFinalizeArgumentKey";

/* THE IDENTIFIER `-initWithContentsOfURL:` RECORDS, and the header says why it is this one: a file's CONTENT type
 * comes from the identifier database, which this system does not have. */
static NSString *const FN_FILE_URL_IDENTIFIER = @"public.file-url";

/* WHAT A REGISTRATION NATIVELY PRODUCES WHEN IT IS NOT LAZY AT ALL. */
typedef enum {
	FNPayloadNone = 0,
	FNPayloadItem,
	FNPayloadData,
	FNPayloadFile
} FNPayloadKind;

/* THE SHAPE OF THE BLOCK THAT PRODUCES IT WHEN IT IS. */
typedef enum {
	FNHandlerNone = 0,
	FNHandlerItem,
	FNHandlerData,
	FNHandlerFile
} FNHandlerShape;

@interface FNItemRepresentation : NSObject
{
@public
	NSString *_identifier;
	NSItemProviderRepresentationVisibility _visibility;
	NSItemProviderFileOptions _fileOptions;
	FNPayloadKind _payloadKind;
	FNHandlerShape _handlerShape;
	id _payload;		/* the item, the NSData, or the NSURL */
	id _handler;		/* Block_copy'd; the shape above says how to call it */
}
@end

@implementation FNItemRepresentation

- (void)dealloc
{
	NSString *identifier = _identifier;
	id payload = _payload;
	id handler = _handler;

	_identifier = nil;
	_payload = nil;
	_handler = nil;
	[identifier release];
	[payload release];
	if (handler != nil) {
		Block_release(handler);
	}
	[super dealloc];
}

@end

/* THE ERROR EVERY FAILING DOOR ANSWERS WITH. */
static NSError *fn_item_error(NSItemProviderErrorCode code, NSString *description)
{
	return [NSError errorWithDomain:NSItemProviderErrorDomain
				   code:(NSInteger)code
			       userInfo:[NSDictionary dictionaryWithObject:description
								    forKey:NSLocalizedDescriptionKey]];
}

/* AN ALREADY-FINISHED PROGRESS OBJECT: the work a load describes is done before its answer can be read (the header
 * states the synchrony), so this is what "the progress of that work" looks like here. */
static NSProgress *fn_finished_progress(void)
{
	NSProgress *progress = [NSProgress discreteProgressWithTotalUnitCount:1];

	[progress setCompletedUnitCount:1];
	return progress;
}

@implementation NSItemProvider

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_representations = [[NSMutableArray alloc] init];
	_preferredPresentationSize = NSMakeSize(0, 0);
	return self;
}

- (void)dealloc
{
	NSMutableArray *representations = _representations;
	NSString *name = _suggestedName;
	NSData *team = _teamData;
	id preview = _previewImageHandler;

	_representations = nil;
	_suggestedName = nil;
	_teamData = nil;
	_previewImageHandler = nil;
	[representations release];
	[name release];
	[team release];
	if (preview != nil) {
		Block_release(preview);
	}
	[super dealloc];
}

/* ---- THE CONVENIENCE INITIALIZERS ---------------------------------------------------------------- */

- (instancetype)initWithContentsOfURL:(NSURL *)fileURL
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	if (fileURL != nil) {
		/* THE FILE IS NOT READ HERE — a load reads it — and the OpenInPlace option IS set, because the file the
		 * caller handed over IS the file, so `-loadInPlaceFileRepresentation…` can answer it rather than a copy. */
		FNItemRepresentation *record = [[FNItemRepresentation alloc] init];

		record->_identifier = [FN_FILE_URL_IDENTIFIER copy];
		record->_visibility = NSItemProviderRepresentationVisibilityAll;
		record->_fileOptions = NSItemProviderFileOptionOpenInPlace;
		record->_payloadKind = FNPayloadFile;
		record->_payload = [fileURL retain];
		[_representations addObject:record];
		[record release];
	}
	return self;
}

- (nullable instancetype)initWithItem:(id<NSSecureCoding>)item typeIdentifier:(nullable NSString *)typeIdentifier
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	if (item == nil || typeIdentifier == nil || [typeIdentifier length] == 0) {
		[self release];
		return nil;
	}
	[self fnAddPayload:item kind:FNPayloadItem identifier:typeIdentifier
		visibility:NSItemProviderRepresentationVisibilityAll fileOptions:0];
	return self;
}

- (nullable instancetype)initWithObject:(id<NSItemProviderWriting>)object
{
	self = [self init];
	if (self == nil) {
		return nil;
	}
	if (object == nil) {
		[self release];
		return nil;
	}
	[self registerObject:object visibility:NSItemProviderRepresentationVisibilityAll];
	return self;
}

/* ---- THE STORE ------------------------------------------------------------------------------------
 *
 * ONE PLACE FILES A REGISTRATION, so the replacement rule and the block ownership live in one function rather
 * than in the seven doors. THE RULE: a type identifier already registered is REPLACED IN PLACE — the new record
 * takes the old one's POSITION, because `-registeredTypeIdentifiers` is documented to answer in registration
 * order and answering one identifier twice would make that order meaningless. Apple publishes nothing here. */

- (void)fnStore:(FNItemRepresentation *)record
{
	NSUInteger i;

	for (i = 0; i < [_representations count]; i++) {
		FNItemRepresentation *existing = [_representations objectAtIndex:i];

		if ([existing->_identifier isEqualToString:record->_identifier]) {
			[_representations replaceObjectAtIndex:i withObject:record];
			return;
		}
	}
	[_representations addObject:record];
}

- (void)fnAddPayload:(nullable id)payload
		kind:(FNPayloadKind)kind
	  identifier:(NSString *)identifier
	  visibility:(NSItemProviderRepresentationVisibility)visibility
	 fileOptions:(NSItemProviderFileOptions)fileOptions
{
	FNItemRepresentation *record = [[FNItemRepresentation alloc] init];

	record->_identifier = [identifier copy];
	record->_visibility = visibility;
	record->_fileOptions = fileOptions;
	record->_payloadKind = kind;
	record->_payload = [payload retain];
	record->_handlerShape = FNHandlerNone;
	[self fnStore:record];
	[record release];
}

- (void)fnAddHandler:(id)handler
	       shape:(FNHandlerShape)shape
	  identifier:(NSString *)identifier
	  visibility:(NSItemProviderRepresentationVisibility)visibility
	 fileOptions:(NSItemProviderFileOptions)fileOptions
{
	FNItemRepresentation *record = [[FNItemRepresentation alloc] init];

	record->_identifier = [identifier copy];
	record->_visibility = visibility;
	record->_fileOptions = fileOptions;
	record->_payloadKind = FNPayloadNone;
	record->_handlerShape = shape;
	record->_handler = Block_copy(handler);
	[self fnStore:record];
	[record release];
}

/* ---- WHAT THE PROVIDER SAYS ABOUT ITSELF --------------------------------------------------------- */

- (nullable NSString *)suggestedName
{
	return _suggestedName;
}

- (void)setSuggestedName:(nullable NSString *)name
{
	NSString *old = _suggestedName;

	_suggestedName = [name copy];
	[old release];
}

- (nullable NSData *)teamData
{
	return _teamData;
}

- (void)setTeamData:(nullable NSData *)data
{
	NSData *old = _teamData;

	_teamData = [data copy];
	[old release];
}

- (NSSize)preferredPresentationSize
{
	return _preferredPresentationSize;
}

- (void)setPreferredPresentationSize:(NSSize)size
{
	_preferredPresentationSize = size;
}

- (NSRect)sourceFrame
{
	return NSMakeRect(0, 0, 0, 0);	/* nothing in this system positions a drag: see the header */
}

- (NSRect)containerFrame
{
	return NSMakeRect(0, 0, 0, 0);
}

- (nullable NSItemProviderLoadHandler)previewImageHandler
{
	return _previewImageHandler;
}

- (void)setPreviewImageHandler:(nullable NSItemProviderLoadHandler)handler
{
	id old = _previewImageHandler;

	_previewImageHandler = (handler != nil) ? Block_copy(handler) : nil;
	if (old != nil) {
		Block_release(old);
	}
}

/* ---- REGISTERING --------------------------------------------------------------------------------- */

- (void)registerItemForTypeIdentifier:(NSString *)typeIdentifier
			  loadHandler:(NSItemProviderLoadHandler)loadHandler
{
	[self fnAddHandler:loadHandler shape:FNHandlerItem identifier:typeIdentifier
		visibility:NSItemProviderRepresentationVisibilityAll fileOptions:0];
}

- (void)registerDataRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					 visibility:(NSItemProviderRepresentationVisibility)visibility
					loadHandler:(NSProgress * _Nullable (^)(void (^)(NSData *,
											   NSError *)))loadHandler
{
	[self fnAddHandler:loadHandler shape:FNHandlerData identifier:typeIdentifier
		visibility:visibility fileOptions:0];
}

- (void)registerFileRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					fileOptions:(NSItemProviderFileOptions)fileOptions
					 visibility:(NSItemProviderRepresentationVisibility)visibility
					loadHandler:(NSProgress * _Nullable (^)(void (^)(NSURL *, BOOL,
											   NSError *)))loadHandler
{
	[self fnAddHandler:loadHandler shape:FNHandlerFile identifier:typeIdentifier
		visibility:visibility fileOptions:fileOptions];
}

/* AN OBJECT'S OWN TYPES, EACH ONE LOADING THROUGH THE OBJECT'S OWN DOOR. The visibility is the caller's, unless
 * the object answers the OPTIONAL instance door for a type — which is exactly what that door is for. And the
 * optional INSTANCE list, when the object answers it, narrows the class's list. */
- (void)registerObject:(id<NSItemProviderWriting>)object
	    visibility:(NSItemProviderRepresentationVisibility)visibility
{
	NSArray *identifiers;
	NSUInteger i;

	if (object == nil) {
		return;
	}
	identifiers = [[object class] writableTypeIdentifiersForItemProvider];
	if ([object respondsToSelector:@selector(writableTypeIdentifiersForItemProvider)]) {
		NSArray *narrowed = [object writableTypeIdentifiersForItemProvider];

		if (narrowed != nil) {
			identifiers = narrowed;
		}
	}
	for (i = 0; identifiers != nil && i < [identifiers count]; i++) {
		NSString *identifier = [identifiers objectAtIndex:i];
		NSItemProviderRepresentationVisibility effective = visibility;
		NSItemProviderLoadHandler handler;

		if ([object respondsToSelector:
			@selector(itemProviderVisibilityForRepresentationWithTypeIdentifier:)]) {
			effective = [object itemProviderVisibilityForRepresentationWithTypeIdentifier:identifier];
		}
		/* THE BLOCK CAPTURES THE OBJECT AND ITS IDENTIFIER; Block_copy (in the store) retains both, so an object
		 * that would otherwise be deallocated is held by its own registration. */
		handler = ^(NSItemProviderCompletionHandler completion, Class expected, NSDictionary *options) {
			(void)expected;
			(void)options;
			[object loadDataWithTypeIdentifier:identifier
			      forItemProviderCompletionHandler:^(NSData *data, NSError *error) {
				completion(data, error);
			}];
		};
		[self fnAddHandler:handler shape:FNHandlerItem identifier:identifier
			visibility:effective fileOptions:0];
	}
}

/* A CLASS, PRODUCED LAZILY: the caller's handler makes the object when a load arrives, and THAT object's own door
 * then produces the data for the identifier this registration was made under. */
- (void)registerObjectOfClass:(Class<NSItemProviderWriting>)aClass
		   visibility:(NSItemProviderRepresentationVisibility)visibility
		  loadHandler:(NSProgress * _Nullable (^)(void (^)(id<NSItemProviderWriting> _Nullable,
								   NSError *)))loadHandler
{
	NSArray *identifiers = (aClass != Nil) ? [aClass writableTypeIdentifiersForItemProvider] : nil;
	NSUInteger i;

	for (i = 0; identifiers != nil && i < [identifiers count]; i++) {
		NSString *identifier = [identifiers objectAtIndex:i];
		NSItemProviderLoadHandler handler;

		handler = ^(NSItemProviderCompletionHandler completion, Class expected, NSDictionary *options) {
			(void)expected;
			(void)options;
			loadHandler(^(id<NSItemProviderWriting> object, NSError *error) {
				if (object == nil) {
					completion(nil, error);
					return;
				}
				[object loadDataWithTypeIdentifier:identifier
				      forItemProviderCompletionHandler:^(NSData *data, NSError *inner) {
					completion(data, inner != nil ? inner : error);
				}];
			});
		};
		[self fnAddHandler:handler shape:FNHandlerItem identifier:identifier
			visibility:visibility fileOptions:0];
	}
}

/* ---- ASKING WHAT IT HAS --------------------------------------------------------------------------- */

- (FNItemRepresentation *)fnRepresentationForIdentifier:(nullable NSString *)identifier
{
	NSUInteger i;

	if (identifier == nil) {
		return nil;
	}
	for (i = 0; i < [_representations count]; i++) {
		FNItemRepresentation *record = [_representations objectAtIndex:i];

		if ([record->_identifier isEqualToString:identifier]) {
			return record;
		}
	}
	return nil;
}

/* THE FILE-OPTIONS RULE: no options asks for everything; otherwise a registration must have ALL the requested
 * bits. Apple says "a subset … according to the specified file options" and publishes nothing more precise. */
static BOOL fn_matches_options(FNItemRepresentation *record, NSItemProviderFileOptions fileOptions)
{
	return (record->_fileOptions & fileOptions) == fileOptions;
}

- (NSArray *)registeredTypeIdentifiers
{
	NSMutableArray *identifiers = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [_representations count]; i++) {
		[identifiers addObject:((FNItemRepresentation *)[_representations objectAtIndex:i])->_identifier];
	}
	return identifiers;
}

- (NSArray *)registeredTypeIdentifiersWithFileOptions:(NSItemProviderFileOptions)fileOptions
{
	NSMutableArray *identifiers = [NSMutableArray array];
	NSUInteger i;

	for (i = 0; i < [_representations count]; i++) {
		FNItemRepresentation *record = [_representations objectAtIndex:i];

		if (fn_matches_options(record, fileOptions)) {
			[identifiers addObject:record->_identifier];
		}
	}
	return identifiers;
}

- (BOOL)hasRepresentationConformingToTypeIdentifier:(NSString *)typeIdentifier
					fileOptions:(NSItemProviderFileOptions)fileOptions
{
	FNItemRepresentation *record = [self fnRepresentationForIdentifier:typeIdentifier];

	return record != nil && fn_matches_options(record, fileOptions);
}

- (BOOL)hasItemConformingToTypeIdentifier:(NSString *)typeIdentifier
{
	return [self hasRepresentationConformingToTypeIdentifier:typeIdentifier fileOptions:0];
}

- (BOOL)canLoadObjectOfClass:(Class<NSItemProviderReading>)aClass
{
	NSArray *readable;
	NSUInteger i;

	if (aClass == Nil) {
		return NO;
	}
	readable = [aClass readableTypeIdentifiersForItemProvider];
	for (i = 0; readable != nil && i < [readable count]; i++) {
		if ([self fnRepresentationForIdentifier:[readable objectAtIndex:i]] != nil) {
			return YES;
		}
	}
	return NO;
}

/* ---- THE NATIVE VALUE, WHICH EVERY LOAD STARTS FROM -----------------------------------------------
 *
 * `completion` receives what the registration NATIVELY is — the item, the data, or the file URL — with an error
 * when there is nothing to produce. The three lazy shapes each call their own completion with their own native
 * value, so there is no coercion in this function at all: the coercion lives in the doors. */

- (void)fnNativeValueOf:(FNItemRepresentation *)record
		 options:(nullable NSDictionary *)options
	      completion:(NSItemProviderCompletionHandler)completion
{
	NSDictionary *noOptions = options != nil ? options : [NSDictionary dictionary];

	switch (record->_handlerShape) {
	case FNHandlerItem: {
		NSItemProviderLoadHandler handler = record->_handler;

		handler(completion, Nil, noOptions);
		return;
	}
	case FNHandlerData: {
		NSProgress * _Nullable (^handler)(void (^)(NSData *, NSError *)) = record->_handler;

		handler(^(NSData *data, NSError *error) {
			completion(data, error);
		});
		return;
	}
	case FNHandlerFile: {
		NSProgress * _Nullable (^handler)(void (^)(NSURL *, BOOL, NSError *)) = record->_handler;

		handler(^(NSURL *url, BOOL coordinated, NSError *error) {
			/* AN `id` LOCAL, AND THE GAP IS NAMED WHERE IT SHOWS: Apple's completion handler is typed
			 * `__kindof id<NSSecureCoding>` and APPLE'S NSURL CONFORMS TO NSSecureCoding — OURS CONFORMS TO
			 * `NSCopying` ONLY, so its coding pair is a recorded gap of the URL unit and the type system says so
			 * here. The object that travels is the same one either way. */
			id item = url;

			(void)coordinated;
			completion(item, error);
		});
		return;
	}
	default:
		break;
	}
	completion(record->_payload, nil);
}

/* ANYTHING, AS DATA — AND WHAT A REGISTRATION WAS MADE AS DECIDES, NOT WHAT ITS VALUE HAPPENS TO BE.
 *
 * THAT DISTINCTION WAS MEASURED RATHER THAN REASONED: the first version asked `isKindOfClass:[NSData class]` of the
 * value, so an NSData registered through `-initWithItem:typeIdentifier:` — an ITEM, whose contract is that the data
 * door answers its ARCHIVE — answered the bytes themselves, and the probe's round trip through
 * `NSKeyedUnarchiver` found it (`archiveHead=item`). An item is archived because that is what an item IS, even
 * when the item is already data.
 *
 * A HANDLER'S answer has no kind to consult, so it is still coerced by what it is: data passes, a file's contents
 * are read, an `NSSecureCoding` object is archived, and anything else is refused with the coercion error — which
 * is what that code exists for. */
- (void)fnDataOf:(FNItemRepresentation *)record
	 completion:(void (^)(NSData *data, NSError *error))completion
{
	if (record->_handlerShape == FNHandlerNone) {
		switch (record->_payloadKind) {
		case FNPayloadData:
			completion(record->_payload, nil);
			return;
		case FNPayloadFile: {
			NSData *contents = [NSData dataWithContentsOfFile:[(NSURL *)record->_payload path]];

			if (contents == nil) {
				completion(nil, fn_item_error(NSItemProviderItemUnavailableError,
					@"the registered file could not be read"));
				return;
			}
			completion(contents, nil);
			return;
		}
		case FNPayloadItem: {
			NSData *archive = [NSKeyedArchiver archivedDataWithRootObject:record->_payload];

			if (archive == nil) {
				completion(nil, fn_item_error(NSItemProviderUnavailableCoercionError,
					@"the registered item could not be archived"));
				return;
			}
			completion(archive, nil);
			return;
		}
		default:
			break;
		}
	}
	[self fnNativeValueOf:record options:nil completion:^(id value, NSError *error) {
		if (value == nil) {
			completion(nil, error);
			return;
		}
		if ([value isKindOfClass:[NSData class]]) {
			completion(value, nil);
			return;
		}
		if ([value isKindOfClass:[NSURL class]]) {
			NSData *contents = [NSData dataWithContentsOfFile:[(NSURL *)value path]];

			if (contents == nil) {
				completion(nil, fn_item_error(NSItemProviderItemUnavailableError,
					@"the registered file could not be read"));
				return;
			}
			completion(contents, nil);
			return;
		}
		if ([value conformsToProtocol:@protocol(NSSecureCoding)]) {
			NSData *archive = [NSKeyedArchiver archivedDataWithRootObject:value];

			if (archive != nil) {
				completion(archive, nil);
				return;
			}
		}
		completion(nil, fn_item_error(NSItemProviderUnavailableCoercionError,
			@"the registered item is neither data, a file, nor an archivable object"));
	}];
}

/* WHERE A COPY GOES AND WHAT IT IS CALLED. The two rules are ours and are in the header: the name comes from
 * `suggestedName` (reduced to its last path component) or from the type identifier's last dot-separated component,
 * and a name already taken gets `-1`, `-2` … so two loads cannot overwrite each other. */
static NSURL *fn_write_copy(NSString *suggestedName, NSString *typeIdentifier, NSData *data, NSError **errorOut)
{
	NSString *directory = NSTemporaryDirectory();
	NSString *base = nil;
	NSString *path = nil;
	NSFileManager *manager = [NSFileManager defaultManager];
	NSUInteger attempt;

	if (suggestedName != nil && [suggestedName length] > 0) {
		base = [suggestedName lastPathComponent];
	}
	if (base == nil || [base length] == 0) {
		NSArray *parts = [typeIdentifier componentsSeparatedByString:@"."];

		base = ([parts count] > 0) ? [parts objectAtIndex:[parts count] - 1] : @"item";
	}
	path = [directory stringByAppendingPathComponent:base];
	for (attempt = 1; [manager fileExistsAtPath:path] && attempt < 10000; attempt++) {
		path = [directory stringByAppendingPathComponent:
			[NSString stringWithFormat:@"%@-%lu", base, (unsigned long)attempt]];
	}
	if (![data writeToFile:path atomically:YES]) {
		if (errorOut != NULL) {
			*errorOut = fn_item_error(NSItemProviderItemUnavailableError,
						  @"the item could not be written to the temporary directory");
		}
		return nil;
	}
	return [NSURL fileURLWithPath:path];
}

/* ---- LOADING --------------------------------------------------------------------------------------
 *
 * THE TWO DOORS THAT ONLY WANT "THE THING" (the item door and the in-place file door) ask for the NATIVE value,
 * and the three that want a SHAPE ask for it and coerce. `-loadDataRepresentation…` and its two siblings are the
 * places the coercion shows, which is why they are written out rather than shared: each one coerces DIFFERENTLY. */

- (void)loadItemForTypeIdentifier:(NSString *)typeIdentifier
			  options:(nullable NSDictionary *)options
		completionHandler:(NSItemProviderCompletionHandler)completionHandler
{
	FNItemRepresentation *record;

	if (completionHandler == NULL) {
		return;
	}
	record = [self fnRepresentationForIdentifier:typeIdentifier];
	if (record == nil) {
		completionHandler(nil, fn_item_error(NSItemProviderItemUnavailableError,
			[NSString stringWithFormat:@"no representation is registered for %@", typeIdentifier]));
		return;
	}
	[self fnNativeValueOf:record options:options completion:completionHandler];
}

- (nullable NSProgress *)loadDataRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					       completionHandler:(void (^)(NSData *,
									   NSError *))completionHandler
{
	FNItemRepresentation *record;

	if (completionHandler == NULL) {
		return nil;
	}
	record = [self fnRepresentationForIdentifier:typeIdentifier];
	if (record == nil) {
		completionHandler(nil, fn_item_error(NSItemProviderItemUnavailableError,
			[NSString stringWithFormat:@"no representation is registered for %@", typeIdentifier]));
		return fn_finished_progress();
	}
	[self fnDataOf:record completion:^(NSData *data, NSError *error) {
		completionHandler(data, error);
	}];
	return fn_finished_progress();
}

/* ONE PLACE DECIDES WHAT "A URL" MEANS, because two doors ask for it: the registration's own file when it is
 * file-backed, and a temporary copy of the DATA otherwise — which is Apple's sentence for the doors that are not
 * already about a file ("writes a copy of the provided, typed data to a temporary file"). */
- (void)fnUrlOf:(FNItemRepresentation *)record
 completionHandler:(void (^)(NSURL *url, NSError *error))completionHandler
{
	if (record->_handlerShape == FNHandlerFile) {
		NSProgress * _Nullable (^handler)(void (^)(NSURL *, BOOL, NSError *)) = record->_handler;

		handler(^(NSURL *url, BOOL coordinated, NSError *error) {
			/* AN `id` LOCAL, AND THE GAP IS NAMED WHERE IT SHOWS: Apple's completion handler is typed
			 * `__kindof id<NSSecureCoding>` and APPLE'S NSURL CONFORMS TO NSSecureCoding — OURS CONFORMS TO
			 * `NSCopying` ONLY, so its coding pair is a recorded gap of the URL unit and the type system says so
			 * here. The value that travels is the same object either way. */
			id item = url;

			(void)coordinated;
			completionHandler(item, error);
		});
		return;
	}
	if (record->_payloadKind == FNPayloadFile) {
		completionHandler(record->_payload, nil);
		return;
	}
	[self fnDataOf:record completion:^(NSData *data, NSError *error) {
		NSError *writeError = nil;
		NSURL *url;

		if (data == nil) {
			completionHandler(nil, error);
			return;
		}
		url = fn_write_copy(_suggestedName, record->_identifier, data, &writeError);
		completionHandler(url, url != nil ? nil : writeError);
	}];
}

- (nullable NSProgress *)loadFileRepresentationForTypeIdentifier:(NSString *)typeIdentifier
					       completionHandler:(void (^)(NSURL *,
									   NSError *))completionHandler
{
	FNItemRepresentation *record;

	if (completionHandler == NULL) {
		return nil;
	}
	record = [self fnRepresentationForIdentifier:typeIdentifier];
	if (record == nil) {
		completionHandler(nil, fn_item_error(NSItemProviderItemUnavailableError,
			[NSString stringWithFormat:@"no representation is registered for %@", typeIdentifier]));
		return fn_finished_progress();
	}
	[self fnUrlOf:record completionHandler:completionHandler];
	return fn_finished_progress();
}

- (nullable NSProgress *)loadInPlaceFileRepresentationForTypeIdentifier:(NSString *)typeIdentifier
						      completionHandler:(void (^)(NSURL *, BOOL,
										  NSError *))completionHandler
{
	FNItemRepresentation *record;

	if (completionHandler == NULL) {
		return nil;
	}
	record = [self fnRepresentationForIdentifier:typeIdentifier];
	if (record == nil) {
		completionHandler(nil, NO, fn_item_error(NSItemProviderItemUnavailableError,
			[NSString stringWithFormat:@"no representation is registered for %@", typeIdentifier]));
		return fn_finished_progress();
	}
	/* THE ORIGINAL FILE, WHEN THE REGISTRATION SAID SO: this is the whole content of the OpenInPlace option here,
	 * because there is no sandbox to grant access through (the header says so). Everything else answers a copy. */
	if ((record->_fileOptions & NSItemProviderFileOptionOpenInPlace) != 0) {
		[self fnUrlOf:record completionHandler:^(NSURL *url, NSError *error) {
			completionHandler(url, url != nil, error);
		}];
		return fn_finished_progress();
	}
	[self fnUrlOf:record completionHandler:^(NSURL *url, NSError *error) {
		completionHandler(url, NO, error);
	}];
	return fn_finished_progress();
}

- (nullable NSProgress *)loadObjectOfClass:(Class<NSItemProviderReading>)aClass
			 completionHandler:(void (^)(__kindof id<NSItemProviderReading> _Nullable, NSError * _Nullable))completionHandler
{
	NSArray *readable;
	NSUInteger i;

	if (completionHandler == NULL) {
		return nil;
	}
	if (aClass == Nil) {
		completionHandler(nil, fn_item_error(NSItemProviderUnexpectedValueClassError,
						     @"no class was given"));
		return fn_finished_progress();
	}
	readable = [aClass readableTypeIdentifiersForItemProvider];
	for (i = 0; readable != nil && i < [readable count]; i++) {
		NSString *identifier = [readable objectAtIndex:i];
		FNItemRepresentation *record = [self fnRepresentationForIdentifier:identifier];

		if (record == nil) {
			continue;
		}
		/* THE FIRST REGISTERED TYPE THE CLASS CAN READ, loaded as data and then BUILT BY THE CLASS — which is the
		 * whole shape of the reading protocol. */
		[self fnDataOf:record completion:^(NSData *data, NSError *error) {
			NSError *buildError = nil;
			id object;

			if (data == nil) {
				completionHandler(nil, error);
				return;
			}
			object = [aClass objectWithItemProviderData:data
						     typeIdentifier:identifier
							      error:&buildError];
			completionHandler(object, object != nil ? nil
				: (buildError != nil ? buildError
				   : fn_item_error(NSItemProviderUnavailableCoercionError,
						@"the class could not build an object from this data")));
		}];
		return fn_finished_progress();
	}
	completionHandler(nil, fn_item_error(NSItemProviderItemUnavailableError,
		@"no registered type identifier can be read by that class"));
	return fn_finished_progress();
}

- (void)loadPreviewImageWithOptions:(nullable NSDictionary *)options
		  completionHandler:(NSItemProviderCompletionHandler)completionHandler
{
	if (completionHandler == NULL) {
		return;
	}
	if (_previewImageHandler != nil) {
		NSItemProviderLoadHandler handler = _previewImageHandler;

		handler(completionHandler, Nil, options != nil ? options : [NSDictionary dictionary]);
		return;
	}
	completionHandler(nil, fn_item_error(NSItemProviderItemUnavailableError,
		@"this provider has no preview image and this system has no icon service to ask"));
}

/* ---- COPYING --------------------------------------------------------------------------------------
 *
 * AN INDEPENDENT PROVIDER OVER THE SAME REGISTRATIONS: the records are immutable so they are SHARED, while the
 * ARRAY is this object's own — adding to one provider must not change the other. This is the mutable half of the
 * §62 rule, and the honest side of it: a provider is not a value, so `-copy` cannot answer `[self retain]` the way
 * an immutable result does. */

- (id)copy
{
	NSItemProvider *copy = [[NSItemProvider alloc] init];

	copy->_representations = [_representations mutableCopy];
	copy->_suggestedName = [_suggestedName copy];
	copy->_teamData = [_teamData copy];
	copy->_preferredPresentationSize = _preferredPresentationSize;
	if (_previewImageHandler != nil) {
		copy->_previewImageHandler = Block_copy(_previewImageHandler);
	}
	return copy;
}

@end
