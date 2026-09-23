/*
 * NSURLCredential.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSURLCredential.h>

@implementation NSURLCredential

+ (instancetype)credentialWithUser:(NSString *)user
			  password:(NSString *)password
		       persistence:(NSURLCredentialPersistence)persistence
{
	return [[[self alloc] initWithUser:user password:password persistence:persistence] autorelease];
}

- (instancetype)initWithUser:(NSString *)user
		    password:(NSString *)password
		 persistence:(NSURLCredentialPersistence)persistence
{
	if(!(self = [super init])) {
		return nil;
	}
	_user = [user copy];
	_password = [password copy];
	_persistence = persistence;
	return self;
}

- (void)dealloc
{
	[_user release];
	[_password release];
	[super dealloc];
}

- (NSString *)user { return _user; }
- (NSString *)password { return _password; }
- (NSURLCredentialPersistence)persistence { return _persistence; }

/* TRUE WHEN THERE IS A PASSWORD TO OFFER. In v1 every credential this class can build is a password one, so
 * the honest statement is that this answers YES for exactly the credentials that have a password - and the
 * NO case belongs to the identity kind, which is refused, so it is UNREACHABLE HERE RATHER THAN ABSENT. The
 * property is still shipped because it is part of the surface and because a caller asking it is asking the
 * right question. */
- (BOOL)hasPassword { return (_password != nil) ? YES : NO; }

- (id)copy { return [self retain]; }

/* THE PASSWORD IS NEVER PRINTED, and that is a rule rather than a preference: a credential's description
 * turns up in logs, in a debugger, and in an exception message, and a secret that reaches any of those has
 * leaked. The user is shown because it identifies the credential; the secret is replaced by its presence. */
- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@ user=%@ password=%@ persistence=%d>", [self class],
			(_user != nil ? _user : @"(none)"),
			(_password != nil ? @"(set)" : @"(none)"),
			(int)_persistence];
}

@end
