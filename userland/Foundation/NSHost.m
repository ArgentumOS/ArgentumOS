/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSHost.m — the host as a name-service answer (plan §62.63). MANUAL OWNERSHIP.
 *
 * THE CLASS IS A VIEW OVER THE SYSTEM RESOLVER, which is what Apple's own sentence makes it: the doors "use the
 * available network administration services to discover this information but do NOT CONTACT THE HOST ITSELF".
 * So there is no socket here, no timeout and no protocol: getaddrinfo(3) asks the name service (musl's in this
 * tree, reading the FSH hosts domain — plan §M5), and getnameinfo(3) with NI_NUMERICHOST turns its answers into
 * the textual addresses a caller expects. A name the service does not know simply has no NSHost.
 *
 * THE REVERSE LOOKUP IS PART OF THE SAME SERVICE, and it is what makes `-name` useful for an address-first host:
 * getnameinfo(3) WITHOUT NI_NUMERICHOST is a name-service query like any other, and it is the one that turns
 * 127.0.0.1 back into "localhost".
 *
 * DUPLICATES ARE REMOVED AND ORDER IS THE RESOLVER'S: getaddrinfo returns one entry per address family and
 * socket type, so the same address can appear several times, and `-addresses` is a SET in a caller's terms
 * (Apple: "returns all the network addresses") while the FIRST of them is what `-address` answers.
 */

#import <Foundation/NSHost.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>

#include <netdb.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* THE PRIVATE DOOR THE FACTORIES BUILD THROUGH: a host is made FROM its two lists, and nothing else can make
 * one — which is why -init is not part of this class's surface (Apple: "do not use alloc/init"). */
@interface NSHost (FNPrivate)
- (instancetype)fnInitWithNames:(NSArray *)names addresses:(NSArray *)addresses;
@end

/* ONE QUESTION TO THE NAME SERVICE: `node` is a NAME or an address in its textual form. It answers nil when the
 * service has nothing, which is a legitimate answer (Apple: "this may mean the information is incomplete"). */
static NSHost *fn_host_from_lookup(const char *node)
{
	struct addrinfo hints, *res = NULL, *it;
	NSMutableArray *names = [NSMutableArray array];
	NSMutableArray *addresses = [NSMutableArray array];
	NSHost *host;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;		/* both families: which one is usable is the caller's business */
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_CANONNAME;
	if (node == NULL || getaddrinfo(node, NULL, &hints, &res) != 0 || res == NULL) {
		return nil;
	}
	for (it = res; it != NULL; it = it->ai_next) {
		char text[INET6_ADDRSTRLEN];
		NSString *value;

		if (getnameinfo(it->ai_addr, it->ai_addrlen, text, sizeof(text), NULL, 0,
				NI_NUMERICHOST) == 0) {
			value = [NSString stringWithUTF8String:text];
			if (value != nil && ![addresses containsObject:value]) {
				[addresses addObject:value];
			}
		}
	}
	/* THE CANONICAL NAME COMES OFF THE SAME ANSWER (AI_CANONNAME), and it is the one entry that carries it. */
	for (it = res; it != NULL; it = it->ai_next) {
		if (it->ai_canonname != NULL && it->ai_canonname[0] != '\0') {
			NSString *value = [NSString stringWithUTF8String:it->ai_canonname];

			if (value != nil && ![names containsObject:value]) {
				[names addObject:value];
			}
			break;
		}
	}
	freeaddrinfo(res);

	/* AND THE REVERSE NAME FOR THE FIRST ADDRESS, because a caller asking for an ADDRESS usually wants to know
	 * what it is called — and this is what Apple's "may contain more than one name" describes. The same local
	 * service answers it; nothing here opens a connection. */
	if ([addresses count] > 0) {
		struct addrinfo *back = NULL;
		char name[NI_MAXHOST];

		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		if (getaddrinfo([[addresses objectAtIndex:0] UTF8String], NULL, &hints, &back) == 0 &&
		    back != NULL) {
			if (getnameinfo(back->ai_addr, back->ai_addrlen, name, sizeof(name), NULL, 0, 0) == 0) {
				NSString *value = [NSString stringWithUTF8String:name];

				if (value != nil && ![names containsObject:value]) {
					[names addObject:value];
				}
			}
			freeaddrinfo(back);
		}
	}
	host = [[[NSHost alloc] fnInitWithNames:names addresses:addresses] autorelease];
	return host;
}

