/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSNotificationCenter — the registry and the delivery (W4).
 *
 * A REGISTRATION IS A FILTER PAIR. `name` and `object` may each be nil, and nil means "this criterion
 * is not used" — so name-only receives every notification of that name from anyone, object-only
 * receives every notification from that sender, and both-nil receives everything. THE POSTER DOES NOT
 * CHOOSE ITS AUDIENCE; the filters do, which is why -postNotification: takes a whole notification.
 *
 * DELIVERY IS SYNCHRONOUS, on the posting thread (Apple's contract, and the reason the block form
 * takes a queue: a queue is how a caller asks for ELSEWHERE). The registry is snapshotted under the
 * lock and delivery happens outside it, so an observer may register or remove itself while it runs —
 * which is exactly what the block form's "remove me from inside my own block" pattern needs.
 *
 * OBSERVERS ARE HELD AS ZEROING WEAK REFERENCES (objc_storeWeak/objc_loadWeak, which the runtime
 * documents as zeroing: "if obj has begun deallocation, then this stores nil"). So a centre does NOT
 * keep its observers alive, and an observer that is deallocated without removing itself is SKIPPED
 * rather than messaged — the modern contract, and the difference between a stale registration and a
 * crash. The OBJECT FILTER is weak for the same reason: a sender that is gone cannot post again.
 */

#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSOperation.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSException.h>
/* THE WEAK API LIVES HERE (objc_storeWeak/objc_loadWeak, documented as zeroing), not in
 * <objc/runtime.h>. */
#include <objc/objc-arc.h>
#include <pthread.h>
#include <stdlib.h>

/* THE BLOCKS RUNTIME, declared where it is used (see NSUndoManager.m: <Block.h> is staged nowhere,
 * and libobjc2 exports both symbols). A registered block OUTLIVES the call that made it, so it must be
 * copied. */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

/*
 * A PRIVATE BLOCK OPERATION, the adapter the queue form needs: an NSOperation whose -main runs a block,
 * which it COPIED (a block literal is a stack object and this one outlives the call that made it), and
 * which it retains the notification through, because the delivery outlives the post.
 */
@interface FnBlockOperation : NSOperation
{
	void (^_body)(void);
}
- (instancetype)initWithBlock:(void (^)(void))body;
@end

@implementation FnBlockOperation

- (instancetype)initWithBlock:(void (^)(void))body
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_body = body != nil ? _Block_copy(body) : nil;
	return self;
}

- (void)main
{
	if (_body != nil) {
		_body();
	}
}

- (void)dealloc
{
	if (_body != nil) {
		_Block_release(_body);
	}
	[super dealloc];
}

@end

/* ONE REGISTRATION. EITHER a selector or a block, never both; the filters may each be nil. */
@interface FnNotificationObserver : NSObject
{
	id _observer;			/* ZEROING WEAK: the centre does not keep it alive */
	SEL _selector;
	void (^_block)(NSNotification *);	/* COPIED */
	NSString *_name;		/* copied, or nil for "any name" */
	id _object;			/* ZEROING WEAK, or nil for "any sender" */
	NSOperationQueue *_queue;	/* retained when the block form asked for one */
}
- (instancetype)initWithObserver:(id)observer
			selector:(SEL)selector
			   block:(nullable void (^)(NSNotification *))block
			    name:(nullable NSString *)name
			  object:(nullable id)object
			   queue:(nullable NSOperationQueue *)queue;
- (nullable id)observer;
- (nullable NSOperationQueue *)queue;
- (BOOL)matchesName:(nullable NSString *)name object:(nullable id)object;
- (void)deliver:(NSNotification *)notification;
@end

@implementation FnNotificationObserver

- (instancetype)initWithObserver:(id)observer
			selector:(SEL)selector
			   block:(nullable void (^)(NSNotification *))block
			    name:(nullable NSString *)name
			  object:(nullable id)object
			   queue:(nullable NSOperationQueue *)queue
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* THE WEAK STORES GO THROUGH THE IVAR'S ADDRESS: that is what makes the runtime able to nil it
	 * when the object goes away, which is the whole contract. */
	objc_storeWeak(&_observer, observer);
	objc_storeWeak(&_object, object);
	_selector = selector;
	_block = block != nil ? _Block_copy(block) : nil;
	_name = [name copy];
	_queue = [queue retain];
	return self;
}

- (id)observer
{
	return objc_loadWeak(&_observer);
}

- (NSOperationQueue *)queue
{
	return _queue;
}

/* BOTH CRITERIA, EACH ONE OPTIONAL ON ITS OWN SIDE. A nil `name` in the registration means "any name";
 * a nil in the POST is the same, so a registration with a name never matches a nameless post. */
- (BOOL)matchesName:(nullable NSString *)name object:(nullable id)object
{
	if (_name != nil && (name == nil || ![_name isEqualToString:name])) {
		return NO;
	}
	if (objc_loadWeak(&_object) != nil && objc_loadWeak(&_object) != object) {
		return NO;
	}
	return YES;
}

