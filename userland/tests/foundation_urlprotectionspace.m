/*
 * foundation_urlprotectionspace.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static int lastcheck;

static void check(const char *name, BOOL held, NSString *why)
{
	lastcheck = held;	/* read by covers() */
	if(held) {
		okc++;
		printf("FOUNDATION-URLPROTECTIONSPACE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-URLPROTECTIONSPACE %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* covers("NSBundle", "resourcePath") - the behavioural claim, piggybacked on the check above it. */
static void covers_(const char *cls, const char *sel)
{
	if (lastcheck) {
		printf("COVERS %s %s\n", cls, sel);
	}
}
#define covers(cls, sel) covers_(cls, sel)

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE TWO DOORS DESCRIBE DIFFERENT THINGS, AND THE SPACE SAYS WHICH -------------------------- */
	{
		NSURLProtectionSpace *server = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:@"restricted" authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
		NSURLProtectionSpace *proxy = [[NSURLProtectionSpace alloc]
			initWithProxyHost:@"proxy.example.com" port:8080 type:NSURLProtectionSpaceHTTPProxy
				   realm:nil authenticationMethod:NSURLAuthenticationMethodDefault];

		check("a-server-space-keeps-what-it-was-given",
		      [[server host] isEqualToString:@"example.com"] && [server port] == 443 &&
		      [[server protocol] isEqualToString:NSURLProtectionSpaceHTTPS] &&
		      [[server realm] isEqualToString:@"restricted"] &&
		      [[server authenticationMethod] isEqualToString:NSURLAuthenticationMethodHTTPBasic],
		      @"host, port, protocol, realm and method all survive");
	covers("NSURLProtectionSpace", "authenticationMethod");
	covers("NSURLProtectionSpace", "port");
	covers("NSURLProtectionSpace", "realm");
		check("a-server-space-is-not-a-proxy", ![server isProxy] && [server proxyType] == nil,
		      @"the server door does not set the proxy flag or a proxy type");
		check("a-proxy-space-says-so", [proxy isProxy] &&
		      [[proxy proxyType] isEqualToString:NSURLProtectionSpaceHTTPProxy] &&
		      [proxy protocol] == nil,
		      @"the proxy door sets the flag and the type, and leaves protocol absent");
		check("a-proxy-only-field-does-not-leak", [server proxyType] == nil,
		      @"a server space has no proxy type");
	}

	/* --- PORT MINUS ONE IS UNKNOWN, NOT ZERO -------------------------------------------------------- */
	{
		NSURLProtectionSpace *space = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:-1 protocol:NSURLProtectionSpaceHTTP
			       realm:nil authenticationMethod:NSURLAuthenticationMethodDefault];

		check("an-unknown-port-is-minus-one", [space port] == -1,
		      @"-1 distinguishes 'not given' from port 0, which is Apple's own answer");
	}

	/* --- THE DERIVED RULES -------------------------------------------------------------------------- */
	{
		NSURLProtectionSpace *none = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:80 protocol:NSURLProtectionSpaceHTTP
			       realm:nil authenticationMethod:nil];
		NSURLProtectionSpace *secure = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:nil authenticationMethod:NSURLAuthenticationMethodDefault];
		NSURLProtectionSpace *plain = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:21 protocol:NSURLProtectionSpaceFTP
			       realm:nil authenticationMethod:NSURLAuthenticationMethodDefault];

		check("a-method-less-space-answers-the-default",
		      [[none authenticationMethod] isEqualToString:NSURLAuthenticationMethodDefault],
		      @"a space that never named a method still answers, with the least specific one");
		check("https-receives-credentials-securely", [secure receivesCredentialSecurely],
		      @"the secure answer follows from the scheme");
		check("http-does-not", ![none receivesCredentialSecurely],
		      @"and an unencrypted scheme does not claim otherwise");
		check("ftp-does-not-either", ![plain receivesCredentialSecurely],
		      @"the rule is https and nothing else");
	}

	/* --- THE PROPERTY THE STORE WILL DEPEND ON ------------------------------------------------------ */
	{
		NSURLProtectionSpace *a = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:@"r" authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
		NSURLProtectionSpace *b = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:@"r" authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
		NSURLProtectionSpace *c = [[NSURLProtectionSpace alloc]
			initWithHost:@"example.com" port:443 protocol:NSURLProtectionSpaceHTTPS
			       realm:@"OTHER" authenticationMethod:NSURLAuthenticationMethodHTTPBasic];
		NSMutableDictionary *store = [NSMutableDictionary dictionary];

		[store setObject:@"kept" forKey:a];
		check("two-spaces-for-one-realm-are-equal", [a isEqual:b],
		      @"a rebuilt space must match the one it describes, or a credential store cannot find it");
		check("and-they-hash-alike", [a hash] == [b hash],
		      @"equality without a matching hash would lose the key");
		check("a-different-realm-is-a-different-space", ![a isEqual:c],
		      @"the realm is part of the identity");
		check("the-store-finds-it-by-a-rebuilt-key",
		      [[store objectForKey:b] isEqualToString:@"kept"],
		      @"which is the whole reason the two methods exist");
	}

	/* --- WHAT IS REFUSED, ASSERTED ABSENT ----------------------------------------------------------- */
	check("the-trust-door-is-absent",
	      ![NSURLProtectionSpace instancesRespondToSelector:NSSelectorFromString(@"serverTrust")],
	      @"a SecTrustRef is the Security framework's type and this system has no trust stack");
	check("the-certificate-fields-are-absent",
	      ![NSURLProtectionSpace instancesRespondToSelector:NSSelectorFromString(@"distinguishedNames")],
	      @"distinguished names are certificate fields, refused with the rest of that surface");

	printf("FOUNDATION-URLPROTECTIONSPACE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-URLPROTECTIONSPACE-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-URLPROTECTIONSPACE DONE\n");
	return failc ? 1 : 0;
}
