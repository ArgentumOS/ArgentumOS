/*
 * NSFileCoordinator.h — THE COORDINATOR FAMILY (W8 slice 7a): the vocabulary its operations are SPELLED
 * with, before the operations themselves.
 *
 * APPLE'S ABSTRACT FOR THE CLASS THE FAMILY IS NAMED AFTER: "An object that coordinates the reading and
 * writing of files and directories among file presenters." The published surface is LARGE - measured from
 * the two pages: NSFileCoordinator carries 13 members (two initializers' worth of one, four registered-
 * presenter doors, the synchronous accessor forms for one and two items, prepareForReadingItemsAtURLs, the
 * two will/did-move notifications, -cancel, the ubiquity notification and the two option sets) and
 * NSFilePresenter carries 24. SO THE FAMILY LANDS AS SLICES, and this one is the VOCABULARY:
 *
 *   7a  *THIS HEADER*: NSFileCoordinatorReadingOptions, NSFileCoordinatorWritingOptions, and
 *       NSFileAccessIntent - the value object that carries "the details of a coordinated-read or
 *       coordinated-write operation" (Apple's abstract) and is what the asynchronous door is driven by.
 *   7b  the SYNCHRONOUS accessor doors (reading, writing, and the two 2-item forms), which run the
 *       accessor and are where this system's semantics have to be STATED: there is no coordination
 *       DAEMON here and no file provider, so what a coordinator can honestly do is coordinate WITHIN the
 *       process - run the accessor, and inform the presenters registered in this process.
 *   7c  the registered-presenter doors (+addFilePresenter:, +removeFilePresenter:, +filePresenters,
 *       -initWithFilePresenter:, -purposeIdentifier) and the NSFilePresenter protocol's in-process core
 *       (the two relinquish forms, -savePresentedItemChanges…, -presentedItemDidChange, the move
 *       notifications), with the relinquish/completion-handler handshake measured before it is built.
 *   7d  what this system cannot bring about, REGISTERED rather than declared-and-forgotten: the ubiquity
 *       pair (-observedPresentedItemUbiquityAttributes, -presentedItemDidChangeUbiquityAttributes:), the
 *       version trio (-presentedItemDidGain/Lose/ResolveConflictVersion: and their subitem forms), and
 *       -accommodatePresentedItemEvictionWithCompletionHandler: - iCloud, file providers, conflict
 *       versions and APFS eviction are things this system does not have, and a protocol member whose
 *       event can never arrive is a member that would report nothing.
 *
 * THE TWO OPTION SETS ARE APPLE'S NAMES WITH OUR VALUES, which is §11.6.1 D2's standing rule for a
 * constant whose name is published and whose number is not; the names below are the published members,
 * measured from the two enum pages, and each set's members are distinct bits so a caller can combine them.
 */

#ifndef FOUNDATION_NSFILECOORDINATOR_H
#define FOUNDATION_NSFILECOORDINATOR_H

#import <Foundation/NSObject.h>
#import <Foundation/NSFilePresenter.h>

@class NSError;
@class NSOperationQueue;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* "Options to use when reading the contents or attributes of a file or directory" - Apple's own abstract
 * for the set. */
typedef enum {
	NSFileCoordinatorReadingWithoutChanges = 1 << 0,
	NSFileCoordinatorReadingResolvesSymbolicLink = 1 << 1,
	NSFileCoordinatorReadingImmediatelyAvailableMetadataOnly = 1 << 2,
	NSFileCoordinatorReadingForUploading = 1 << 3,
} NSFileCoordinatorReadingOptions;

/* "Options to use when changing the contents or attributes of a file or directory". */
typedef enum {
	NSFileCoordinatorWritingForDeleting = 1 << 0,
	NSFileCoordinatorWritingForMoving = 1 << 1,
	NSFileCoordinatorWritingForMerging = 1 << 2,
	NSFileCoordinatorWritingForReplacing = 1 << 3,
	NSFileCoordinatorWritingContentIndependentMetadataOnly = 1 << 4,
} NSFileCoordinatorWritingOptions;

