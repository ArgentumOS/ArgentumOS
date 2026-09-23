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
	/* THE SENDER IS ACCEPTED AND NOT KEPT, which the header states in full: the argument exists so that
	 * Apple-source compatibility holds, and the accessor that would hand it back is not shipped because
	 * Apple files it as Legacy and this library answers "who asked" with the task. */
	(void)sender;
	if(!(self = [super init])) {
		return nil;
	}
	_protectionSpace = [space copy];
	_proposedCredential = [credential copy];
	_failureResponse = [response copy];
	_error = [error copy];
	_previousFailureCount = previousFailureCount;
	return self;
}

- (instancetype)initWithAuthenticationChallenge:(NSURLAuthenticationChallenge *)challenge
					 sender:(id)sender
{
	(void)sender;
	/* A COPY, FIELD FOR FIELD, INCLUDING the count: a challenge rebuilt from another is the same challenge,
	 * which is the whole point of the door existing. */
	return [self initWithProtectionSpace:[challenge protectionSpace]
			  proposedCredential:[challenge proposedCredential]
			previousFailureCount:[challenge previousFailureCount]
			     failureResponse:[challenge failureResponse]
				       error:[challenge error]
				      sender:nil];
}

- (void)dealloc
{
	[_protectionSpace release];
	[_proposedCredential release];
	[_failureResponse release];
	[_error release];
	[super dealloc];
}

- (NSURLProtectionSpace *)protectionSpace { return _protectionSpace; }
- (NSURLCredential *)proposedCredential { return _proposedCredential; }
- (NSInteger)previousFailureCount { return _previousFailureCount; }
- (NSURLResponse *)failureResponse { return _failureResponse; }
- (NSError *)error { return _error; }

/* THE SECRET IS NOT PRINTED HERE EITHER, even indirectly: a proposed credential's description is already
 * careful (NSURLCredential), and this method does not go behind it for the password. */
- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@ space=%@ attempts=%d error=%@>", [self class],
			_protectionSpace, (int)_previousFailureCount,
			(_error != nil ? _error : @"(none)")];
}

@end
