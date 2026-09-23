/*
 * NSURLProtectionSpace.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSURLProtectionSpace.h>

NSString * const NSURLProtectionSpaceHTTP = @"http";
NSString * const NSURLProtectionSpaceHTTPS = @"https";
NSString * const NSURLProtectionSpaceFTP = @"ftp";

NSString * const NSURLProtectionSpaceHTTPProxy = @"NSURLProtectionSpaceHTTPProxy";
NSString * const NSURLProtectionSpaceHTTPSProxy = @"NSURLProtectionSpaceHTTPSProxy";
NSString * const NSURLProtectionSpaceFTPProxy = @"NSURLProtectionSpaceFTPProxy";
NSString * const NSURLProtectionSpaceSOCKSProxy = @"NSURLProtectionSpaceSOCKSProxy";

NSString * const NSURLAuthenticationMethodDefault = @"NSURLAuthenticationMethodDefault";
NSString * const NSURLAuthenticationMethodHTTPBasic = @"NSURLAuthenticationMethodHTTPBasic";
NSString * const NSURLAuthenticationMethodHTTPDigest = @"NSURLAuthenticationMethodHTTPDigest";
NSString * const NSURLAuthenticationMethodHTMLForm = @"NSURLAuthenticationMethodHTMLForm";
NSString * const NSURLAuthenticationMethodNTLM = @"NSURLAuthenticationMethodNTLM";
NSString * const NSURLAuthenticationMethodNegotiate = @"NSURLAuthenticationMethodNegotiate";
NSString * const NSURLAuthenticationMethodClientCertificate = @"NSURLAuthenticationMethodClientCertificate";
NSString * const NSURLAuthenticationMethodServerTrust = @"NSURLAuthenticationMethodServerTrust";

@implementation NSURLProtectionSpace

- (instancetype)fn_initWithHost:(NSString *)host
			   port:(NSInteger)port
		       protocol:(NSString *)protocol
			  realm:(NSString *)realm
	     authenticationMethod:(NSString *)authenticationMethod
			isProxy:(BOOL)isProxy
		      proxyType:(NSString *)proxyType
{
	if(!(self = [super init])) {
		return nil;
	}
	_host = [host copy];
	_port = port;
	_realm = [realm copy];
	/* A SPACE WITH NO METHOD IS STILL A SPACE, and the default is the least specific one rather than nil:
	 * a caller asking "what method?" of a space that never named one should get an answer, and
	 * NSURLAuthenticationMethodDefault is exactly that answer. */
	_authenticationMethod = [(authenticationMethod != nil
				  ? authenticationMethod : NSURLAuthenticationMethodDefault) copy];
	_isProxy = isProxy ? 1 : 0;
	if(isProxy) {
		_proxyType = [proxyType copy];
	} else {
		_protocol = [protocol copy];
	}
	return self;
}

- (instancetype)initWithHost:(NSString *)host
			port:(NSInteger)port
		    protocol:(NSString *)protocol
		       realm:(NSString *)realm
	  authenticationMethod:(NSString *)authenticationMethod
{
	return [self fn_initWithHost:host
				port:port
			    protocol:protocol
			       realm:realm
		  authenticationMethod:authenticationMethod
			     isProxy:NO
			   proxyType:nil];
}

- (instancetype)initWithProxyHost:(NSString *)host
			     port:(NSInteger)port
			     type:(NSString *)type
			    realm:(NSString *)realm
	       authenticationMethod:(NSString *)authenticationMethod
{
	return [self fn_initWithHost:host
				port:port
			    protocol:nil
			       realm:realm
		  authenticationMethod:authenticationMethod
			     isProxy:YES
			   proxyType:type];
}

- (void)dealloc
{
	[_host release];
	[_protocol release];
	[_proxyType release];
	[_realm release];
	[_authenticationMethod release];
	[super dealloc];
}

- (NSString *)host { return _host; }
- (NSInteger)port { return _port; }
- (NSString *)protocol { return _protocol; }
- (NSString *)proxyType { return _proxyType; }
- (NSString *)realm { return _realm; }
- (NSString *)authenticationMethod { return _authenticationMethod; }
- (BOOL)isProxy { return _isProxy ? YES : NO; }

/* HTTPS AND NOTHING ELSE, because it is the only scheme this library speaks that puts credentials on an
 * encrypted channel; https is compared case-insensitively, since a scheme's case is not significant. */
- (BOOL)receivesCredentialSecurely
{
	return (_protocol != nil && [_protocol caseInsensitiveCompare:NSURLProtectionSpaceHTTPS] == NSOrderedSame)
		? YES : NO;
}

/* EQUALITY AND HASH ARE NOT DECORATION HERE: the credential store is keyed BY protection space, so two
 * spaces describing the same realm must match as dictionary keys or `-credentialsForProtectionSpace:` would
 * answer for the object identity of a space its caller rebuilt rather than for the realm itself. Apple's
 * page lists neither method because both are NSObject's - but a value type that is a key has to say what
 * makes two of them the same, and that is this: host, port, realm, method, and WHICH KIND of space it is,
 * which is why the proxy flag participates. */
- (BOOL)isEqual:(id)other
{
	NSURLProtectionSpace *o;

	if(other == self) {
		return YES;
	}
	if(![other isKindOfClass:[NSURLProtectionSpace class]]) {
		return NO;
	}
	o = (NSURLProtectionSpace *)other;
	if(![o->_host isEqualToString:_host] || o->_port != _port || o->_isProxy != _isProxy) {
		return NO;
	}
	if(![o->_authenticationMethod isEqualToString:_authenticationMethod]) {
		return NO;
	}
	/* THE OPTIONAL FIELDS ARE COMPARED BY THE NIL-AWARE IDIOM, on both sides: a space with no realm and a
	 * space whose realm is an empty string are different things, and `isEqual:` on a nil receiver would
	 * answer NO for two spaces that are in fact the same. */
	if(!(o->_realm == _realm || [o->_realm isEqualToString:_realm])) {
		return NO;
	}
	if(!(o->_protocol == _protocol || [o->_protocol isEqualToString:_protocol])) {
		return NO;
	}
	if(!(o->_proxyType == _proxyType || [o->_proxyType isEqualToString:_proxyType])) {
		return NO;
	}
	return YES;
}

/* THE COPY IS A VALUE COPY, which is what makes a protection space usable as a dictionary key: the copy
 * carries the same description and therefore the same -isEqual: and -hash. The signature is `-copy` because
 * that is this library's copy entry point (NSObject.h removed the zone-taking methods deliberately). */
- (id)copy
{
	return [self retain];
}

- (NSUInteger)hash
{
	/* A HASH ONLY HAS TO AGREE WITH EQUALITY, so it is built from the same fields - one of them optional,
	 * which simply contributes 0 when absent. */
	NSUInteger h = [_host hash] ^ (NSUInteger)_port ^ [_authenticationMethod hash];

	if(_realm != nil) {
		h ^= [_realm hash];
	}
	return h;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@ %@%@%@ realm=%@ %@>", [self class],
			(_isProxy ? @"proxy " : @""), _host, (_port >= 0
				? [NSString stringWithFormat:@":%d", (int)_port] : @""),
			_realm, _authenticationMethod];
}

@end
