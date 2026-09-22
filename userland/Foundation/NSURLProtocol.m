/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSURLProtocol.m — the seam's machinery: the registry, the request-property table, and the abstract
 * base's defaults. The design and the reasoning are in NSURLProtocol.h.
 */
#import <Foundation/NSURLProtocol.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSDictionary.h>	/* NSMutableDictionary is declared HERE, not in a header of its own */
#import <Foundation/NSString.h>

/* THE REGISTRY. A fixed array rather than a growable one, because registration happens during start-up
 * (Apple says so: "you should call +registerClass: before starting any URL loading") and a bound that
 * cannot grow mid-flight cannot fail mid-flight. The cap is checked rather than assumed — +registerClass:
 * answers NO when it is full, which is the same answer it gives for anything it will not register. */
#define FN_URLPROTOCOL_MAX_CLASSES	16

static Class fn_urlprotocol_classes[FN_URLPROTOCOL_MAX_CLASSES];
static int fn_urlprotocol_class_count = 0;

/* THE REQUEST-PROPERTY TABLE, AND ITS KEY IS RETAINED. The table is keyed by the request's IDENTITY — a
 * request is an immutable value, so identity is the only handle a caller can name twice. RETENTION IS
 * LOAD-BEARING RATHER THAN TIDY: without it, a request that is freed leaves an address in the table, a
 * later request can be allocated at that same address, and it would answer the DEAD request's properties
 * — a use-after-free that presents as a wrong answer rather than as a crash, which is the worse kind. The
 * cap is the same shape as the registry's: a fixed array, checked, with an explicit "no more" answer. */
#define FN_URLPROTOCOL_MAX_PROPERTY_ENTRIES	32

static NSURLRequest *fn_urlprotocol_property_requests[FN_URLPROTOCOL_MAX_PROPERTY_ENTRIES];
static NSMutableDictionary *fn_urlprotocol_properties[FN_URLPROTOCOL_MAX_PROPERTY_ENTRIES];
static int fn_urlprotocol_property_count = 0;

/* POINTER EQUALITY, deliberately: `==` and not -isEqual:, because two EQUAL requests are two different
 * requests and a property attached to one must not appear on the other. */
static int fn_urlprotocol_property_slot(NSURLRequest *request)
{
	int i;

	for (i = 0; i < fn_urlprotocol_property_count; i++) {
		if (fn_urlprotocol_property_requests[i] == request) {
			return i;
		}
	}
	return -1;
}

@implementation NSURLProtocol

/* --- THE OVERRIDE POINTS, AND THEIR DEFAULTS ARE THE ABSTRACT BASE'S WHOLE BEHAVIOUR --------------- */

/* THE BASE CLAIMS NOTHING: a registry of override points is only meaningful if the base itself answers
 * NO, and a subclass that forgets to override this is then silently never consulted rather than
 * accidentally claiming every request. */
+ (BOOL)canInitWithRequest:(NSURLRequest *)request
{
	return NO;
}

+ (NSURLRequest *)canonicalRequestForRequest:(NSURLRequest *)request
{
	return request;
}

/* VALUE EQUALITY IS THE HONEST DEFAULT, and it has something to mean because NSURLRequest implements
 * -isEqual:. A protocol with a looser notion (ignoring a fragment, say) overrides this. */
+ (BOOL)requestIsCacheEquivalent:(NSURLRequest *)a toRequest:(NSURLRequest *)b
{
	return [a isEqual:b];
}

/* --- THE REQUEST-PROPERTY TABLE ------------------------------------------------------------------- */

+ (id)propertyForKey:(NSString *)key inRequest:(NSURLRequest *)request
{
	int slot = fn_urlprotocol_property_slot(request);

	if (slot < 0) {
		return nil;
	}
	return [fn_urlprotocol_properties[slot] objectForKey:key];
}

