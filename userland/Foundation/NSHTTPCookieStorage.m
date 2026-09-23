/*
 * NSHTTPCookieStorage.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSHTTPCookieStorage.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLSessionTask.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDate.h>

NSString * const NSHTTPCookieManagerCookiesChangedNotification =
	@"NSHTTPCookieManagerCookiesChangedNotification";

/*
 * WHICH COOKIES A URL MAY SEE IS THE WHOLE SUBSTANCE OF THIS CLASS, and it is RFC 6265's two rules - so it
 * is written as the two rules rather than as a heuristic. Everything else here is bookkeeping; if these
 * two functions are wrong, a cookie leaks to a host that should not have it, which is the failure that
 * matters.
 *
 * DOMAIN (5.1.3): a cookie whose Domain starts with a dot matches that domain AND its subdomains; a cookie
 * WITHOUT the dot matches the host exactly AND its subdomains too, but never a host that merely ENDS with
 * the same letters - which is why the length comparisons below are not paranoia: "notexample.com" ends with
 * "example.com" and must not match it.
 */
static BOOL fn_domain_matches(NSString *cookieDomain, NSString *host)
{
	if(cookieDomain == nil || host == nil) {
		return NO;
	}
	if([host isEqualToString:cookieDomain]) {
		return YES;
	}
	if([cookieDomain hasPrefix:@"."]) {
		NSString *bare = [cookieDomain substringFromIndex:1];

		if([host isEqualToString:bare]) {
			return YES;
		}
		return [host hasSuffix:cookieDomain];
	}
	if([host hasSuffix:cookieDomain]) {
		return [host length] > [cookieDomain length] &&
		       [[host substringFromIndex:[host length] - [cookieDomain length] - 1]
				hasPrefix:@"."];
	}
	return NO;
}

/*
 * PATH (5.1.4): a cookie path matches when it IS the request path, or is a prefix of it that ends at a "/"
 * boundary. The boundary is the part a naive `hasPrefix:` misses - cookie path "/foo" must not be sent with
 * "/foobar", because the next character has to be a separator.
 */
static BOOL fn_path_matches(NSString *cookiePath, NSString *requestPath)
{
	if(cookiePath == nil || [cookiePath length] == 0) {
		return YES;
	}
	if([requestPath isEqualToString:cookiePath]) {
		return YES;
	}
	if([requestPath hasPrefix:cookiePath]) {
		if([cookiePath hasSuffix:@"/"]) {
			return YES;
		}
		return [[requestPath substringFromIndex:[cookiePath length]] hasPrefix:@"/"];
	}
	return NO;
}

/* TWO COOKIES ARE THE SAME COOKIE when their name, domain and path agree - RFC 6265's identity rule, and
 * the reason `-setCookie:` REPLACES rather than accumulates. Without this a server that re-sends a cookie
 * would grow the store without bound and the oldest copy would win the lookup. */
static BOOL fn_same_cookie(NSHTTPCookie *a, NSHTTPCookie *b)
{
	NSString *ad = [a domain], *bd = [b domain];

	return [[a name] isEqualToString:[b name]] &&
	       [[a path] isEqualToString:[b path]] &&
	       (ad == bd || [ad isEqualToString:bd]);
}

@implementation NSHTTPCookieStorage

+ (NSHTTPCookieStorage *)sharedHTTPCookieStorage
{
	static NSHTTPCookieStorage *shared = nil;

	if(shared == nil) {
		shared = [[self alloc] init];
	}
	return shared;
}

- (instancetype)init
{
	if(!(self = [super init])) {
		return nil;
	}
	_cookies = [[NSMutableArray alloc] init];
	_lock = [[NSLock alloc] init];
	/* THE DEFAULT IS THE PERMISSIVE ONE, which is why its case number is 0 in the header: a store whose
	 * default refused everything would look broken to a caller who never set a policy at all. */
	_policy = NSHTTPCookieAcceptPolicyAlways;
	return self;
}

- (void)dealloc
{
	[_cookies release];
	[_lock release];
	[super dealloc];
}

- (NSHTTPCookieAcceptPolicy)cookieAcceptPolicy
{
	return _policy;
}

- (void)setCookieAcceptPolicy:(NSHTTPCookieAcceptPolicy)policy
{
	[_lock lock];
	_policy = policy;
	[_lock unlock];
	/* NOTHING IS POSTED: the accept-policy notification is a DEPRECATED name (see the header), so this
	 * library does not carry it. A setter that quietly does its job is the honest shape for that. */
}

/* THE POLICY GATES INSERTION, which is the only place it can be observed: a store that accepted a cookie
 * and then declined to hand it back would be indistinguishable from one with no policy at all. */
- (BOOL)fn_wouldAccept
{
	return _policy == NSHTTPCookieAcceptPolicyAlways ? YES : NO;
}

- (void)fn_insert:(NSHTTPCookie *)cookie
{
	NSUInteger i;

	for(i = 0; i < [_cookies count]; i++) {
		if(fn_same_cookie([_cookies objectAtIndex:i], cookie)) {
			[_cookies replaceObjectAtIndex:i withObject:cookie];
			return;
		}
	}
	[_cookies addObject:cookie];
}

- (void)fn_notifyCookiesChanged
{
	[[NSNotificationCenter defaultCenter]
		postNotificationName:NSHTTPCookieManagerCookiesChangedNotification object:self];
}

