/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSDistributedNotificationCenter.m — the vocabulary's constants AND the center itself (§62.80). MANUAL OWNERSHIP.
 *
 * THE ENGINE IS LOCAL AND IT IS REAL: a registry of observing records that each carry a SUSPENSION BEHAVIOUR, and
 * the three states a suspended center can be in for each of them — dropped on arrival, held until the resume, or
 * coalesced to one per name-and-object — plus the behaviour that asks to be delivered ANYWAY. Nothing here sends
 * anything anywhere else; see the header for the bus this system does not have.
 */

#import <Foundation/NSDistributedNotificationCenter.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSString.h>

/* Every name answers its own name: the convention NSNotification.h records, applied to this family. */
NSDistributedNotificationCenterType const NSLocalNotificationCenterType = @"NSLocalNotificationCenterType";
NSString *const NSNotificationDeliverImmediately = @"NSNotificationDeliverImmediately";
NSString *const NSNotificationPostToAllSessions = @"NSNotificationPostToAllSessions";

/* ONE OBSERVING RECORD, as a small object rather than a dictionary: five fields and a comparison, and a dictionary
 * would make every access a spelling. */
@interface FnDistributedObservation : NSObject
{
@public
	id observer;				/* UNRETAINED: NSNotificationCenter's own rule */
	SEL selector;
	NSString *name;				/* retained; nil matches every name */
	id object;				/* UNRETAINED: nil matches every sender */
	NSNotificationSuspensionBehavior behavior;
}
@end

@implementation FnDistributedObservation

- (void)dealloc
{
	[name release];
	[super dealloc];
}

@end

@implementation NSDistributedNotificationCenter

+ (NSDistributedNotificationCenter *)defaultCenter
{
	static NSDistributedNotificationCenter *shared = nil;

	if (shared == nil) {
		shared = [[NSDistributedNotificationCenter alloc] init];
	}
	return shared;
}

+ (NSDistributedNotificationCenter *)notificationCenterForType:(NSDistributedNotificationCenterType)centerType
{
	/* THE ONE TYPE THIS SYSTEM HAS, AND THE REFUSAL FOR EVERY OTHER: a distributed center would need a bus, and
	 * naming one here would be naming something that does not exist. */
	if ([centerType isEqualToString:NSLocalNotificationCenterType]) {
		return [self defaultCenter];
	}
	[NSException raise:NSInvalidArgumentException
		    format:@"+[NSDistributedNotificationCenter notificationCenterForType:] refuses \"%@\": this "
			   @"system has no distributed notification bus - the only type it has is "
			   @"NSLocalNotificationCenterType", centerType];
	return nil;
}

- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_observations = [[NSMutableArray alloc] init];
		_pending = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)dealloc
{
	[_observations release];
	[_pending release];
	[super dealloc];
}

- (void)addObserver:(id)observer
	   selector:(SEL)selector
	       name:(nullable NSString *)name
	     object:(nullable id)object
suspensionBehavior:(NSNotificationSuspensionBehavior)behavior
{
	FnDistributedObservation *record = [[FnDistributedObservation alloc] init];

	record->observer = observer;
	record->selector = selector;
	record->name = name != nil ? [[NSString alloc] initWithString:name] : nil;
	record->object = object;
	record->behavior = behavior;
	[_observations addObject:record];
	[record release];
}

/* THE INHERITED THREE-ARGUMENT DOOR, REGISTERED WITH APPLE'S OWN DEFAULT FOR THIS CLASS: a notification arriving
 * while delivery is suspended is COALESCED. */
- (void)addObserver:(id)observer
	   selector:(SEL)selector
	       name:(nullable NSString *)name
	     object:(nullable id)object
{
	[self addObserver:observer
		 selector:selector
		     name:name
		   object:object
       suspensionBehavior:NSNotificationSuspensionBehaviorCoalesce];
}

- (void)removeObserver:(id)observer name:(nullable NSString *)name object:(nullable id)object
{
	NSUInteger i = 0;

	while (i < [_observations count]) {
		FnDistributedObservation *record = [_observations objectAtIndex:i];

		if (record->observer == observer &&
		    (name == nil || (record->name != nil && [record->name isEqualToString:name])) &&
		    (object == nil || record->object == object)) {
			[_observations removeObjectAtIndex:i];
			continue;
		}
		i++;
	}
}

- (void)removeObserver:(id)observer
{
	[self removeObserver:observer name:nil object:nil];
}

/* ---- POSTING ---------------------------------------------------------------------------------------- */

