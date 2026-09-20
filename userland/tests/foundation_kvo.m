/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_kvo, unit of 1 — F13.9's acceptance for the observer registry.
 * docs/design/foundation-plan.md §10.
 *
 * ONE unit, importing only <foundation/Foundation.h> (which proves the umbrella exports the new
 * header). The observer and the observed object are both private to this file.
 *
 * WHAT IT MEASURES, and every detail carries its numbers rather than a description:
 *   kvo-notifies-on-a-kvc-write  -setValue:forKey: reaches the observer with key path, object and
 *                                the NEW value;
 *   kvo-old-and-new-cross-the-change  the OLD and NEW values around one change, which is the pair
 *                                the will/did split exists to produce;
 *   kvo-context-comes-back-untouched  the context pointer an observer registered with;
 *   kvo-initial-option           the callback arrives DURING -addObserver: with the current value;
 *   kvo-prior-option             a .Prior observer is called TWICE per change, the first time with
 *                                the notification-is-prior flag;
 *   kvo-manual-pair-notifies     -willChangeValueForKey:/-didChangeValueForKey: around a DIRECT
 *                                setter, which is how a runtime-invisible change is announced;
 *   kvo-remove-stops-it          no further callbacks after -removeObserver:forKeyPath:;
 *   kvo-observation-info-round-trips  Cocoa's own per-object storage doors.
 */

#import <foundation/Foundation.h>

#include <stdio.h>

/* THE OBSERVED OBJECT: plain accessors, which is what KVC resolves and what -valueForKey: reads. */
@interface KVOThing : NSObject
{
	NSInteger _amount;
	NSString *_name;
}
- (NSInteger)amount;
- (void)setAmount:(NSInteger)amount;
- (NSString *)name;
- (void)setName:(NSString *)name;
@end

@implementation KVOThing

- (NSInteger)amount { return _amount; }
- (void)setAmount:(NSInteger)amount { _amount = amount; }
- (NSString *)name { return _name; }
- (void)setName:(NSString *)name { _name = name; }

@end

/* THE OBSERVER: it records what it was told, so the checks can assert on it. */
@interface KVOProbe : NSObject
{
@public
	NSUInteger calls;
	NSUInteger priorCalls;
	NSString *lastKeyPath;
	id lastObject;
	id lastNew;
	id lastOld;
	void *lastContext;
}
@end

@implementation KVOProbe

- (void)observeValueForKeyPath:(nullable NSString *)keyPath
		      ofObject:(nullable id)object
			change:(nullable NSDictionary *)change
		       context:(nullable void *)context
{
	calls++;
	lastKeyPath = keyPath;
	lastObject = object;
	lastNew = [change objectForKey:NSKeyValueChangeNewKey];
	lastOld = [change objectForKey:NSKeyValueChangeOldKey];
	lastContext = context;
	if ([[change objectForKey:NSKeyValueChangeNotificationIsPriorKey] boolValue]) {
		priorCalls++;
	}
}

@end

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-KVO %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-KVO %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

