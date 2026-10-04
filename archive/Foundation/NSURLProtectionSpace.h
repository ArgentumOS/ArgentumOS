/*
 * NSURLProtectionSpace.h
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * A protection space: the realm a server asks for credentials in, and the method it will accept.
 *
 * IT IS A VALUE AND NOT A POLICY. Nothing here decides whether a credential may be sent; it DESCRIBES where
 * one would be sent, and `-receivesCredentialSecurely` is a property of the description (the scheme), not a
 * judgement about the caller. The store that keeps credentials is a different class, and the decision to
 * send one belongs to whoever is talking to the server.
 *
 * REFUSED BY NAME, and the checks assert they are ABSENT: `-serverTrust` and `-distinguishedNames`. Each
 * names a `SecTrustRef` or a certificate field - the SECURITY framework's types - and this system has no
 * certificate stack to give one a meaning (see docs/design/foundation-plan.md §48.1).
 */

#ifndef _FNX_FOUNDATION_NSURLPROTECTIONSPACE_H
#define _FNX_FOUNDATION_NSURLPROTECTIONSPACE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>

NS_ASSUME_NONNULL_BEGIN

/*
 * THE TYPES ARE STRINGS, AND THE VALUES ARE OURS - the same rule as every constant this library declares
 * from Apple's published NAMES (D2). The protocol types take their URL SCHEME ("http", "https", "ftp")
 * because a protection space is built FROM a URL and a scheme is the one form of this fact that is
 * observable to a reader; the proxy and authentication-method types take their own constant name, since
 * neither has a wire form and both exist only to be compared.
 */
extern NSString * const NSURLProtectionSpaceHTTP;
extern NSString * const NSURLProtectionSpaceHTTPS;
extern NSString * const NSURLProtectionSpaceFTP;

extern NSString * const NSURLProtectionSpaceHTTPProxy;
extern NSString * const NSURLProtectionSpaceHTTPSProxy;
extern NSString * const NSURLProtectionSpaceFTPProxy;
extern NSString * const NSURLProtectionSpaceSOCKSProxy;

extern NSString * const NSURLAuthenticationMethodDefault;
extern NSString * const NSURLAuthenticationMethodHTTPBasic;
extern NSString * const NSURLAuthenticationMethodHTTPDigest;
extern NSString * const NSURLAuthenticationMethodHTMLForm;
extern NSString * const NSURLAuthenticationMethodNTLM;
extern NSString * const NSURLAuthenticationMethodNegotiate;
extern NSString * const NSURLAuthenticationMethodClientCertificate;
extern NSString * const NSURLAuthenticationMethodServerTrust;

/* NSCopying IS NOT DECORATION: a protection space is the KEY a credential store files under, and a
 * collection COPIES its keys - so a class that is a key without a copy implementation aborts the moment it
 * is used as one, which is exactly how this was found (a SIGABRT, not a subtle failure). The member is
 * `-copy` rather than Cocoa's `-copyWithZone:`, per this library's own copying deviation in NSObject.h. */
@interface NSURLProtectionSpace : NSObject <NSCopying>
{
	NSString *_host;
	NSString *_protocol;
	NSString *_proxyType;
	NSString *_realm;
	NSString *_authenticationMethod;
	NSInteger _port;
	unsigned int _isProxy:1;
}

/* TWO DOORS, AND WHICH ONE WAS USED IS A PROPERTY (`-isProxy`), not something a caller has to remember:
 * the proxy initialiser takes a PROXY TYPE where the other takes a PROTOCOL, and they are different
 * vocabularies describing different things. */
- (instancetype)initWithHost:(NSString *)host
			port:(NSInteger)port
		    protocol:(nullable NSString *)protocol
		       realm:(nullable NSString *)realm
	  authenticationMethod:(nullable NSString *)authenticationMethod;

- (instancetype)initWithProxyHost:(NSString *)host
			     port:(NSInteger)port
			     type:(nullable NSString *)type
			    realm:(nullable NSString *)realm
	       authenticationMethod:(nullable NSString *)authenticationMethod;

@property (readonly, copy) NSString *host;
/* MINUS ONE MEANS UNKNOWN, which is Apple's own answer and the reason this is an integer rather than a
 * number object: a port that was not given is not the same as port 0. */
@property (readonly) NSInteger port;
@property (readonly, copy, nullable) NSString *protocol;
@property (readonly, copy, nullable) NSString *proxyType;
@property (readonly, copy, nullable) NSString *realm;
@property (readonly, copy) NSString *authenticationMethod;
@property (readonly, getter=isProxy) BOOL proxy;
/* A PROPERTY OF THE SCHEME, not of the caller's intentions: https receives credentials securely and http
 * does not, and that is the whole rule. */
@property (readonly) BOOL receivesCredentialSecurely;

@end

NS_ASSUME_NONNULL_END

#endif /* _FNX_FOUNDATION_NSURLPROTECTIONSPACE_H */
