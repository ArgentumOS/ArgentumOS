/*
 * NSURLAuthenticationChallenge.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSURLAuthenticationChallenge.h>
#import <Foundation/NSURLCredential.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSError.h>

@implementation NSURLAuthenticationChallenge

- (instancetype)initWithProtectionSpace:(NSURLProtectionSpace *)space
		     proposedCredential:(NSURLCredential *)credential
		   previousFailureCount:(NSInteger)previousFailureCount
			failureResponse:(NSURLResponse *)response
				  error:(NSError *)error
				 sender:(id)sender
{
	/* THE SENDER IS KEPT, AND IT IS RETAINED: a challenge outlives the call that built it, and the delegate
	 * that receives it may answer through this object after this frame is gone. */
	if(!(self = [super init])) {
		return nil;
	}
	_protectionSpace = [space copy];
	_proposedCredential = [credential copy];
	_failureResponse = [response copy];
	_error = [error copy];
	_previousFailureCount = previousFailureCount;
	_sender = [sender retain];
	return self;
}

- (instancetype)initWithAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
					 sender:(id)sender
{
	/* A COPY, FIELD FOR FIELD, INCLUDING the count: a challenge rebuilt from another is the same challenge,
	 * which is the whole point of the door existing. THE SENDER IS THE ARGUMENT'S, NOT THE ORIGINAL'S - the
	 * door exists so a caller (or a transport) can hand out a challenge whose answers go somewhere of ITS
	 * choosing, and passing the original's would make this door unable to do that. */
	return [self initWithProtectionSpace:[challenge protectionSpace]
			  proposedCredential:[challenge proposedCredential]
			previousFailureCount:[challenge previousFailureCount]
			     failureResponse:[challenge failureResponse]
				       error:[challenge error]
				      sender:sender];
}

- (void)dealloc
{
	[_protectionSpace release];
	[_proposedCredential release];
	[_failureResponse release];
	[_error release];
	[_sender release];
	[super dealloc];
}

- (NSURLProtectionSpace *)protectionSpace { return _protectionSpace; }
- (NSURLCredential *)proposedCredential { return _proposedCredential; }
- (NSInteger)previousFailureCount { return _previousFailureCount; }
- (NSURLResponse *)failureResponse { return _failureResponse; }
- (NSError *)error { return _error; }

/* WHO ASKED, AND THE DOOR AN ANSWER GOES BACK THROUGH (§62.27). Nil when a caller built the challenge with
 * no sender - the LOADING SYSTEM is what supplies one - which is why the property is nullable.
 *
 * THIS ACCESSOR IS THE ONE §48.1 REFUSED, AND THE SWEEP CAUGHT ITS FIRST VERSION MISSING: the property was
 * declared and the ivar was stored and released, but the accessor itself was never written, and
 * `foundation-sweep --unimplemented` named it ("declared selectors with no implementation: NEW: 1 --
 * NSURLAuthenticationChallenge sender"). A declared property with no accessor compiles and answers nil at
 * runtime, which is exactly the kind of gap a probe would have to be pointed at to find. */
- (id <NSURLAuthenticationChallengeSender>)sender { return _sender; }

/* THE SECRET IS NOT PRINTED HERE EITHER, even indirectly: a proposed credential's description is already
 * careful (NSURLCredential), and this method does not go behind it for the password. */
- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@ space=%@ attempts=%d error=%@>", [self class],
			_protectionSpace, (int)_previousFailureCount,
			(_error != nil ? _error : @"(none)")];
}

@end