@implementation NSHost

- (instancetype)fnInitWithNames:(NSArray *)names addresses:(NSArray *)addresses
{
	self = [super init];
	if (self != nil) {
		/* IMMUTABLE COPIES, ONE PER LIST: Apple states the methods are thread-safe, and a caller that could
		 * mutate what it was handed would be mutating another thread's answer. */
		_names = [[NSArray alloc] initWithArray:names];
		_addresses = [[NSArray alloc] initWithArray:addresses];
	}
	return self;
}

- (void)dealloc
{
	[_names release];
	[_addresses release];
	[super dealloc];		/* NSObject's -dealloc is what frees the instance */
}

+ (nullable NSHost *)currentHost
{
	char kernelName[256];
	NSHost *resolved;

	if (gethostname(kernelName, sizeof(kernelName)) != 0) {
		return nil;
	}
	kernelName[sizeof(kernelName) - 1] = '\0';
	resolved = fn_host_from_lookup(kernelName);
	if (resolved != nil) {
		return resolved;
	}
	/* THE NAME SERVICE MAY NOT KNOW THIS HOST (a machine whose name is not in the hosts domain, or none), and
	 * Apple's class still answers: the kernel's name is a name, and "information may be incomplete" is the
	 * published caveat rather than a failure. */
	return [[[NSHost alloc] fnInitWithNames:[NSArray arrayWithObject:
					       [NSString stringWithUTF8String:kernelName]]
				     addresses:[NSArray array]] autorelease];
}

+ (nullable NSHost *)hostWithName:(NSString *)name
{
	if (name == nil) {
		return nil;
	}
	return fn_host_from_lookup([name UTF8String]);
}

+ (nullable NSHost *)hostWithAddress:(NSString *)address
{
	if (address == nil) {
		return nil;
	}
	return fn_host_from_lookup([address UTF8String]);
}

/* ONE AND ALL. "Chosen arbitrarily if multiple" is Apple's own wording for the singular doors, and it is
 * arbitrary here for the same reason it is arbitrary there: the name service reports no preference. */
- (nullable NSString *)address
{
	return [_addresses count] > 0 ? [_addresses objectAtIndex:0] : nil;
}

- (NSArray *)addresses
{
	return _addresses;
}

- (nullable NSString *)name
{
	return [_names count] > 0 ? [_names objectAtIndex:0] : nil;
}

- (NSArray *)names
{
	return _names;
}

- (nullable NSString *)localizedName
{
	return [self name];
}

/* TWO HOSTS ARE ONE HOST WHEN THEY SHARE AN ADDRESS. A name cannot decide it (the aliases of one host are
 * different strings) and an ADDRESS can, because that is what a connection is actually made to. When either side
 * has no address at all the question falls back to the names, which is the only evidence left. */
- (BOOL)isEqualToHost:(NSHost *)aHost
{
	NSUInteger i;

	if (aHost == nil) {
		return NO;
	}
	if (aHost == self) {
		return YES;
	}
	for (i = 0; i < [_addresses count]; i++) {
		if ([[aHost addresses] containsObject:[_addresses objectAtIndex:i]]) {
			return YES;
		}
	}
	if ([_addresses count] == 0 || [[aHost addresses] count] == 0) {
		for (i = 0; i < [_names count]; i++) {
			if ([[aHost names] containsObject:[_names objectAtIndex:i]]) {
				return YES;
			}
		}
	}
	return NO;
}

/* THE CACHE DOORS, ANSWERING AS APPLE DOCUMENTS THEM NOW: the class's own annotation on all three is "Caching no
 * longer supported", so there is no cache, `+isHostCacheEnabled` says so, and the other two do nothing. That is
 * a published answer rather than a stub. */
+ (BOOL)isHostCacheEnabled
{
	return NO;
}

+ (void)setHostCacheEnabled:(BOOL)flag
{
	(void)flag;
}

+ (void)flushHostCache
{
}

@end
