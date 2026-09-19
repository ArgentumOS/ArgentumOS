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
 * WHAT IS NOT, named: `+allocWithZone:` (the zone API is out, §11.5), `-finalize` (deprecated), and
 * `-allowsWeakReference`/`-retainWeakReference` (marked unavailable in the modern SDK).
 */
#ifndef FOUNDATION_NSPROXY_H
#define FOUNDATION_NSPROXY_H

#import <foundation/NSObject.h>

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

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSPROXY_H */