- (void)postNotificationName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo
	   deliverImmediately:(BOOL)deliverImmediately;
{
	NSNotification *notification = [NSNotification notificationWithName:name object:object userInfo:userInfo];
	NSUInteger i;

	for (i = 0; i < [_observations count]; i++) {
		FnDistributedObservation *record = [_observations objectAtIndex:i];

		if (record->name != nil && ![record->name isEqualToString:name]) {
			continue;
		}
		if (record->object != nil && record->object != object) {
			continue;
		}
		/* THE FOUR BEHAVIOURS, and the immediate one is the caller's request FIRST: a poster that asks for
		 * immediate delivery gets it whatever the center's state is. */
		if (deliverImmediately || record->behavior == NSNotificationSuspensionBehaviorDeliverImmediately) {
			[record->observer performSelector:record->selector withObject:notification];
			continue;
		}
		if (!_suspended || record->behavior == NSNotificationSuspensionBehaviorDeliverImmediately) {
			[record->observer performSelector:record->selector withObject:notification];
			continue;
		}
		if (record->behavior == NSNotificationSuspensionBehaviorDrop) {
			continue;
		}
		if (record->behavior == NSNotificationSuspensionBehaviorCoalesce) {
			/* ONE PER OBSERVING RECORD AND NAME: a second notification of the same name to the same record is
			 * what coalescing throws away. */
			BOOL already = NO;
			NSUInteger j;

			for (j = 0; j < [_pending count]; j++) {
				FnDistributedObservation *held = [_pending objectAtIndex:j];

				if (held->observer == record->observer && held->selector == record->selector &&
				    (held->name == nil || [held->name isEqualToString:name])) {
					already = YES;
					break;
				}
			}
			if (already) {
				continue;
			}
		}
		/* HOLD AND COALESCE BOTH KEEP IT, IN ARRIVAL ORDER. */
		{
			FnDistributedObservation *held = [[FnDistributedObservation alloc] init];

			held->observer = record->observer;
			held->selector = record->selector;
			held->name = [[NSString alloc] initWithString:name];
			held->object = object;
			held->behavior = record->behavior;
			[_pending addObject:held];
			[held release];
		}
	}
}

- (void)postNotificationName:(NSString *)name
		      object:(nullable id)object
		    userInfo:(nullable NSDictionary *)userInfo
		     options:(NSDistributedNotificationOptions)options
{
	/* THE CROSS-SESSION OPTION IS REFUSED BY NAME - this system has one session and no bus between any two of
	 * them - and the immediate option is honoured. */
	if ((options & NSDistributedNotificationPostToAllSessions) != 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSDistributedNotificationCenter "
				   @"postNotificationName:object:userInfo:options:] refuses "
				   @"NSDistributedNotificationPostToAllSessions: this system has no distributed "
				   @"notification bus"];
	}
	[self postNotificationName:name
			    object:object
			  userInfo:userInfo
		 deliverImmediately:(options & NSDistributedNotificationDeliverImmediately) != 0];
}

/* THE INHERITED POSTING DOORS ROUTE INTO THE SAME ENGINE, so the two halves of the surface cannot disagree. */
- (void)postNotification:(NSNotification *)notification
{
	[self postNotificationName:[notification name]
			    object:[notification object]
			  userInfo:[notification userInfo]
		 deliverImmediately:NO];
}

- (void)postNotificationName:(NSString *)name object:(nullable id)object
{
	[self postNotificationName:name object:object userInfo:nil deliverImmediately:NO];
}

- (void)postNotificationName:(NSString *)name object:(nullable id)object userInfo:(nullable NSDictionary *)userInfo
{
	[self postNotificationName:name object:object userInfo:userInfo deliverImmediately:NO];
}

/* ---- SUSPENSION ------------------------------------------------------------------------------------- */

- (void)setSuspended:(BOOL)suspended
{
	_suspended = suspended;
	if (!suspended) {
		/* THE RESUME FLUSHES WHAT WAS HELD, IN ARRIVAL ORDER, and the observe-during-suspension rule is stated:
		 * a record whose observer has since been removed receives nothing, because the flush asks the registry
		 * whether the observer is still there. */
		while ([_pending count] > 0) {
			FnDistributedObservation *held = [[_pending objectAtIndex:0] retain];
			NSNotification *notification = [NSNotification notificationWithName:held->name
									     object:held->object
									   userInfo:nil];
			BOOL stillObserving = NO;
			NSUInteger i;

			for (i = 0; i < [_observations count]; i++) {
				FnDistributedObservation *record = [_observations objectAtIndex:i];

				if (record->observer == held->observer && record->selector == held->selector) {
					stillObserving = YES;
					break;
				}
			}
			[_pending removeObjectAtIndex:0];
			if (stillObserving) {
				[held->observer performSelector:held->selector withObject:notification];
			}
			[held release];
		}
	}
}


/* §63.235: THE GETTER ONLY — the setter has been here all along (-setSuspended:, above), so the property was
 * owed its READ half and nothing else. The gate itself is the class's own doing. */
- (BOOL)isSuspended
{
	return _suspended;
}
@end