+ (void)setProperty:(id)value forKey:(NSString *)key inRequest:(NSMutableURLRequest *)request
{
	int slot = fn_urlprotocol_property_slot(request);

	if (slot < 0) {
		if (fn_urlprotocol_property_count >= FN_URLPROTOCOL_MAX_PROPERTY_ENTRIES) {
			return;		/* out of slots: refusing is better than evicting somebody's property */
		}
		slot = fn_urlprotocol_property_count++;
		fn_urlprotocol_property_requests[slot] = [request retain];
		fn_urlprotocol_properties[slot] = [[NSMutableDictionary alloc] init];
	}
	/* A NIL VALUE REMOVES, which is Apple's documented reading of this door and the reason
	 * -removePropertyForKey: is not the only way out. */
	if (value == nil) {
		[fn_urlprotocol_properties[slot] removeObjectForKey:key];
	} else {
		[fn_urlprotocol_properties[slot] setObject:value forKey:key];
	}
}

+ (void)removePropertyForKey:(NSString *)key inRequest:(NSMutableURLRequest *)request
{
	int slot = fn_urlprotocol_property_slot(request);

	if (slot >= 0) {
		[fn_urlprotocol_properties[slot] removeObjectForKey:key];
	}
}

/* --- THE REGISTRY --------------------------------------------------------------------------------- */

/* ONLY A SUBCLASS REGISTERS, and the base refuses ITSELF by name: if NSURLProtocol could be registered,
 * a registry could contain a class whose every override point is the default, which would shadow every
 * real protocol behind it. */
+ (BOOL)registerClass:(Class)protocolClass
{
	if (protocolClass == nil || protocolClass == [NSURLProtocol class] ||
	    ![protocolClass isSubclassOfClass:[NSURLProtocol class]]) {
		return NO;
	}
	if (fn_urlprotocol_class_count >= FN_URLPROTOCOL_MAX_CLASSES) {
		return NO;
	}
	fn_urlprotocol_classes[fn_urlprotocol_class_count++] = protocolClass;
	return YES;
}

+ (void)unregisterClass:(Class)protocolClass
{
	int i, j = 0;

	/* COMPACTED RATHER THAN TOMBSTONED: an unregistered class must not be consultable, and a hole that
	 * the consultation loop skips is a hole that a later registration can silently fill in the wrong
	 * order. (A `nil` test would also make re-registering a freed class pointer impossible to see.) */
	for (i = 0; i < fn_urlprotocol_class_count; i++) {
		if (fn_urlprotocol_classes[i] != protocolClass) {
			fn_urlprotocol_classes[j++] = fn_urlprotocol_classes[i];
		}
	}
	fn_urlprotocol_class_count = j;
}

+ (Class)fnProtocolClassForRequest:(NSURLRequest *)request
{
	int i;

	/* MOST-RECENTLY-REGISTERED FIRST, walked backwards, so the last word belongs to the most recent
	 * caller and a protocol installed later can deliberately take precedence over a shipped one. */
	for (i = fn_urlprotocol_class_count - 1; i >= 0; i--) {
		if ([fn_urlprotocol_classes[i] canInitWithRequest:request]) {
			return fn_urlprotocol_classes[i];
		}
	}
	return Nil;
}

/* --- THE INSTANCE --------------------------------------------------------------------------------- */

- (instancetype)initWithRequest:(NSURLRequest *)request
		 cachedResponse:(NSCachedURLResponse *)cachedResponse
			 client:(id <NSURLProtocolClient>)client
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_request = [request copy];
	_cachedResponse = [cachedResponse copy];
	_client = [client retain];
	return self;
}

- (NSURLRequest *)request
{
	return _request;
}

- (NSCachedURLResponse *)cachedResponse
{
	return _cachedResponse;
}

- (id <NSURLProtocolClient>)client
{
	return _client;
}

/* NOTHING TO DO IN THE BASE, and that is the point rather than an unfinished method: a protocol that
 * implements neither has nothing to start, and calling these must not be a programming error in a
 * subclass that only reads its properties. Both are asserted to be callable no-ops. */
- (void)startLoading
{
}

- (void)stopLoading
{
}

- (void)dealloc
{
	[_request release];
	[_cachedResponse release];
	[_client release];
	[super dealloc];
}

@end
