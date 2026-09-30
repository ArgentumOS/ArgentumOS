/*
 * FNObjectDeath.h — THE ONE PLACE AN OBJECT'S LIFE ENDS, AS A SEAM (§63.21).
 *
 * WHAT THIS IS FOR. Several subsystems in this library keep a table keyed by the IDENTITY of somebody
 * else's objects — the KVO registry is the one that needed this file (§63.21), and `-setObservationInfo:`
 * rides in the same table. Apple gets the cleanup for free because its table is PER-OBJECT and dies with
 * the object; OUR TABLES ARE PROCESS-GLOBAL, so they have to be TOLD. Without a death signal an entry
 * outlives its object: the entry itself leaks, and — worse, because the lookup is by pointer — a NEW
 * object allocated at the old address inherits the dead object's registrations.
 *
 * WHO CALLS IT: `-[NSObject dealloc]`, which is the single point every object in this library passes
 * through (the root class is where the allocation goes away: the runtime sends `-dealloc` and does not
 * free afterwards, so `object_dispose(self)` there IS the free). The hook runs BEFORE that dispose, while
 * the address is still the object's, because the address is all a subscriber needs to compare against.
 *
 * WHO INSTALLS IT: a subscriber, once, when it first creates a table — deliberately NOT at `+load` time,
 * so a program that never uses the subsystem pays one pointer test per dealloc and there is no load-order
 * question to get wrong. KVO installs it in `fn_kvo_init()`.
 *
 * THE CONTRACT A SUBSCRIBER MUST KEEP: the hook is called for EVERY object that dies, including objects
 * the subscriber has never heard of, and the object is mid-death — a hook must not resurrect it, must not
 * rely on any of its state beyond its ADDRESS, and must return promptly. Exactly one hook is supported;
 * a second subscriber would have to chain, which is why the slot is a documented single-consumer seam
 * rather than a general notification.
 */
#ifndef FOUNDATION_FNOBJECTDEATH_H
#define FOUNDATION_FNOBJECTDEATH_H

#include <Foundation/NSObject.h>

/* `object` is the dying object. NULL until a subsystem installs one. */
typedef void (*FNObjectDeathHook)(NSObject *object);

/* DEFINED in NSObject.m, where it is called, so the call site is never a null symbol. */
extern FNObjectDeathHook fn_object_death_hook;

#endif /* FOUNDATION_FNOBJECTDEATH_H */