int main(void)
{
	{
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];

		[thing setValue:@7 forKey:@"amount"];
		[thing addObserver:probe forKeyPath:@"amount"
			   options:NSKeyValueObservingOptionNew context:NULL];
		[thing setValue:@9 forKey:@"amount"];
		check("kvo-notifies-on-a-kvc-write",
		      probe->calls == 1 && [probe->lastKeyPath isEqualToString:@"amount"] &&
		      probe->lastObject == thing && [probe->lastNew integerValue] == 9,
		      [NSString stringWithFormat:@"calls=%lu keyPath=%@ new=%@",
			(unsigned long)probe->calls, probe->lastKeyPath, probe->lastNew]);
		[thing removeObserver:probe forKeyPath:@"amount"];
	}

	{
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];

		[thing setValue:@7 forKey:@"amount"];
		[thing addObserver:probe forKeyPath:@"amount"
			   options:(NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld)
			   context:NULL];
		[thing setValue:@9 forKey:@"amount"];
		check("kvo-old-and-new-cross-the-change",
		      probe->calls == 1 && [probe->lastOld integerValue] == 7 &&
		      [probe->lastNew integerValue] == 9,
		      [NSString stringWithFormat:@"calls=%lu old=%@ new=%@",
			(unsigned long)probe->calls, probe->lastOld, probe->lastNew]);
		[thing removeObserver:probe forKeyPath:@"amount"];
	}

	{
		static int marker = 0;
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];

		[thing addObserver:probe forKeyPath:@"amount"
			   options:NSKeyValueObservingOptionNew context:&marker];
		[thing setValue:@1 forKey:@"amount"];
		check("kvo-context-comes-back-untouched",
		      probe->calls == 1 && probe->lastContext == (void *)&marker &&
		      probe->lastContext != NULL,
		      [NSString stringWithFormat:@"calls=%lu context=%p want=%p",
			(unsigned long)probe->calls, probe->lastContext, (void *)&marker]);
		[thing removeObserver:probe forKeyPath:@"amount"];
	}

	{
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];

		/* THE INITIAL NOTIFICATION ARRIVES DURING THIS CALL, before it returns. */
		[thing setValue:@42 forKey:@"amount"];
		[thing addObserver:probe forKeyPath:@"amount"
			   options:(NSKeyValueObservingOptionNew | NSKeyValueObservingOptionInitial)
			   context:NULL];
		check("kvo-initial-option",
		      probe->calls == 1 && [probe->lastNew integerValue] == 42,
		      [NSString stringWithFormat:@"calls=%lu new=%@",
			(unsigned long)probe->calls, probe->lastNew]);
		[thing removeObserver:probe forKeyPath:@"amount"];
	}

	{
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];

		[thing setValue:@1 forKey:@"amount"];
		[thing addObserver:probe forKeyPath:@"amount"
			   options:(NSKeyValueObservingOptionOld | NSKeyValueObservingOptionPrior)
			   context:NULL];
		[thing setValue:@2 forKey:@"amount"];
		/* TWICE: the prior notification in -willChange, then the change in -didChange. */
		check("kvo-prior-option",
		      probe->calls == 2 && probe->priorCalls == 1 &&
		      [probe->lastNew isEqual:[NSNull null]] == NO,
		      [NSString stringWithFormat:@"calls=%lu prior=%lu",
			(unsigned long)probe->calls, (unsigned long)probe->priorCalls]);
		[thing removeObserver:probe forKeyPath:@"amount"];
	}

	{
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];

		[thing addObserver:probe forKeyPath:@"name"
			   options:(NSKeyValueObservingOptionNew | NSKeyValueObservingOptionOld)
			   context:NULL];
		/* A DIRECT SETTER, announced by hand — the pair the runtime cannot see by itself. The OLD
		 * value is NSNull, not nil: a change dictionary says "there was none" the way a collection
		 * says "no value", which is the same rule the whole library follows. */
		[thing willChangeValueForKey:@"name"];
		[thing setName:@"after"];
		[thing didChangeValueForKey:@"name"];
		check("kvo-manual-pair-notifies",
		      probe->calls == 1 && [probe->lastNew isEqualToString:@"after"] &&
		      probe->lastOld != nil && [probe->lastOld isEqual:[NSNull null]],
		      [NSString stringWithFormat:@"calls=%lu new=%@ old=%@",
			(unsigned long)probe->calls, probe->lastNew, probe->lastOld]);
		[thing removeObserver:probe forKeyPath:@"name"];
	}

	{
		KVOThing *thing = [[KVOThing alloc] init];
		KVOProbe *probe = [[KVOProbe alloc] init];
		NSUInteger afterRemove;

		[thing addObserver:probe forKeyPath:@"amount"
			   options:NSKeyValueObservingOptionNew context:NULL];
		[thing setValue:@1 forKey:@"amount"];
		[thing removeObserver:probe forKeyPath:@"amount"];
		afterRemove = probe->calls;
		[thing setValue:@2 forKey:@"amount"];
		check("kvo-remove-stops-it",
		      afterRemove == 1 && probe->calls == 1,
		      [NSString stringWithFormat:@"beforeRemove=%lu afterRemove=%lu",
			(unsigned long)afterRemove, (unsigned long)probe->calls]);
	}

	{
		KVOThing *thing = [[KVOThing alloc] init];
		static char payload[4];
		void *back;

		[thing setObservationInfo:(void *)payload];
		back = [thing observationInfo];
		check("kvo-observation-info-round-trips",
		      back == (void *)payload,
		      [NSString stringWithFormat:@"info=%p want=%p", back, (void *)payload]);
	}

	printf("FOUNDATION-KVO RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output. After a probe the console can stop
	 * serving INPUT for a while (the tier residual), so an `echo $?` that the harness
	 * types may never run. The probe knows exactly what it returns, so it says so - and
	 * this is the same value: failc ? 1 : 0 is the return statement below. */
	printf("FOUNDATION-KVO-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-KVO DONE\n");
	return failc ? 1 : 0;
}
