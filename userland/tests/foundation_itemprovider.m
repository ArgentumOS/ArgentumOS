/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_itemprovider — `NSItemProvider`: an ordered list of type-identifier registrations, the loads that
 * coerce them, and the two protocols an object travels by (docs/design/foundation-plan.md §62.23). ONE unit,
 * importing only <Foundation/Foundation.h>.
 *
 * THE FIXTURE CLASS CONFORMS TO BOTH PROTOCOLS, which is what makes the object doors real: the provider asks its
 * class which identifiers it can read, loads one as DATA, and hands the bytes to
 * `+objectWithItemProviderData:typeIdentifier:error:` — so a round trip through `-registerObject:` and
 * `-loadObjectOfClass:` exercises the reading protocol, the writing protocol and the provider at once.
 *
 * EVERY CHECK IS ABOUT WHAT A CALLER CAN SEE: the ORDER the identifiers come back in, which door answers which
 * SHAPE, where a temporary copy lands and what it is called, which identifier a refusal names, and the fact that
 * the completion handler has already run by the time a load returns (the synchrony the header states).
 */

#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "FOUNDATION-ITEMPROVIDER"

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf(PREFIX " %s ok\n", name);
	} else {
		failc++;
		printf(PREFIX " %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* A VALUE HANDED TO AN ITEM-SHAPED COMPLETION GOES THROUGH THIS, AND THE GAP IS NAMED RATHER THAN HIDDEN: Apple's
 * `NSItemProviderCompletionHandler` is typed `__kindof id<NSSecureCoding>`, and APPLE'S NSString CONFORMS TO
 * NSSecureCoding WHILE OURS CONFORMS TO `NSCopying` ONLY — its coding pair is that class's owed work, and the
 * same is true of NSURL. (NSData's conformance existed to be declared, so it WAS declared with this unit.) The
 * object that travels is the same one either way; this function is a place to say so once. */
static id fn_item_value(id value)
{
	return value;
}

/* THE FILE FIXTURE IS WRITTEN BY THIS PROBE, and that is not tidiness: the guest's temporary directory is an FSH
 * path ("/System/Temporary Files/") that does not exist on the host, so a probe that READ a file from the image
 * would be untestable outside the guest — and a probe whose fixture cannot be written fails for a reason that is
 * not the library's. Writing its own makes the file-backed checks depend on the same directory the LIBRARY writes
 * its temporary copies into, which is the fact they are about. */
static NSString *fn_write_fixture(NSString *contents)
{
	NSString *path = [NSTemporaryDirectory() stringByAppendingPathComponent:@"fn_itemprovider_fixture.txt"];
	NSData *bytes = [contents dataUsingEncoding:NSUTF8StringEncoding];

	if (![bytes writeToFile:path atomically:YES]) {
		return nil;
	}
	return path;
}
static NSString *const FN_TEXT_TYPE = @"org.argentum.probe.text";
static NSString *const FN_OTHER_TYPE = @"org.argentum.probe.binary";

/* WHAT A URL IS CALLED, ASKED OF ITS PATH STRING: `-[NSURL lastPathComponent]` IS NOT IMPLEMENTED IN THIS TREE
 * (nor are its `-pathExtension`/`-URLByDeleting…` siblings), which is a measured gap of the URL unit rather than
 * of this one — the string family's own `-lastPathComponent` is here, so the check asks through it. */
static NSString *fn_url_name(NSURL *url)
{
	return [[url path] lastPathComponent];
}

/* ---- THE FIXTURE: A CLASS THAT TRAVELS BOTH WAYS -------------------------------------------------- */

@interface FNPayloadObject : NSObject <NSItemProviderReading, NSItemProviderWriting>
{
	NSString *_text;
}
- (instancetype)initWithText:(NSString *)text;
- (NSString *)text;
@end

@implementation FNPayloadObject

+ (NSArray *)readableTypeIdentifiersForItemProvider
{
	return [NSArray arrayWithObject:FN_TEXT_TYPE];
}

+ (nullable instancetype)objectWithItemProviderData:(NSData *)data
				     typeIdentifier:(NSString *)typeIdentifier
					      error:(NSError **)outError
{
	NSString *text = [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding];

	if (text == nil) {
		if (outError != NULL) {
			*outError = [NSError errorWithDomain:@"FNProbe" code:1
						    userInfo:@{
				NSLocalizedDescriptionKey : @"not UTF-8" }];
		}
		return nil;
	}
	(void)typeIdentifier;
	return [[self alloc] initWithText:text];	/* ARC: the probe is compiled with -fobjc-arc */
}

+ (NSArray *)writableTypeIdentifiersForItemProvider
{
	return [NSArray arrayWithObject:FN_TEXT_TYPE];
}

- (nullable NSProgress *)loadDataWithTypeIdentifier:(NSString *)typeIdentifier
			 forItemProviderCompletionHandler:(void (^)(NSData *, NSError *))completionHandler
{
	if (![typeIdentifier isEqualToString:FN_TEXT_TYPE]) {
		completionHandler(nil, [NSError errorWithDomain:NSItemProviderErrorDomain
							   code:NSItemProviderItemUnavailableError
						       userInfo:@{
			NSLocalizedDescriptionKey : @"this object writes text only" }]);
		return nil;
	}
	completionHandler([_text dataUsingEncoding:NSUTF8StringEncoding], nil);
	return nil;
}

- (instancetype)initWithText:(NSString *)text
{
	self = [super init];
	if (self != nil) {
		_text = [text copy];
	}
	return self;
}

- (NSString *)text
{
	return _text;
}

@end

/* A CLASS THAT CAN READ NOTHING THIS PROVIDER HOLDS, for the negative half of `-canLoadObjectOfClass:`. */
@interface FNUnrelatedObject : NSObject <NSItemProviderReading>
@end

@implementation FNUnrelatedObject
+ (NSArray *)readableTypeIdentifiersForItemProvider
{
	return [NSArray arrayWithObject:@"org.argentum.probe.nothing"];
}
+ (nullable instancetype)objectWithItemProviderData:(NSData *)data
				     typeIdentifier:(NSString *)typeIdentifier
					      error:(NSError **)outError
{
	(void)data; (void)typeIdentifier; (void)outError;
	return nil;
}
@end

int main(void)
{
	NSString *fixturePath = fn_write_fixture(@"a file fixture, written by the probe\n");
	NSData *fixtureBytes = fixturePath != nil ? [NSData dataWithContentsOfFile:fixturePath] : nil;
	NSData *smallItem = [NSData dataWithBytes:"item" length:4];
	FNPayloadObject *fixture = [[FNPayloadObject alloc] initWithText:@"the payload"];

	/* ---- AN EMPTY PROVIDER, AND THE ERROR EVERY DOOR ANSWERS --------------------------------------- */
	{
		NSItemProvider *empty = [[NSItemProvider alloc] init];
		__block NSError *dataError = nil;
		__block NSError *objectError = nil;
		__block NSError *itemError = nil;

		[empty loadDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
				     completionHandler:^(NSData *data, NSError *error) {
			(void)data;
			dataError = error;
		}];
		[empty loadObjectOfClass:[FNPayloadObject class]
	       completionHandler:^(id<NSItemProviderReading> object, NSError *error) {
			(void)object;
			objectError = error;
		}];
		[empty loadItemForTypeIdentifier:FN_TEXT_TYPE options:nil
		       completionHandler:^(id item, NSError *error) {
			(void)item;
			itemError = error;
		}];
		check("an-empty-provider-refuses-every-door-by-domain-and-code",
		      [[empty registeredTypeIdentifiers] count] == 0 &&
		      ![empty hasItemConformingToTypeIdentifier:FN_TEXT_TYPE] &&
		      ![empty canLoadObjectOfClass:[FNPayloadObject class]] &&
		      [dataError domain] != nil &&
		      [[dataError domain] isEqualToString:NSItemProviderErrorDomain] &&
		      [dataError code] == NSItemProviderItemUnavailableError &&
		      [objectError code] == NSItemProviderItemUnavailableError &&
		      [itemError code] == NSItemProviderItemUnavailableError,
		      [[NSString stringWithFormat:@"types=%lu data=%ld object=%ld item=%ld domain=%@",
			(unsigned long)[[empty registeredTypeIdentifiers] count],
			(long)[dataError code], (long)[objectError code], (long)[itemError code],
			[dataError domain]] UTF8String]);
	}

	/* ---- THE ORDER, AND THE REPLACEMENT RULE ------------------------------------------------------- */
	{
		NSItemProvider *provider = [[NSItemProvider alloc] init];

		[provider registerItemForTypeIdentifier:@"org.argentum.probe.first"
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"one"), nil);
		}];
		[provider registerItemForTypeIdentifier:@"org.argentum.probe.second"
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"two"), nil);
		}];
		[provider registerItemForTypeIdentifier:@"org.argentum.probe.first"
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"one-again"), nil);
		}];
		{
			NSArray *identifiers = [provider registeredTypeIdentifiers];

			check("the-identifiers-come-back-in-registration-order-and-a-repeat-replaces-in-place",
			      [identifiers count] == 2 &&
			      [[identifiers objectAtIndex:0] isEqualToString:@"org.argentum.probe.first"] &&
			      [[identifiers objectAtIndex:1] isEqualToString:@"org.argentum.probe.second"],
			      [[NSString stringWithFormat:@"identifiers=%@", identifiers] UTF8String]);
		}
	}
	{
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block id answer = nil;

		[provider registerItemForTypeIdentifier:FN_TEXT_TYPE
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"first-handler"), nil);
		}];
		[provider registerItemForTypeIdentifier:FN_TEXT_TYPE
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"second-handler"), nil);
		}];
		[provider loadItemForTypeIdentifier:FN_TEXT_TYPE options:nil
		       completionHandler:^(id item, NSError *error) {
			(void)error;
			answer = item;
		}];
		check("the-replacement-is-the-handler-that-answers",
		      [answer isEqualToString:@"second-handler"] &&
		      [[provider registeredTypeIdentifiers] count] == 1,
		      [[NSString stringWithFormat:@"answer=%@ types=%lu", answer,
			(unsigned long)[[provider registeredTypeIdentifiers] count]] UTF8String]);
	}

	/* ---- THE SHAPES: DATA, FILE, ITEM, OBJECT ------------------------------------------------------- */
	{
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block NSData *loaded = nil;
		__block NSError *error = nil;
		__block int ranDuringCall = 0;
		NSProgress *progress;

		[provider registerDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
			      visibility:NSItemProviderRepresentationVisibilityAll
			     loadHandler:^NSProgress *(void (^completionHandler)(NSData *, NSError *)) {
			completionHandler([@"from-the-handler" dataUsingEncoding:NSUTF8StringEncoding], nil);
			return nil;
		}];
		progress = [provider loadDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
						  completionHandler:^(NSData *data, NSError *failure) {
			loaded = data;
			error = failure;
			ranDuringCall = 1;
		}];
		check("a-data-registration-answers-its-bytes-before-the-load-returns",
		      loaded != nil && error == nil && ranDuringCall == 1 &&
		      [[[NSString alloc] initWithData:loaded encoding:NSUTF8StringEncoding]
			isEqualToString:@"from-the-handler"] &&
		      progress != nil && [progress isFinished] &&
		      [progress completedUnitCount] == [progress totalUnitCount],
		      [[NSString stringWithFormat:@"ran=%d finished=%d progress=%lld/%lld",
			ranDuringCall, (int)[progress isFinished],
			(long long)[progress completedUnitCount], (long long)[progress totalUnitCount]]
			UTF8String]);
	}
	{
		/* A REGISTRATION WHOSE VALUE IS NOT DATA, NOT A FILE AND NOT ARCHIVABLE IS REFUSED WITH THE COERCION
		 * ERROR — which is what that code exists for. */
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block NSError *error = nil;

		[provider registerItemForTypeIdentifier:FN_OTHER_TYPE
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value([[NSObject alloc] init]), nil);
		}];
		[provider loadDataRepresentationForTypeIdentifier:FN_OTHER_TYPE
				     completionHandler:^(NSData *data, NSError *failure) {
			(void)data;
			error = failure;
		}];
		check("a-value-that-cannot-be-coerced-to-data-is-refused-by-that-name",
		      error != nil && [error code] == NSItemProviderUnavailableCoercionError &&
		      [[error domain] isEqualToString:NSItemProviderErrorDomain],
		      [[NSString stringWithFormat:@"code=%ld domain=%@", (long)[error code],
			[error domain]] UTF8String]);
	}
	{
		/* A FILE: the provider answers the file itself at both URL doors, and IN PLACE, because
		 * `-initWithContentsOfURL:` records the open-in-place option. */
		/* AN `id` LOCAL, because `+fileURLWithPath:` is annotated nullable while this door is not (the idiom the
		 * guest build's -Werror=nullable-to-nonnull-conversion asks for). */
		id fileURL = (fixturePath != nil) ? [NSURL fileURLWithPath:fixturePath] : nil;
		NSItemProvider *provider = [[NSItemProvider alloc] initWithContentsOfURL:fileURL];
		__block NSData *data = nil;
		__block id url = nil;
		__block BOOL inPlace = NO;
		__block id copyUrl = nil;

		[provider loadDataRepresentationForTypeIdentifier:@"public.file-url"
				     completionHandler:^(NSData *bytes, NSError *error) {
			(void)error;
			data = bytes;
		}];
		[provider loadInPlaceFileRepresentationForTypeIdentifier:@"public.file-url"
			      completionHandler:^(NSURL *loaded, BOOL isInPlace, NSError *error) {
			(void)error;
			url = loaded;
			inPlace = isInPlace;
		}];
		[provider loadFileRepresentationForTypeIdentifier:@"public.file-url"
					  completionHandler:^(NSURL *loaded, NSError *error) {
			(void)error;
			copyUrl = loaded;
		}];
		check("a-file-registration-answers-the-file-its-bytes-and-in-place-when-asked",
		      [provider hasItemConformingToTypeIdentifier:@"public.file-url"] &&
		      data != nil && fixtureBytes != nil && [data isEqualToData:fixtureBytes] &&
		      [[url path] isEqualToString:fixturePath] && inPlace &&
		      [[copyUrl path] isEqualToString:fixturePath],
		      [[NSString stringWithFormat:@"bytes=%lu match=%d url=%@ inPlace=%d fixture=%@ temp=%@",
			(unsigned long)[data length],
			(int)(data != nil && [data isEqualToData:fixtureBytes]), [url path],
			(int)inPlace, fixturePath != nil ? fixturePath : (NSString *)@"(unavailable)",
			NSTemporaryDirectory()] UTF8String]);
	}
	{
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block id url = nil;
		__block NSData *copyBytes = nil;

		[provider setSuggestedName:@"payload.txt"];
		[provider registerDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
			      visibility:NSItemProviderRepresentationVisibilityAll
			     loadHandler:^NSProgress *(void (^completionHandler)(NSData *, NSError *)) {
			completionHandler([@"copy me" dataUsingEncoding:NSUTF8StringEncoding], nil);
			return nil;
		}];
		[provider loadFileRepresentationForTypeIdentifier:FN_TEXT_TYPE
					  completionHandler:^(NSURL *loaded, NSError *error) {
			(void)error;
			url = loaded;
			copyBytes = [NSData dataWithContentsOfFile:[loaded path]];
		}];
		{
			__block id second = nil;

			[provider loadFileRepresentationForTypeIdentifier:FN_TEXT_TYPE
						  completionHandler:^(NSURL *loaded, NSError *error) {
				(void)error;
				second = loaded;
			}];
			check("a-data-registration-is-copied-to-a-file-named-by-suggested-name",
			      [fn_url_name(url) isEqualToString:@"payload.txt"] &&
			      [[url path] hasPrefix:NSTemporaryDirectory()] &&
			      copyBytes != nil &&
			      [[[NSString alloc] initWithData:copyBytes encoding:NSUTF8StringEncoding]
				isEqualToString:@"copy me"] &&
			      [fn_url_name(second) isEqualToString:@"payload.txt-1"],
			      [[NSString stringWithFormat:@"first=%@ second=%@ bytes=%@",
				[url path], [second path], copyBytes] UTF8String]);
		}
	}
	{
		/* A SUGGESTED NAME IS A NAME, NOT A PATH: it is reduced to its last component, and the copy still lands
		 * in the temporary directory (the rule the header states). */
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block id url = nil;

		[provider setSuggestedName:@"../../etc/passwd"];
		[provider registerDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
			      visibility:NSItemProviderRepresentationVisibilityAll
			     loadHandler:^NSProgress *(void (^completionHandler)(NSData *, NSError *)) {
			completionHandler([@"x" dataUsingEncoding:NSUTF8StringEncoding], nil);
			return nil;
		}];
		[provider loadFileRepresentationForTypeIdentifier:FN_TEXT_TYPE
					  completionHandler:^(NSURL *loaded, NSError *error) {
			(void)error;
			url = loaded;
		}];
		check("a-suggested-name-cannot-choose-a-directory",
		      [fn_url_name(url) isEqualToString:@"passwd"] &&
		      [[url path] hasPrefix:NSTemporaryDirectory()] &&
		      [[url path] rangeOfString:@".."].location == NSNotFound,
		      [[NSString stringWithFormat:@"path=%@ temp=%@", [url path],
			NSTemporaryDirectory()] UTF8String]);
	}
	{
		/* WITH NO NAME AT ALL THE FILE IS NAMED AFTER THE LAST COMPONENT OF THE TYPE IDENTIFIER, so a caller can
		 * still find what it asked for. */
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block id url = nil;

		[provider registerDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
			      visibility:NSItemProviderRepresentationVisibilityAll
			     loadHandler:^NSProgress *(void (^completionHandler)(NSData *, NSError *)) {
			completionHandler([@"y" dataUsingEncoding:NSUTF8StringEncoding], nil);
			return nil;
		}];
		[provider loadFileRepresentationForTypeIdentifier:FN_TEXT_TYPE
					  completionHandler:^(NSURL *loaded, NSError *error) {
			(void)error;
			url = loaded;
		}];
		check("with-no-suggested-name-the-type-identifier-names-the-copy",
		      [fn_url_name(url) hasPrefix:@"text"],
		      [[NSString stringWithFormat:@"name=%@", fn_url_name(url)] UTF8String]);
	}
	{
		/* AN ITEM IS ARCHIVED FOR THE DATA DOOR AND HANDED BACK UNTOUCHED AT THE ITEM DOOR. */
		NSItemProvider *provider = [[NSItemProvider alloc] initWithItem:smallItem
								 typeIdentifier:FN_OTHER_TYPE];
		__block id item = nil;
		__block NSData *archive = nil;

		[provider loadItemForTypeIdentifier:FN_OTHER_TYPE options:nil
		       completionHandler:^(id loaded, NSError *error) {
			(void)error;
			item = loaded;
		}];
		[provider loadDataRepresentationForTypeIdentifier:FN_OTHER_TYPE
				     completionHandler:^(NSData *data, NSError *error) {
			(void)error;
			archive = data;
		}];
		/* THE UNARCHIVE IS GUARDED, AND BOTH THE DIRECT ROUND TRIP AND THE PROVIDER'S ARE TRIED: an exception
		 * raised inside a DIAGNOSTIC turns a failed check into an aborted probe that prints nothing — which is
		 * how this probe lost its whole output on its first host run. */
		{
			id directBack = nil;
			id roundTrip = nil;
			NSString *raised = nil;
			NSData *direct = [NSKeyedArchiver archivedDataWithRootObject:smallItem];

			@try {
				if (direct != nil) {
					directBack = [NSKeyedUnarchiver unarchiveObjectWithData:direct];
				}
				if (archive != nil) {
					roundTrip = [NSKeyedUnarchiver unarchiveObjectWithData:archive];
				}
			} @catch (NSException *e) {
				raised = [e name];
			}
			check("an-item-is-handed-back-at-the-item-door-and-archived-at-the-data-door",
			      item != nil && [item isEqualToData:smallItem] &&
			      archive != nil && ![archive isEqualToData:smallItem] &&
			      roundTrip != nil && [roundTrip isEqualToData:smallItem] &&
			      directBack != nil && [directBack isEqualToData:smallItem] &&
			      raised == nil,
			      [[NSString stringWithFormat:@"item=%lu archive=%lu roundTrip=%d direct=%d raised=%@ "
				   @"archiveHead=%.16s",
				(unsigned long)[(NSData *)item length], (unsigned long)[archive length],
				(int)(roundTrip != nil && [roundTrip isEqualToData:smallItem]),
				(int)(directBack != nil && [directBack isEqualToData:smallItem]),
				raised != nil ? raised : (NSString *)@"(none)",
				archive != nil ? (const char *)[archive bytes] : "(no archive)"] UTF8String]);
		}
	}

	/* ---- THE OBJECT DOORS, WHICH IS WHAT THE TWO PROTOCOLS ARE FOR ---------------------------------- */
	{
		NSItemProvider *provider = [[NSItemProvider alloc] initWithObject:fixture];
		__block id<NSItemProviderReading> object = nil;
		__block NSData *data = nil;

		[provider loadObjectOfClass:[FNPayloadObject class]
	       completionHandler:^(id<NSItemProviderReading> loaded, NSError *error) {
			(void)error;
			object = loaded;
		}];
		[provider loadDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
				     completionHandler:^(NSData *bytes, NSError *error) {
			(void)error;
			data = bytes;
		}];
		check("an-objects-own-types-are-registered-and-an-object-comes-back-through-them",
		      [[provider registeredTypeIdentifiers] count] == 1 &&
		      [[[provider registeredTypeIdentifiers] objectAtIndex:0] isEqualToString:FN_TEXT_TYPE] &&
		      data != nil &&
		      [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]
			isEqualToString:@"the payload"] &&
		      [object isKindOfClass:[FNPayloadObject class]] &&
		      [[(FNPayloadObject *)object text] isEqualToString:@"the payload"] &&
		      [provider canLoadObjectOfClass:[FNPayloadObject class]] &&
		      ![provider canLoadObjectOfClass:[FNUnrelatedObject class]],
		      [[NSString stringWithFormat:@"object=%@ text=%@ canLoad=%d other=%d",
			[object class], [(FNPayloadObject *)object text],
			(int)[provider canLoadObjectOfClass:[FNPayloadObject class]],
			(int)[provider canLoadObjectOfClass:[FNUnrelatedObject class]]] UTF8String]);
	}
	{
		/* THE CLASS DOOR: the caller's handler makes the object WHEN A LOAD ARRIVES, and that object's own door
		 * then produces the data for the identifier the registration was made under. */
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block int made = 0;
		__block NSData *data = nil;

		[provider registerObjectOfClass:[FNPayloadObject class]
			      visibility:NSItemProviderRepresentationVisibilityOwnProcess
			     loadHandler:^NSProgress *(void (^completionHandler)(id<NSItemProviderWriting>,
										 NSError *)) {
			made++;
			completionHandler([[FNPayloadObject alloc] initWithText:@"made-lazily"], nil);
			return nil;
		}];
		[provider loadDataRepresentationForTypeIdentifier:FN_TEXT_TYPE
				     completionHandler:^(NSData *bytes, NSError *error) {
			(void)error;
			data = bytes;
		}];
		check("a-class-registration-makes-its-object-on-demand-and-loads-through-it",
		      made == 1 && data != nil &&
		      [[[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]
			isEqualToString:@"made-lazily"] &&
		      [[provider registeredTypeIdentifiersWithFileOptions:0] count] == 1,
		      [[NSString stringWithFormat:@"made=%d data=%@", made,
			data != nil ? [[NSString alloc] initWithData:data encoding:NSUTF8StringEncoding]
				    : (NSString *)@"(nil)"] UTF8String]);
	}

	/* ---- THE OPTIONS AND THE VISIBILITY ARE RECORDED AND ANSWERED ----------------------------------- */
	{
		NSItemProvider *provider = [[NSItemProvider alloc] init];

		[provider registerFileRepresentationForTypeIdentifier:@"org.argentum.probe.openable"
						   fileOptions:NSItemProviderFileOptionOpenInPlace
						    visibility:NSItemProviderRepresentationVisibilityOwnProcess
						   loadHandler:^NSProgress *(void (^completionHandler)(NSURL *, BOOL,
												      NSError *)) {
			id fileURL = (fixturePath != nil) ? [NSURL fileURLWithPath:fixturePath] : nil;

			completionHandler(fileURL, YES, nil);
			return nil;
		}];
		[provider registerDataRepresentationForTypeIdentifier:@"org.argentum.probe.plain"
			      visibility:NSItemProviderRepresentationVisibilityAll
			     loadHandler:^NSProgress *(void (^completionHandler)(NSData *, NSError *)) {
			completionHandler(fixtureBytes, nil);
			return nil;
		}];
		check("the-file-options-decide-which-identifiers-a-query-answers",
		      [[provider registeredTypeIdentifiersWithFileOptions:0] count] == 2 &&
		      [[provider registeredTypeIdentifiersWithFileOptions:NSItemProviderFileOptionOpenInPlace]
			count] == 1 &&
		      [[[provider registeredTypeIdentifiersWithFileOptions:
				NSItemProviderFileOptionOpenInPlace] objectAtIndex:0]
			isEqualToString:@"org.argentum.probe.openable"] &&
		      [provider hasRepresentationConformingToTypeIdentifier:@"org.argentum.probe.openable"
							       fileOptions:NSItemProviderFileOptionOpenInPlace] &&
		      ![provider hasRepresentationConformingToTypeIdentifier:@"org.argentum.probe.plain"
								fileOptions:NSItemProviderFileOptionOpenInPlace],
		      [[NSString stringWithFormat:@"all=%lu openable=%lu plainInPlace=%d",
			(unsigned long)[[provider registeredTypeIdentifiersWithFileOptions:0] count],
			(unsigned long)[[provider registeredTypeIdentifiersWithFileOptions:
				NSItemProviderFileOptionOpenInPlace] count],
			(int)[provider hasRepresentationConformingToTypeIdentifier:@"org.argentum.probe.plain"
								 fileOptions:NSItemProviderFileOptionOpenInPlace]]
			UTF8String]);
	}
	{
		/* THE ONE OPTION KEY THIS LIBRARY PUBLISHES IS HANDED TO A LOAD HANDLER, AND THE EXPECTED CLASS IS NIL —
		 * the boundary the header names (Apple reads it from the caller's block signature; this library cannot). */
		NSItemProvider *provider = [[NSItemProvider alloc] init];
		__block Class seenExpectation = (Class)@"not nil";
		__block NSDictionary *seenOptions = nil;

		[provider registerItemForTypeIdentifier:FN_TEXT_TYPE
					     loadHandler:^(NSItemProviderCompletionHandler completion,
							   Class expected, NSDictionary *options) {
			seenExpectation = expected;
			seenOptions = options;
			completion(fn_item_value(@"ok"), nil);
		}];
		[provider loadItemForTypeIdentifier:FN_TEXT_TYPE
					    options:[NSDictionary dictionaryWithObject:[NSNumber numberWithInt:4]
									       forKey:NSItemProviderPreferredImageSizeKey]
			  completionHandler:^(id item, NSError *error) {
			(void)item; (void)error;
		}];
		check("a-load-handler-is-given-no-expected-class-and-the-callers-options",
		      seenExpectation == Nil && seenOptions != nil &&
		      [seenOptions objectForKey:NSItemProviderPreferredImageSizeKey] != nil,
		      [[NSString stringWithFormat:@"expected=%s options=%@",
			seenExpectation == Nil ? "Nil" : "NOT NIL", seenOptions] UTF8String]);
	}

	/* ---- THE PREVIEW DOOR, AND COPYING -------------------------------------------------------------- */
	{
		NSItemProvider *plain = [[NSItemProvider alloc] init];
		__block NSError *withoutHandler = nil;
		__block id withHandler = nil;
		NSItemProvider *withPreview = [[NSItemProvider alloc] init];

		[plain loadPreviewImageWithOptions:nil completionHandler:^(id item, NSError *error) {
			(void)item;
			withoutHandler = error;
		}];
		[withPreview setPreviewImageHandler:^(NSItemProviderCompletionHandler completion,
						     Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"the preview"), nil);
		}];
		[withPreview loadPreviewImageWithOptions:nil completionHandler:^(id item, NSError *error) {
			(void)error;
			withHandler = item;
		}];
		check("the-preview-door-uses-the-handler-and-otherwise-refuses",
		      withoutHandler != nil &&
		      [withoutHandler code] == NSItemProviderItemUnavailableError &&
		      [withHandler isEqualToString:@"the preview"],
		      [[NSString stringWithFormat:@"without=%ld with=%@", (long)[withoutHandler code],
			withHandler] UTF8String]);
	}
	{
		NSItemProvider *original = [[NSItemProvider alloc] initWithObject:fixture];
		NSItemProvider *copy = [original copy];
		NSArray *before = [original registeredTypeIdentifiers];

		[copy registerItemForTypeIdentifier:FN_OTHER_TYPE
					loadHandler:^(NSItemProviderCompletionHandler completion,
						      Class expected, NSDictionary *options) {
			(void)expected; (void)options;
			completion(fn_item_value(@"only in the copy"), nil);
		}];
		check("a-copy-is-an-independent-provider-over-the-same-registrations",
		      copy != original &&
		      [[copy registeredTypeIdentifiers] count] == 2 &&
		      [[original registeredTypeIdentifiers] count] == [before count] &&
		      [original hasItemConformingToTypeIdentifier:FN_TEXT_TYPE] &&
		      ![original hasItemConformingToTypeIdentifier:FN_OTHER_TYPE],
		      [[NSString stringWithFormat:@"original=%lu copy=%lu",
			(unsigned long)[[original registeredTypeIdentifiers] count],
			(unsigned long)[[copy registeredTypeIdentifiers] count]] UTF8String]);
	}

	printf(PREFIX " RESULT ok=%d fail=%d\n", okc, failc);
	printf(PREFIX "-STATUS=%d\n", failc ? 1 : 0);
	printf(PREFIX " DONE\n");
	return failc ? 1 : 0;
}
