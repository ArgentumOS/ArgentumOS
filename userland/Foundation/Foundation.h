/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * Foundation.h — the umbrella.
 *
 * A consumer writes `#import <Foundation/Foundation.h>`. The path is lower-case
 * on purpose: Apple's Foundation is <Foundation/…>, and the wall's gate fails
 * the build if a first-party file ever imports that spelling.
 *
 * docs/design/foundation-plan.md — F0 ships the ROOT CLASS. It ships no pool
 * class, because the RUNTIME adopts any class named NSAutoreleasePool as its own
 * pool object (§6 of the plan), so pools are the runtime's and ARC's
 * `@autoreleasepool` is the interface to them.
 *
 * THE OWNERSHIP RULE, STATED ONCE FOR THE WHOLE LIBRARY (F13.21), because it is
 * the one thing a reader of any file here needs and no single file can say:
 *
 *   THE LIBRARY IS COMPILED WITHOUT ARC, and not by preference — this system's
 *   libobjc2 uses the LEGACY runtime ABI, and clang refuses -fobjc-arc on it
 *   ("not supported on platforms using the legacy runtime"). Enabling ARC would
 *   mean changing the runtime's object layout, not adding a flag.
 *
 *   SO THE MODEL IS MANUAL, AND IT IS THE SAME IN EVERY FILE: a method STORES
 *   the pointers it is given and owns NOTHING. An object handed to the library
 *   must outlive its use of it; a collection does not keep its members alive; a
 *   delegate, target or observer is the caller's to keep. The exceptions are
 *   the classes that own C STORAGE (a buffer, a mutex, a compiled regex), and
 *   each of those frees what it allocated in -dealloc and says so in its own
 *   header.
 *
 *   THE CONSEQUENCE, WHICH IS WHY THIS IS WRITTEN DOWN: a caller must not
 *   release a pointer it has handed in until the library is done with it, and a
 *   library call that outlives its caller's scope — a thread, a timer, a queue —
 *   is the caller's to keep alive. Every file in this library was written to
 *   that rule; the four that were not were fixed when they were found.
 */

#ifndef FOUNDATION_FOUNDATION_H
#define FOUNDATION_FOUNDATION_H

#import <Foundation/NSObjCRuntime.h>
#import <Foundation/NSBundle.h>
#import <Foundation/NSByteOrder.h>
#import <Foundation/NSUUID.h>
#import <Foundation/NSDecimal.h>
#import <Foundation/NSDecimalNumber.h>
#import <Foundation/NSDateInterval.h>
#import <Foundation/NSValueTransformer.h>
#import <Foundation/NSAffineTransform.h>
#import <Foundation/NSAutoreleasePool.h>
#import <Foundation/NSProxy.h>
#import <Foundation/NSUndoManager.h>
#import <Foundation/NSJSONSerialization.h>
#import <Foundation/NSObject.h>
#import <Foundation/NSGeometry.h>
#import <Foundation/NSString.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDate.h>
#import <Foundation/NSDateComponents.h>
#import <Foundation/NSTimeZone.h>
#import <Foundation/NSCalendar.h>
#import <Foundation/NSAttributedString.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
/* THE URL LOADING SYSTEM'S ERROR NAMES (§56): the codes, the two reason enums and the userInfo keys. Its own
 * header because the family is its own, and imported here so a caller of Foundation has them. */
#import <Foundation/NSURLError.h>
#import <Foundation/NSException.h>
#import <Foundation/NSCharacterSet.h>
#import <Foundation/NSScanner.h>
#import <Foundation/NSOrthography.h>
#import <Foundation/NSLinguisticTagger.h>
#import <Foundation/NSSpellServer.h>
#import <Foundation/NSArchiver.h>
#import <Foundation/NSUbiquitousKeyValueStore.h>
#import <Foundation/NSBundleResourceRequest.h>
#import <Foundation/NSUserActivity.h>
#import <Foundation/NSDistantObjectRequest.h>
#import <Foundation/NSIndexSet.h>
#import <Foundation/NSIndexPath.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSEnumerator.h>
#import <Foundation/NSDirectoryEnumerator.h>
#import <Foundation/NSFileCoordinator.h>
#import <Foundation/NSFilePresenter.h>
#import <Foundation/NSFileSecurity.h>
#import <Foundation/NSFileVersion.h>
#import <Foundation/NSFileProviderService.h>
#import <Foundation/NSXMLDTD.h>
#import <Foundation/NSXMLDocument.h>
#import <Foundation/NSXMLNode.h>
#import <Foundation/NSXMLParser.h>
#import <Foundation/NSFileWrapper.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSURL.h>
#import <Foundation/NSKeyValueCoding.h>
#import <Foundation/NSSortDescriptor.h>
/* ⚠⚠ AND THE PREDICATE FAMILY IS NOT IN THE UMBRELLA ANY MORE (§63.161): NSPredicate,
 * NSExpression, NSCompoundPredicate, NSComparisonPredicate and the format grammar are ALL macOS 10.4 —
 * later than the 10.2 baseline §11.0 targets — and §63.135 had ALREADY removed their ledger rows with
 * the recorded reason "REMOVED rather than struck ... the headers lose these names in the same campaign".
 * THIS IS THAT CAMPAIGN. What the family took with it is the predicate-taking API on the collection
 * classes, which is 10.4 too: `-filteredArrayUsingPredicate:`, `-filterUsingPredicate:` and their
 * siblings left NSArray, NSSet and NSOrderedSet in the same pass. */
