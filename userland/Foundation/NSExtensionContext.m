/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NSExtensionContext (§62.92) — the class, the lifecycle protocol, and the host seam's implementation.
 */

#import <Foundation/NSExtensionContext.h>
#import <Foundation/FNExtensionContext.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

NSString * const NSExtensionItemsAndErrorsKey = @"NSExtensionItemsAndErrorsKey";

@implementation NSExtensionContext

/* NO PUBLIC CONSTRUCTOR, by design and by Apple's own description: a context comes from the host. The seam's
 * factory below is the only way in, and this refuses with the ground rather than returning a context whose items
 * no host sent. */
- (instancetype)init
{
	[NSException raise:NSInvalidArgumentException
		    format:@"NSExtensionContext: -init is not the way in; a context comes from the host "
			   @"(FNExtensionContext.h's factory is the host, in miniature)"];
	return nil;
}

- (void)dealloc
{
	[_inputItems release];
	[_endingItems release];
	[_endingError release];
	[super dealloc];
}

- (NSArray *)inputItems
{
	return (NSArray *)_inputItems;
}

/* THE ONE ENDING. See the file note in the header for why the second call raises rather than being ignored. */
- (void)completeRequestReturningItems:(NSArray *)items completionHandler:(void (^)(BOOL))completionHandler
{
	if (_ended)
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSExtensionContext: this request has already ended; a host has one ending per request"];
	_ended = YES;
	_endingItems = [(items ? items : [NSArray array]) copy];
	if (completionHandler != nil)
		completionHandler(NO);	/* our 'expired' is always NO: nothing terminated this request early */
}

- (void)cancelRequestWithError:(NSError *)error
{
	if (_ended)
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSExtensionContext: this request has already ended; a host has one ending per request"];
	if (error == nil)
		[NSException raise:NSInvalidArgumentException
			    format:@"NSExtensionContext: -cancelRequestWithError: needs an error; a cancellation must say why"];
	_ended = YES;
	_endingError = [error retain];
}


/* ================== THE HOST DOOR (§63.114) ================== */
- (void)openURL:(NSURL *)URL completionHandler:(void (^)(BOOL success))completionHandler
{
	/* ⚠ APPLE'S CONTRACT IS "ASKS THE HOST" AND THERE IS NO HOST HERE, SO THE REQUEST CANNOT BE HONOURED. THE DOOR DOES
	 * NOT THROW AND DOES NOT PRETEND: **its completion takes `success`, and `NO` is what actually happened.** The
	 * handler is optional in Apple's own declaration (`_Nullable`), so it is guarded rather than assumed. */
	(void)URL;
	if (completionHandler != NULL) {
		completionHandler(NO);
	}
}

@end

@implementation NSExtensionContext (FNPrivate)

/* The seam's factory, and the only way to make one. */
- (instancetype)fnInitWithInputItems:(NSArray *)inputItems
{
	self = [super init];
	if (self != nil) {
		_inputItems = [(inputItems ? inputItems : [NSArray array]) copy];
		_endingItems = nil;
		_endingError = nil;
		_ended = NO;
	}
	return self;
}

- (void)fnBeginRequestWithHandler:(id <NSExtensionRequestHandling>)handler
{
	[handler beginRequestWithExtensionContext:self];
}

- (BOOL)fnHasEnded
{
	return _ended;
}

- (NSArray *)fnEndingItems
{
	return (NSArray *)_endingItems;
}

- (NSError *)fnEndingError
{
	return (NSError *)_endingError;
}

@end

/* THE HOST, IN MINIATURE: it makes the context and it hands it over. */
NSExtensionContext *FNExtensionContextMakeWithInputItems(NSArray *inputItems)
{
	return [[[NSExtensionContext alloc] fnInitWithInputItems:inputItems] autorelease];
}

void FNExtensionContextBeginRequestWithHandler(NSExtensionContext *context,
					       id <NSExtensionRequestHandling> handler)
{
	[context fnBeginRequestWithHandler:handler];
}

BOOL FNExtensionContextHasEnded(NSExtensionContext *context)
{
	return [context fnHasEnded];
}

NSArray *FNExtensionContextEndingItems(NSExtensionContext *context)
{
	return [context fnEndingItems];
}

NSError *FNExtensionContextEndingError(NSExtensionContext *context)
{
	return [context fnEndingError];
}
