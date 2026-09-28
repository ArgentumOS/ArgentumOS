/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDistantObjectRequest.m — the request a delegate may answer itself (§62.91). MANUAL OWNERSHIP.
 *
 * THE CONNECTION AND THE CONVERSATION ARE NOT RETAINED; the invocation and the reply name are. That split is not
 * arbitrary: the connection owns the request for as long as it is being handled (so retaining it back would be a
 * cycle), while the invocation and the reply name are the request's OWN business and must outlive whoever made it.
 *
 * THE ONE REPLY is enforced here rather than hoped for. Apple's contract gives a request exactly one answer; a
 * second `-replyWithException:` would be a second answer to a question asked once, and since the client has
 * already stopped waiting for the first, silence about it would be the worst outcome — so it raises.
 */

#import <Foundation/NSDistantObjectRequest.h>
#import <Foundation/FNDistantObjectRequest.h>
#import <Foundation/NSConnection.h>
#import <Foundation/NSException.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSMethodSignature.h>

@implementation NSDistantObjectRequest

- (instancetype)fnInitWithConnection:(NSConnection *)connection
			conversation:(nullable id)conversation
			  invocation:(NSInvocation *)invocation
			   replyName:(NSString *)replyName
{
	self = [super init];
	if (self != nil) {
		_connection = connection;	/* NOT retained: see the file's note */
		_conversation = conversation;	/* NOT retained, and it may be nil */
		_invocation = [invocation retain];
		_replyName = [replyName copy];
	}
	return self;
}

- (void)dealloc
{
	[(id)_invocation release];
	[(id)_replyName release];
	[super dealloc];
}

- (NSConnection *)connection { return _connection; }
- (nullable id)conversation { return _conversation; }
- (NSInvocation *)invocation { return _invocation; }

- (void)replyWithException:(nullable NSException *)exception
{
	NSMethodSignature *signature = [_invocation methodSignature];
	id value = nil;

	if (_replied) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"a distant object request has ONE reply, and this one has already been answered"];
	}
	_replied = YES;
	if (exception == nil) {
		/* THE INVOCATION'S RETURN VALUE — whatever the delegate put there while handling the request. A method
		 * that returns nothing contributes nothing: the RETURN TYPE is what says so, not the value. */
		const char *returnType = signature != nil ? [signature methodReturnType] : NULL;

		if (returnType != NULL && returnType[0] == '@') {
			[_invocation getReturnValue:&value];
		}
	}
	[(NSConnection *)_connection fnReplyToName:(NSString *)_replyName
					     value:value
					     error:nil
					 exception:exception];
}

@end