#import <Foundation/NSFormatter.h>
#import <Foundation/NSLengthFormatter.h>
#import <Foundation/NSMassFormatter.h>
#import <Foundation/NSEnergyFormatter.h>
#import <Foundation/NSDateFormatter.h>
#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSPersonNameComponents.h>
#import <Foundation/NSISO8601DateFormatter.h>
#import <Foundation/NSDateIntervalFormatter.h>
#import <Foundation/NSRelativeDateTimeFormatter.h>
#import <Foundation/NSDateComponentsFormatter.h>
/* W12's first slice: the unit machinery and the value that carries one. None of these four needs ICU —
 * the arithmetic is the converters' — which is why they are not in FN_FOUNDATION_ICU. */
/* W12's dimensional families. Each is a table and a set of class methods — and none needs ICU, for the same
 * reason the machinery above needs none: the arithmetic is the converters'. */
#import <Foundation/NSKeyedArchiverDelegate.h>
#import <Foundation/NSKeyedUnarchiverDelegate.h>
#import <Foundation/NSSecureUnarchiveFromDataTransformer.h>
#import <Foundation/NSPointerFunctions.h>
#import <Foundation/NSPointerArray.h>
#import <Foundation/NSHashTable.h>
#import <Foundation/NSMapTable.h>
#import <Foundation/NSDiscardableContent.h>
#import <Foundation/NSPurgeableData.h>
#import <Foundation/NSCache.h>
#import <Foundation/NSCacheDelegate.h>
#import <Foundation/NSSet.h>
#import <Foundation/NSCountedSet.h>
#import <Foundation/NSOrderedSet.h>
#import <Foundation/NSMutableOrderedSet.h>
#import <Foundation/NSValue.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSDistributedNotificationCenter.h>
#import <Foundation/NSNotificationQueue.h>
#import <Foundation/NSNull.h>
#import <Foundation/NSKeyValueObserving.h>
#import <Foundation/NSCoding.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSKeyedArchiver.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSURLComponents.h>
#import <Foundation/NSTextCheckingResult.h>
#import <Foundation/NSRegularExpression.h>
#import <Foundation/NSDataDetector.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSThread.h>
#import <Foundation/NSTimer.h>
#import <Foundation/NSRunLoop.h>
#import <Foundation/NSHost.h>
#import <Foundation/NSBackgroundActivityScheduler.h>
#import <Foundation/NSCalendarDate.h>
#import <Foundation/NSOperation.h>
#import <Foundation/NSBlockOperation.h>
#import <Foundation/NSInvocationOperation.h>
#import <Foundation/NSOperationQueue.h>
#import <Foundation/NSProgress.h>
#import <Foundation/NSItemProvider.h>
#import <Foundation/NSUserDefaults.h>
#import <Foundation/NSPort.h>
#import <Foundation/NSMachPort.h>
#import <Foundation/NSMessagePort.h>
#import <Foundation/NSPortNameServer.h>
#import <Foundation/NSMessagePortNameServer.h>
#import <Foundation/NSSocketPortNameServer.h>
#import <Foundation/NSMachBootstrapServer.h>
#import <Foundation/NSProtocolChecker.h>
#import <Foundation/NSDistributedLock.h>
#import <Foundation/NSPortCoder.h>
#import <Foundation/NSDistantObject.h>
#import <Foundation/NSConnection.h>
#import <Foundation/NSPortMessage.h>
#import <Foundation/NSSocketPort.h>
#import <Foundation/NSFileHandle.h>
#import <Foundation/NSPipe.h>
#import <Foundation/NSInputStream.h>
#import <Foundation/NSOutputStream.h>
#import <Foundation/NSUserUnixTask.h>
#import <Foundation/NSStream.h>
#import <Foundation/NSTask.h>
#import <Foundation/NSURLRequest.h>
#import <Foundation/NSURLResponse.h>
#import <Foundation/NSURLConnection.h>
#import <Foundation/NSURLDownload.h>
#import <Foundation/NSURLHandle.h>
#import <Foundation/NSCachedURLResponse.h>
#import <Foundation/NSURLCache.h>
#import <Foundation/NSURLProtocol.h>
#import <Foundation/FNCURLURLProtocol.h>
/* ⚠⚠ AND THE NSURLSession FAMILY IS NOT IN THE UMBRELLA ANY MORE (§63.159). It was SEVEN classes — NSURLSession,
 * its configuration, its task base, its stream/download/websocket tasks and its metrics record — all of them
 * Apple's LATER than the 10.2 baseline this library targets (§11.0), and all of them REMOVED here together. The
 * selector ledger had already filtered their owners out (§63.135); this is the code catching up with that
 * statement. WHAT REPLACED THEM IS THE 10.2 SHAPE THE SEAM ALWAYS HAD: a connection and a download drive
 * `NSURLProtocol` themselves (§63.153, §63.157), so no class in this library owns a session. */
#import <Foundation/NSHTTPURLResponse.h>
#import <Foundation/NSHTTPCookie.h>
#import <Foundation/NSURLProtectionSpace.h>
#import <Foundation/NSURLCredential.h>
#import <Foundation/NSURLAuthenticationChallenge.h>
#import <Foundation/NSURLCredentialStorage.h>
#import <Foundation/NSHTTPCookieStorage.h>

#endif /* FOUNDATION_FOUNDATION_H */
