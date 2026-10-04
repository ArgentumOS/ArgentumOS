/*
 * NSFilePresenter.h — THE PRESENTER SIDE OF THE COORDINATOR FAMILY (W8 slice 7c).
 *
 * APPLE'S ABSTRACT: "The interface a file coordinator uses to inform an object presenting a file about
 * changes to that file made elsewhere in the system." Its published surface is 24 members, measured from
 * the protocol's page, and the split this header makes is the one the pages themselves make:
 *
 *   REQUIRED, AND THE COORDINATOR CANNOT WORK WITHOUT THEM (Apple: "File presenters must implement this
 *   property") - -presentedItemURL and -presentedItemOperationQueue. The second is not decoration: "As
 *   other objects and processes interact with the presented item, the system queues relevant messages for
 *   this presenter object on the operation queue in this property", which is why the handshake below is
 *   the one place in this family where a wrong reading means a HANG rather than a wrong answer.
 *
 *   OPTIONAL, AND THE ONES THIS IMPLEMENTATION ACTUALLY CAUSES - the two relinquish forms, the changes
 *   and move/delete notifications. These are the events an in-process coordinator can bring about, so
 *   they are the ones a presenter here will see.
 *
 *   OPTIONAL, AND REGISTERED RATHER THAN DECLARED-AND-FORGOTTEN (slice 7d): the ubiquity pair, the version
 *   trio and their subitem forms, and -accommodatePresentedItemEvictionWithCompletionHandler:. iCloud,
 *   file providers, conflict versions and APFS eviction are things this system does not have, so those
 *   events can never arrive - and a protocol member whose event cannot arrive reports nothing.
 *
 * THE HANDSHAKE, MEASURED FROM THE RELINQUISH PAGES BEFORE IT WAS BUILT, because it is the part with a
 * hang in it: "After taking any appropriate steps, you MUST EXECUTE THE BLOCK IN THE [reacquirer] PARAMETER"
 * - so the parameter is a block taking a RECACQUIRER block ("it executes the reader block and passes in yet
 * another block for the reader to execute when it is done"); "Your implementation of this method is
 * executed using the queue in the [presentedItemOperationQueue] property"; and "Your reacquirer block is
 * executed on the queue associated with the reader" - so the coordinator WAITS for the presenter's call
 * before running its accessor, and calls the reacquirer afterwards. Both halves are asserted by the probe.
 */

#ifndef FOUNDATION_NSFILEPRESENTER_H
#define FOUNDATION_NSFILEPRESENTER_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSError;
@class NSOperationQueue;
@class NSSet;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

@protocol NSFilePresenter <NSObject>

/* REQUIRED. "File presenters must implement this property and use it to return the file or directory of
 * interest", and its accessor "must be thread safe". */
@property (nullable, readonly, copy) NSURL *presentedItemURL;

/* REQUIRED. "The operation queue in which to execute presenter-related messages." */
@property (readonly, strong) NSOperationQueue *presentedItemOperationQueue;

@optional

/* THE PRIMARY PRESENTED ITEM (§63.236, the tail of slice 7c): for a presented item that IS a package, the URL
 * of the package's own main item — the door a presenter answers when its presented item is a subitem of one.
 * Apple declares it OPTIONAL, so it lives here beside the other presenter-supplied answers. */
@property (nullable, readonly, copy) NSURL *primaryPresentedItemURL;

/* STEPPING ASIDE: the two forms the coordinator's doors cause, and the reacquirer that comes back. */
- (void)relinquishPresentedItemToReader:(void (^)(void (^ _Nullable reacquirer)(void)))reader;
- (void)relinquishPresentedItemToWriter:(void (^)(void (^ _Nullable reacquirer)(void)))writer;

/* THE CHANGES A COORDINATED OPERATION MAKES VISIBLE. */
- (void)savePresentedItemChangesWithCompletionHandler:(void (^)(NSError * _Nullable errorOrNil))completionHandler;
- (void)accommodatePresentedItemDeletionWithCompletionHandler:(void (^)(NSError * _Nullable errorOrNil))completionHandler;
- (void)presentedItemDidMoveToURL:(NSURL *)newURL;
- (void)presentedItemDidChange;

/* THE DIRECTORY HALF: subitems a presented directory can see appear, change, move or vanish. */
- (void)accommodatePresentedSubitemDeletionAtURL:(NSURL *)url
			       completionHandler:(void (^)(NSError * _Nullable errorOrNil))completionHandler;
- (void)presentedSubitemDidAppearAtURL:(NSURL *)url;
- (void)presentedSubitemAtURL:(NSURL *)oldURL didMoveToURL:(NSURL *)newURL;
- (void)presentedSubitemDidChangeAtURL:(NSURL *)url;

/* REGISTERED ABSENCES (slice 7d), DECLARED HERE SO THAT A CONFORMING CLASS COMPILES AND SO THAT THE
 * BOUNDARY IS VISIBLE WHERE A PRESENTER IS WRITTEN - see the header's opening note. */
- (NSArray *)observedPresentedItemUbiquityAttributes;
- (void)presentedItemDidChangeUbiquityAttributes:(NSSet *)attributes;
- (void)presentedItemDidGainVersion:(id)version;
- (void)presentedItemDidLoseVersion:(id)version;
- (void)presentedItemDidResolveConflictVersion:(id)version;
- (void)presentedSubitemAtURL:(NSURL *)url didGainVersion:(id)version;
- (void)presentedSubitemAtURL:(NSURL *)url didLoseVersion:(id)version;
- (void)presentedSubitemAtURL:(NSURL *)url didResolveConflictVersion:(id)version;
- (void)accommodatePresentedItemEvictionWithCompletionHandler:(void (^)(NSError * _Nullable errorOrNil))completionHandler;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEPRESENTER_H */