/* ---- THE COORDINATOR ITSELF (W8 slice 7b): THE SYNCHRONOUS ACCESSOR DOORS -------------------------
 *
 * APPLE'S OWN WORDS FOR WHAT A COORDINATED OPERATION IS, measured from the doors' page: "the actual URL
 * passed to the [accessor] may be DIFFERENT than the one in this parameter ... ALWAYS USE THE URL PASSED
 * INTO THE BLOCK INSTEAD OF THE VERSION YOU PROVIDED" - so the accessor's URL is authoritative and this
 * implementation hands it the URL it actually coordinated.
 *
 * AND THE ERROR SHAPE IS THE OTHER HALF, VERBATIM: "If a file presenter encounters an error while preparing
 * for this read operation, that error is returned in this parameter and the block in the [accessor]
 * parameter IS NOT EXECUTED." So these doors return VOID and report through `outError`, and a refusal means
 * the accessor never runs - which is why every refusal here is asserted by a check that also asserts the
 * accessor did not fire.
 *
 * THE BOUNDARY, STATED AT THE DOORS BECAUSE IT IS WHAT THEY MEAN HERE (§11.6.1 is where it is registered):
 * **THIS SYSTEM HAS NO COORDINATION SERVICE AND NO FILE PROVIDER.** Apple's coordinator coordinates "among
 * file presenters" and, on its own platform, across PROCESSES through a system service. What this
 * implementation can honestly do is coordinate WITHIN THE PROCESS - run the accessor, and (7c) inform the
 * presenters registered in this process. Cross-process coordination is not silently implied: there is no
 * daemon to do it, and building one is a subsystem of its own rather than a slice of this family.
 *
 * THE OPTIONS THAT HAVE A MEANING HERE ARE HONOURED AND THE REST ARE NOT PRETENDED:
 * `NSFileCoordinatorReadingResolvesSymbolicLink` resolves the item before the accessor sees it (asserted
 * from both sides by the probe); the others describe presenter notifications and version behaviour that
 * arrive with 7c and 7d.
 */
/* NULLABILITY, OURS AND STATED: Apple publishes no nullability for these doors, and this library REFUSES a
 * nil URL and a nil accessor WITH A NAMED ERROR rather than invoking them - so those parameters are
 * `nullable` and the refusals are asserted by the probe. A parameter that may be nil and is refused is
 * exactly what `nullable` means. */
@interface NSFileCoordinator : NSObject
{
	id _presenter;			/* the coordinator's OWN presenter, NOT retained: see +addFilePresenter: */
	NSString *_purposeIdentifier;	/* COPIED, or nil: see -setPurposeIdentifier: */
	volatile int _cancelled;	/* set by -cancel: an ACTIVE call ends its wait and runs no accessor */
}

/* ---- THE PROCESS-WIDE REGISTRY (W8 slice 7c) ------------------------------------------------------
 *
 * APPLE'S OWN SENTENCES, measured: +addFilePresenter: "registers the file presenter object PROCESS WIDE.
 * Thus, any file coordinator objects you create later automatically know about the file presenter object
 * and know to message it when its file or directory is affected"; "Be sure to balance calls to this method
 * with a corresponding call to [removeFilePresenter:]"; and "You must remove file presenters from the
 * process wide registry before the object is deallocated" - WHICH IS WHY THE REGISTRY DOES NOT OWN WHAT IT
 * HOLDS: a registry that retained its presenters would keep objects alive whose owners believe they are
 * gone, and Apple's rule (remove before deallocating) is exactly the rule a non-owning registry needs.
 */
+ (void)addFilePresenter:(id<NSFilePresenter>)filePresenter;
+ (void)removeFilePresenter:(id<NSFilePresenter>)filePresenter;
+ (NSArray *)filePresenters;

