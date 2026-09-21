/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserUnixTask — RUNNING AN ORDINARY UNIX SCRIPT. docs/design/foundation-plan.md W6's streams half, §45-Y
 * sub-step 4.
 *
 * WHAT IT IS: a script at a URL, executed with arguments, whose result arrives in a
 * `NSUserUnixTaskCompletionHandler`. Nothing about the execution is new here - it is `fork`/`exec` and a
 * status, which is W6d's `NSTask` - so this class is BUILT ON `NSTask` rather than repeating its reaper: an
 * `NSTask` whose executable is the script, whose arguments are the caller's, and whose termination handler is
 * the completion handler.
 *
 * AND ONE THING THIS CLASS HAS IS APPLE'S AND NOT OUR CHOICE, WHICH IS WHY IT IS STATED FIRST: IN APPLE'S API
 * `NSUserUnixTask` INHERITS `NSUserScriptTask`, AND §39 STRUCK THAT SUPERCLASS - along with its two
 * AppleScript and Automator siblings - keeping this one on the ground that it "runs an ordinary Unix script,
 * which is process execution rather than AppleScript". So the hierarchy here is
 *
 *     NSUserUnixTask : NSObject        ← THE DEVIATION
 *
 * and `-initWithScriptURL:error:` and the three standard streams are DECLARED here rather than inherited,
 * because a class cannot inherit from what this project refused to ship. That is the whole of the difference
 * a caller sees, and the user took the decision to make it (2026-09-21).
 *
 * TWO THINGS ARE OURS, STATED BECAUSE APPLE PUBLISHES NEITHER:
 *
 *   * THE SCRIPT IS EXECUTED BY THE KERNEL, so it needs a `#!` line and the executable bit - an ordinary
 *     Unix script, exactly as the row's own ground for keeping the class says. An error out of
 *     `-initWithScriptURL:error:` is `NSPOSIXErrorDomain` with the `errno` that caused it.
 *   * THE COMPLETION HANDLER RUNS ON THE REAPER'S THREAD, and the error it is handed is nil for a script that
 *     exited 0 and an `NSPOSIXErrorDomain` error carrying the exit status or the signal otherwise.
 */

#ifndef FOUNDATION_NSUSERUNIXTASK_H
#define FOUNDATION_NSUSERUNIXTASK_H

#import <Foundation/NSObject.h>

@class NSArray;
@class NSError;
@class NSFileHandle;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* NIL FOR SUCCESS; an error when the script failed to run or exited non-zero. */
typedef void (^NSUserUnixTaskCompletionHandler)(NSError * _Nullable error);

@interface NSUserUnixTask : NSObject
{
	NSURL *_scriptURL;
	NSFileHandle *_standardInput;
	NSFileHandle *_standardOutput;
	NSFileHandle *_standardError;
}

/* NIL WITH AN ERROR when the URL is nil or names nothing this system can execute - Apple's shape for this
 * initialiser, and the error is the POSIX one for the reason. */
- (nullable instancetype)initWithScriptURL:(NSURL *)url error:(NSError **)error;

- (nullable NSURL *)scriptURL;

/* THE THREE STANDARD STREAMS, as Apple's struck superclass declares them: an NSFileHandle for each, or nil
 * to inherit the caller's. NOT COPIED - a handle is a wrapper for a descriptor and the caller keeps owning it. */
- (nullable NSFileHandle *)standardInput;
- (void)setStandardInput:(nullable NSFileHandle *)handle;
- (nullable NSFileHandle *)standardOutput;
- (void)setStandardOutput:(nullable NSFileHandle *)handle;
- (nullable NSFileHandle *)standardError;
- (void)setStandardError:(nullable NSFileHandle *)handle;

/* ASYNCHRONOUS, which is why the handler exists: this returns before the script has finished, and the handler
 * is called - ON THE REAPER'S THREAD - when it has. A nil handler is allowed and simply means nobody is
 * waiting for the answer. */
- (void)executeWithArguments:(nullable NSArray *)arguments
	   completionHandler:(nullable NSUserUnixTaskCompletionHandler)handler;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSUSERUNIXTASK_H */
