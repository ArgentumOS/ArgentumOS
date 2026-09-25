/*
 * NSFileProviderService.h — W8 slice 9: A CLASS WHOSE SUBJECT THIS SYSTEM DOES NOT HAVE, SHIPPED AS FAR
 * AS THE TRUTH GOES. docs/design/foundation-plan.md §60.
 *
 * APPLE'S ABSTRACT: "A service that provides a custom communication channel between your app and a File
 * Provider extension" - and its published surface is TWO members, measured from the class's page:
 * -name and -getFileProviderConnectionWithCompletionHandler:. The connection it hands back is an
 * NSXPCConnection, which is the second thing this system does not have: there is no file provider
 * extension machinery AND NO XPC.
 *
 * SO WHAT IS HONEST HERE, AND IT IS TWO DIFFERENT KINDS OF ANSWER:
 *
 *  * THE LOOKUP IS TRUTHFULLY EMPTY, and it is the door Apple publishes FOR LOOKING: NSFileManager's
 *    -getFileProviderServicesForItemAtURL:completionHandler: (which is why it is declared in
 *    NSFileManager's own header and implemented in its .m). With no provider extension registered for any
 *    item, the dictionary it answers is EMPTY and the handler is CALLED - the question was asked and
 *    answered rather than left hanging, exactly as NSFileVersion's nonlocal-versions door does.
 *
 *  * THE CONNECTION IS REFUSED BY NAME. -getFileProviderConnectionWithCompletionHandler: is the door that
 *    hands back "the custom communication channel"; with no XPC on this system there is no channel to hand
 *    back, so it answers NIL AND AN ERROR rather than pretending a connection exists. That refusal is the
 *    reason the class is worth declaring at all: a program can ASK, and finds out.
 *
 * AND THE ONE OBJECT A CALLER CAN ACTUALLY HOLD is one it made itself ([[NSFileProviderService alloc] init]),
 * because the lookup never returns one - so -name answers an EMPTY STRING for a service nothing named, and
 * the connection door refuses. Both are asserted by the probe, which is what keeps this class from being
 * declarations that no check can reach.
 */

#ifndef FOUNDATION_NSFILEPROVIDERSERVICE_H
#define FOUNDATION_NSFILEPROVIDERSERVICE_H

#import <Foundation/NSObject.h>

@class NSError;
@class NSXPCConnection;
@class NSString;

NS_ASSUME_NONNULL_BEGIN

/* THE CONNECTION TYPE IS FORWARD DECLARED AND NOT IMPLEMENTED, which is not a gap being hidden: the channel
 * is an NSXPCConnection, this system has no XPC, and the door below therefore never produces one. The
 * declaration exists so that a program written against Apple's API COMPILES here; a program that tries to
 * USE the object finds out from the error. */
@interface NSFileProviderService : NSObject
{
	NSString *_name;
}

/* "The name of the service" - an EMPTY STRING for a service that nothing named, because a caller can only
 * obtain one by making it. */
- (NSString *)name;

/* "Returns the file provider service's connection" - NIL AND AN ERROR here: the channel is an NSXPCConnection
 * and this system has no XPC. */
- (void)getFileProviderConnectionWithCompletionHandler:
	(void (^)(NSXPCConnection * _Nullable connection, NSError * _Nullable error))completionHandler;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEPROVIDERSERVICE_H */