/* "A string that uniquely identifies the file access that was performed by this file coordinator", whose
 * rule is measured: "Coordinated reads and writes performed using the same purpose identifier never block
 * each other, even if they occur in DIFFERENT PROCESSES" - a statement about a system with a coordination
 * service, which this one does not have (see the doors), so what is honoured here is the PROPERTY and its
 * spelling rule: "You cannot use [nil] or zero-length strings", which this door answers by IGNORING such an
 * assignment and keeping what it had (a permitted variation: Apple says the value is not allowed and does
 * not say what happens, so nothing is invented and nothing is silently stored).
 *
 * AND ONE DOOR THAT IS DELIBERATELY HALF A PAIR: -itemAtURL:didMoveToURL: "calls the
 * [-presentedItemDidMoveToURL:] method for any of the item's file presenters", and it "must balance a call
 * to" -itemAtURL:willMoveToURL:. THAT FIRST HALF SHIPS, AND THE SECOND IS APPLE'S OWN NO-OP HERE: "This
 * method is intended for apps that adopt App Sandbox ... If your macOS app is not sandboxed, this method
 * serves no purpose. This method is nonfunctional in iOS." This system has no app sandbox, so the will form
 * is declared (a balanced pair must compile) and notifies nobody, which is Apple's sentence rather than an
 * omission. */
- (nullable NSString *)purposeIdentifier;
- (void)setPurposeIdentifier:(nullable NSString *)identifier;
- (void)itemAtURL:(NSURL *)oldURL willMoveToURL:(NSURL *)newURL;
- (void)itemAtURL:(NSURL *)oldURL didMoveToURL:(NSURL *)newURL;

/* "Performs a number of coordinated-read or -write operations ASYNCHRONOUSLY", measured: "The file
 * coordinator waits asynchronously to get access to the files and then INVOKES THE ACCESSOR BLOCK ON THE
 * SPECIFIED QUEUE. ... If an error occurs while waiting for access, AN ERROR MESSAGE IS PASSED TO THE BLOCK.
 * You must check the block's error parameter before accessing any of the files." So the accessor takes the
 * intents and an error, and "the system UPDATES this URL property to account for any changes to the
 * underlying files" is why the intents handed to it are the coordinated ones.
 *
 * THE QUEUE "MUST NOT BE" nil, and this door has NO error of its own to refuse with - so a nil queue or an
 * empty intent array answers by DOING NOTHING, which is written here rather than left to a crash. And the
 * waiting half of -cancel is honoured by the handshake beside this door: "if the block ... has not yet been
 * executed - perhaps because the file coordinator is still waiting for a response from other file
 * presenters - the file coordinator method STOPS WAITING". */
/* "Prepare to read or write from multiple files in a SINGLE BATCH operation" - and its block is not an
 * accessor in the shape the other doors take, which is measured rather than assumed: "A [block] containing
 * ADDITIONAL CALLS TO METHODS OF THIS CLASS ... The block you provide for the [batch] parameter does NOT
 * perform the actual operations itself. Instead, you MUST CALL THE INDIVIDUAL COORDINATED READ AND WRITE
 * METHODS FROM INSIDE THE BLOCK." So it takes no URLs and no error, and it "EXECUTES SYNCHRONOUSLY,
 * BLOCKING THE CURRENT THREAD until the [batch] block finishes executing".
 *
 * WHAT THIS IMPLEMENTATION DOES WITH THAT is the same handshake the other doors perform, over the two
 * lists: each item is coordinated (and refused with an error, in which case "the block ... is not
 * executed"), the presenters of those items step aside, the batch block runs, and they are reacquired
 * afterwards. Apple's own REASON for the door - "because file coordination requires interprocess
 * communication, it is much more efficient to batch changes to large numbers of files" - is the half this
 * system cannot need, since there is no interprocess communication to batch (see the boundary above). */
