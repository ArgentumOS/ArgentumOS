/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSProxy.h — a stand-in for another object, and the second ROOT class (W2h, §14).
 *
 * IT IS A ROOT CLASS, so it inherits nothing: the methods a first-class object owes are declared here
 * and provided by the runtime (a `+alloc` of its own, `-dealloc` that frees the instance), and
 * everything ELSE is forwarded to the object the proxy stands for. That is the point of the class and
 * the reason `-forwardInvocation:` exists: a proxy answers messages it does not implement by
 * forwarding them, so `-respondsToSelector:`, `-isKindOfClass:` and the rest reach the REAL object
 * through a subclass's forwarding rather than through any implementation here.
 *
 * THE TWO FORWARDING METHODS DEFAULT TO RAISING, which is Apple's contract and the honest one: a
 * proxy that has not been told what it stands for must not invent a signature, so
 * `-methodSignatureForSelector:` answers with an exception rather than a guess.
 *
 * THE TWO DEPRECATED DOORS SHIP (§63.205, user decision `dec-1a00739ebd0bdc05` — a deprecated API is OWED
 * work, not a strike, so §11.5 governs the CG/AppKit duplication only). `+allocWithZone:` takes a zone this
 * runtime has none of (there is no NSZone here), so it is `+alloc` with the reading stated; `-finalize`
 * belongs to the collector, which does not exist here either, so it is defined for compatibility — source
 * implementing it compiles and links — and is never called.
 *
 * WHAT IS NOT, named: `-allowsWeakReference`/`-retainWeakReference` (marked UNAVAILABLE in the modern SDK).
 * Unavailable is not deprecated: Apple removed those two doors with the collector, so they are a §11-style
 * absence with this citation rather than work, and their ledger rows stay open carrying it.
 */
#ifndef FOUNDATION_NSPROXY_H
#define FOUNDATION_NSPROXY_H

#import <Foundation/NSObject.h>

@class NSMethodSignature;
@class NSInvocation;

NS_ASSUME_NONNULL_BEGIN

__attribute__((objc_root_class))
@interface NSProxy <NSObject>
{
	Class isa;
}

+ (instancetype)alloc;
+ (Class)class;
+ (BOOL)respondsToSelector:(SEL)aSelector;

- (void)forwardInvocation:(NSInvocation *)invocation;
- (nullable NSMethodSignature *)methodSignatureForSelector:(SEL)selector;
- (void)dealloc;

@property (readonly, copy) NSString *description;
@property (readonly, copy) NSString *debugDescription;


+ (instancetype)allocWithZone:(nullable void *)zone;
- (void)finalize;
@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSPROXY_H */