- (void)deliver:(NSNotification *)notification
{
	if (_block != nil) {
		if (_queue != nil) {
			/* ASKED FOR ELSEWHERE: one operation holding the block and the notification, handed to the
			 * queue, which is where the block runs. THE ADAPTER EXISTS BECAUSE THE CONVENIENCE DOES
			 * NOT: -addOperationWithBlock: is named as not-yet-shipped in NSOperation.h (it belongs
			 * with the block operations, W17), and this family must not widen into that one to deliver
			 * its own documented semantics — so a private block operation is the seam. */
			FnBlockOperation *operation = [[FnBlockOperation alloc] initWithBlock:^(void) {
				_block(notification);
			}];

			[_queue addOperation:operation];
			[operation release];
			return;
		}
		_block(notification);
		return;
	}
	if (_selector != NULL) {
		id observer = objc_loadWeak(&_observer);

		if (observer != nil) {
			[observer performSelector:_selector withObject:notification];
		}
	}
}

- (void)dealloc
{
	objc_storeWeak(&_observer, nil);
	objc_storeWeak(&_object, nil);
	if (_block != nil) {
		_Block_release(_block);
	}
	[_name release];
	[_queue release];
	[super dealloc];
}

@end

static NSNotificationCenter *fn_default_center = nil;
static pthread_once_t fn_default_once = PTHREAD_ONCE_INIT;

static void fn_make_default_center(void)
{
	fn_default_center = [[NSNotificationCenter alloc] init];
}

@implementation NSNotificationCenter

+ (NSNotificationCenter *)defaultCenter
{
	pthread_once(&fn_default_once, fn_make_default_center);
	return fn_default_center;
}

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_observers = [[NSMutableArray alloc] init];
	_lock = [[NSLock alloc] init];
	return self;
}

- (void)addObserver:(id)observer selector:(SEL)selector name:(nullable NSString *)name object:(nullable id)object
{
	FnNotificationObserver *record;

	if (observer == nil || selector == NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSNotificationCenter addObserver:selector:name:object:] needs an observer and a selector"];
		return;
	}
	record = [[FnNotificationObserver alloc] initWithObserver:observer selector:selector block:nil
							     name:name object:object queue:nil];
	[_lock lock];
	[_observers addObject:record];
	[_lock unlock];
	[record release];
}

- (id)addObserverForName:(nullable NSString *)name
		  object:(nullable id)object
		   queue:(nullable NSOperationQueue *)queue
	      usingBlock:(void (^)(NSNotification *notification))block
{
	FnNotificationObserver *record;

	if (block == nil) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSNotificationCenter addObserverForName:object:queue:usingBlock:] needs a block"];
		return nil;
	}
	record = [[FnNotificationObserver alloc] initWithObserver:nil selector:NULL block:block
							     name:name object:object queue:queue];
	[_lock lock];
	[_observers addObject:record];
	[_lock unlock];
	/* THE TOKEN IS THE RECORD ITSELF, and it is what -removeObserver: takes: an opaque object, which
	 * is exactly what Apple documents the return value to be. The centre owns it (the array retains
	 * it) and the caller owns this reference to it, so the caller may release the token at once and
	 * still remove the observation later by handing back the same pointer. */
	return [record autorelease];
}

- (void)removeObserver:(id)observer
{
	NSMutableArray *survivors;

	if (observer == nil) {
		return;
	}
	[_lock lock];
	survivors = [[NSMutableArray alloc] init];
	for (FnNotificationObserver *record in _observers) {
		BOOL mine = [record observer] == observer || record == observer;

		if (!mine) {
			[survivors addObject:record];
		}
	}
	[_observers setArray:survivors];
	[survivors release];
	[_lock unlock];
}

- (void)removeObserver:(id)observer name:(nullable NSString *)name object:(nullable id)object
{
	NSMutableArray *survivors;

	if (observer == nil) {
		return;
	}
	[_lock lock];
	survivors = [[NSMutableArray alloc] init];
	for (FnNotificationObserver *record in _observers) {
		BOOL mine = ([record observer] == observer || record == observer) &&
			    [record matchesName:name object:object];

		if (!mine) {
			[survivors addObject:record];
		}
	}
	[_observers setArray:survivors];
	[survivors release];
	[_lock unlock];
}

/* THE DELIVERY ITSELF, and the snapshot is what makes it safe for an observer to change the registry
 * while it runs. EVERYTHING IS DELIVERED OUTSIDE THE LOCK: a selector that registers or removes would
 * otherwise deadlock against the lock its own post is holding. */
- (void)postNotification:(NSNotification *)notification
{
	NSArray *snapshot;

	if (notification == nil) {
		return;
	}
	[_lock lock];
	snapshot = [[NSArray alloc] initWithArray:_observers];
	[_lock unlock];
	for (FnNotificationObserver *record in snapshot) {
		if ([record matchesName:[notification name] object:[notification object]]) {
			[record deliver:notification];
		}
	}
	[snapshot release];
}

- (void)postNotificationName:(NSString *)name object:(nullable id)object
{
	[self postNotification:[NSNotification notificationWithName:name object:object]];
}

- (void)postNotificationName:(NSString *)name object:(nullable id)object userInfo:(nullable NSDictionary *)userInfo
{
	[self postNotification:[NSNotification notificationWithName:name object:object userInfo:userInfo]];
}

- (void)dealloc
{
	[_observers release];
	[_lock release];
	[super dealloc];
}

@end