- (void)prepareForReadingItemsAtURLs:(nullable NSArray *)readingURLs
			     options:(NSFileCoordinatorReadingOptions)readingOptions
		writingItemsAtURLs:(nullable NSArray *)writingURLs
			     options:(NSFileCoordinatorWritingOptions)writingOptions
			       error:(NSError ** _Nullable)outError
			  byAccessor:(nullable void (^)(void))batchAccessor;

- (void)coordinateAccessWithIntents:(NSArray *)intents
			      queue:(NSOperationQueue *)queue
			 byAccessor:(void (^)(NSArray *intents, NSError * _Nullable error))accessor;

/* "Cancels any active file coordination calls ... it returns immediately ... when this method returns, you
 * cannot assume that the read or write operation occurred or did not occur." A cancelled operation ends its
 * WAIT and does not run its accessor; a call made after the cancel is a NEW call and is not affected, which
 * is what "any ACTIVE" calls means. */
- (void)cancel;

/* "This object is assumed to be performing the relevant file or directory operations and therefore does
 * NOT receive notifications about those operations" - so the coordinator's own presenter is excluded from
 * the notifications this coordinator causes, which is a rule 7c will need. */
- (id)initWithFilePresenter:(nullable id)filePresenterOrNil;

- (void)coordinateReadingItemAtURL:(nullable NSURL *)url
			   options:(NSFileCoordinatorReadingOptions)options
			     error:(NSError ** _Nullable)outError
			byAccessor:(nullable void (^)(NSURL *newURL))reader;

- (void)coordinateWritingItemAtURL:(NSURL *)url
			   options:(NSFileCoordinatorWritingOptions)options
			     error:(NSError ** _Nullable)outError
			byAccessor:(nullable void (^)(NSURL *newURL))writer;

- (void)coordinateReadingItemAtURL:(nullable NSURL *)readingURL
			   options:(NSFileCoordinatorReadingOptions)readingOptions
		  writingItemAtURL:(nullable NSURL *)writingURL
			   options:(NSFileCoordinatorWritingOptions)writingOptions
			     error:(NSError ** _Nullable)outError
			byAccessor:(nullable void (^)(NSURL *newReadingURL, NSURL *newWritingURL))readerWriter;

- (void)coordinateWritingItemAtURL:(nullable NSURL *)url1
			   options:(NSFileCoordinatorWritingOptions)options1
		  writingItemAtURL:(nullable NSURL *)url2
			   options:(NSFileCoordinatorWritingOptions)options2
			     error:(NSError ** _Nullable)outError
			byAccessor:(nullable void (^)(NSURL *newURL1, NSURL *newURL2))writer;

@end

/* "The details of a coordinated-read or coordinated-write operation" - and the WHOLE of what Apple
 * publishes for it, measured from its page: the two factories and `-URL`. THERE IS NO PUBLISHED ACCESSOR
 * FOR THE OPTIONS OR FOR THE KIND, so none is invented here; the kind is what makes the two factories
 * different objects, and the probe asserts what a caller can actually observe (the URL, and the identity
 * of two intents). */
@interface NSFileAccessIntent : NSObject
{
	NSURL *_url;			/* the item this operation is about (retained) */
	BOOL _writing;			/* which of the two operations this is */
	NSUInteger _options;		/* the caller's options, carried for the door that consumes them */
}

+ (nullable NSFileAccessIntent *)readingIntentWithURL:(NSURL *)url
					     options:(NSFileCoordinatorReadingOptions)options;
+ (nullable NSFileAccessIntent *)writingIntentWithURL:(NSURL *)url
					     options:(NSFileCoordinatorWritingOptions)options;

/* "The current URL for this file access intent." */
- (NSURL *)URL;

@end

/* OURS: the coordinator updates an intent's URL when coordination changes it - Apple: "The system updates
 * this property to account for any changes to the underlying files". */
@interface NSFileAccessIntent (FNPrivate)
- (void)fnSetURL:(NSURL *)url;
- (BOOL)fnIsWriting;
- (BOOL)fnResolvesSymbolicLink;
@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILECOORDINATOR_H */