- (void)setCookie:(NSHTTPCookie *)cookie
{
	BOOL changed = NO;

	if(cookie == nil) {
		return;
	}
	[_lock lock];
	if([self fn_wouldAccept]) {
		[self fn_insert:cookie];
		changed = YES;
	}
	[_lock unlock];
	/* ONLY A REAL CHANGE IS ANNOUNCED. The first version posted unconditionally here while
	 * `-setCookies:forURL:mainDocumentURL:` posted only on a change - two doors of one class disagreeing
	 * about what "changed" means, which the probe caught by making the difference observable: a store told
	 * to refuse everything still announced that its contents had changed. */
	if(changed) {
		[self fn_notifyCookiesChanged];
	}
}

- (void)setCookies:(NSArray *)cookies
	    forURL:(NSURL *)URL
  mainDocumentURL:(NSURL *)mainDocumentURL
{
	NSEnumerator *e;
	NSHTTPCookie *cookie;
	BOOL accepted = NO;

	[_lock lock];
	e = [cookies objectEnumerator];
	while((cookie = [e nextObject]) != nil) {
		/* THE THIRD POLICY CASE IS THE ONLY ONE THAT NEEDS THE EXTRA ARGUMENT, which is why the method
		 * takes a main document URL at all: it exists so a third-party response's cookies can be refused. */
		if(_policy == NSHTTPCookieAcceptPolicyNever) {
			continue;
		}
		if(_policy == NSHTTPCookieAcceptPolicyOnlyFromMainDocumentDomain &&
		   mainDocumentURL != nil &&
		   !fn_domain_matches([mainDocumentURL host], [URL host])) {
			continue;
		}
		[self fn_insert:cookie];
		accepted = YES;
	}
	[_lock unlock];
	if(accepted) {
		[self fn_notifyCookiesChanged];
	}
}

- (void)deleteCookie:(NSHTTPCookie *)cookie
{
	NSUInteger i;

	if(cookie == nil) {
		return;
	}
	[_lock lock];
	for(i = 0; i < [_cookies count]; i++) {
		if(fn_same_cookie([_cookies objectAtIndex:i], cookie)) {
			[_cookies removeObjectAtIndex:i];
			break;
		}
	}
	[_lock unlock];
	[self fn_notifyCookiesChanged];
}

- (void)removeCookiesSinceDate:(NSDate *)date
{
	NSUInteger i = 0;
	BOOL removed = NO;

	if(date == nil) {
		return;
	}
	[_lock lock];
	while(i < [_cookies count]) {
		NSDate *expiry = [[_cookies objectAtIndex:i] expiresDate];

		/* A COOKIE'S OWN EXPIRY DECIDES, and a cookie with NO expiry is never removed by a date: it is a
		 * session cookie, not an expired one. */
		if(expiry != nil && [expiry compare:date] != NSOrderedAscending) {
			[_cookies removeObjectAtIndex:i];
			removed = YES;
		} else {
			i++;
		}
	}
	[_lock unlock];
	if(removed) {
		[self fn_notifyCookiesChanged];
	}
}

- (NSArray *)cookies
{
	NSArray *copy;

	[_lock lock];
	copy = [[_cookies copy] autorelease];
	[_lock unlock];
	return copy;
}

- (NSArray *)cookiesForURL:(NSURL *)URL
{
	NSMutableArray *matched = [NSMutableArray array];
	NSString *host = [URL host];
	NSString *path = [URL path];
	NSString *scheme = [URL scheme];
	NSEnumerator *e;
	NSHTTPCookie *cookie;

	if(host == nil) {
		return matched;
	}
	if(path == nil || [path length] == 0) {
		path = @"/";
	}
	[_lock lock];
	e = [_cookies objectEnumerator];
	while((cookie = [e nextObject]) != nil) {
		if(!fn_domain_matches([cookie domain], host)) {
			continue;
		}
		if(!fn_path_matches([cookie path], path)) {
			continue;
		}
		/* A SECURE COOKIE IS NOT SENT OVER A CLEAR CHANNEL, and the comparison is case-insensitive because
		 * a URL's scheme arrives in whatever case its author typed. */
		if([cookie isSecure] && [scheme caseInsensitiveCompare:@"https"] != NSOrderedSame) {
			continue;
		}
		[matched addObject:cookie];
	}
	[_lock unlock];
	return matched;
}

- (NSArray *)sortedCookiesUsingDescriptors:(NSArray *)sortDescriptors
{
	NSArray *snapshot = [self cookies];

	/* THE SORT IS `NSSortDescriptor`'S, not a comparator invented here: it is what the class is for, and it
	 * is why this door exists at all. A nil descriptor list sorts nothing, which the collection already
	 * answers. */
	if(sortDescriptors == nil || [sortDescriptors count] == 0) {
		return snapshot;
	}
	return [snapshot sortedArrayUsingDescriptors:sortDescriptors];
}

/* THE TASK-SCOPED PAIR ANSWERS THROUGH THE TASK'S OWN REQUEST, so there is no second index to keep in
 * step with the first: a task names one URL, and the store's one answer is `-cookiesForURL:`. */
- (void)storeCookies:(NSArray *)cookies forTask:(NSURLSessionTask *)task
{
	NSURLRequest *request = [task originalRequest];

	[self setCookies:cookies forURL:[request URL] mainDocumentURL:nil];
}

- (void)getCookiesForTask:(NSURLSessionTask *)task completionHandler:(void (^)(NSArray *))completionHandler
{
	NSURLRequest *request = [task originalRequest];

	if(completionHandler == nil) {
		return;
	}
	completionHandler([self cookiesForURL:[request URL]]);
}

@end
