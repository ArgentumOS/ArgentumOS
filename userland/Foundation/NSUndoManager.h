/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUndoManager.h — a recorder of operations that can be undone (W2h, §14).
 *
 * AN UNDO IS A MESSAGE SENT LATER. A caller registers the pair (target, selector) that would REVERSE
 * its change, with the object to send it, and the manager replays them in reverse order when asked.
 * The redo stack is not a second mechanism: undoing an action registers the action that undoes IT, so
 * redo is the same machinery run the other way, which is why registering anything clears redo.
 *
 * WHAT IS NOT, named rather than discovered, AND ONE OF THESE REASONS WAS WRONG HERE: the
 * NOTIFICATIONS ship (the centre exists, §21) - the eight names are declared below and posted at the
 * points Apple documents. `-undoMenuTitleForUndoActionName:`/`-redoMenuTitleForUndoActionName:` and the
 * `-undo/redoMenuItemTitle` properties remain named: their titles are per-locale TEMPLATES (the words
 * "Undo"/"Redo" are the application's LOCALIZED resources), and a hard-coded English string would answer
 * the SHAPE while being wrong in every other locale - a fidelity deviation rather than an implementation.
 *
 * AND THIS ONE WAS MEASURED WRONG AND IS SHIPPING NOW: the DISCARDABLE-ACTIONS half used to be named here
 * as "a surface this class does not have". It is not a missing surface - the group existed as a bare
 * NSMutableArray with nowhere to hang per-group state; giving the group an object of its own (FnUndoGroup)
 * is exactly the move that creates the surface, so `-setActionIsDiscardable:` and the two questions behind
 * it are implemented below (§62.103's discardable half).
 */
#ifndef FOUNDATION_NSUNDOMANAGER_H
#define FOUNDATION_NSUNDOMANAGER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>	/* NSMutableArray, for the two stacks */

NS_ASSUME_NONNULL_BEGIN

/* THE RUN-LOOP PRIORITY FOR CLOSING AN UNDO GROUP (§62.103): Apple's 350000, the `order` argument a caller
 * passes to `-[NSRunLoop performSelector:target:argument:order:modes:]` — high enough that grouping closes
 * after the event that opened it. */
extern const NSUInteger NSUndoCloseGroupingRunLoopOrdering;

/* THE TYPE OF A userInfo KEY (§62.103), which is what lets the key below be declared at all — and what a
 * caller writes when it reads a group's userInfo. */
typedef NSString *NSUndoManagerUserInfoKey;

/* AND THE KEY A GROUP'S userInfo CARRIES TO SAY IT MAY BE DISCARDED, which this header's own note about the
 * discardable-actions half already refers to. */
extern NSUndoManagerUserInfoKey const NSUndoManagerGroupIsDiscardableKey;

/* THE GROUP OBJECT: a list of actions PLUS the per-group state (userInfo and the discardable flag). It is
 * named here only so the ivar below can be typed; its definition is private to the implementation. */
@class FnUndoGroup;

@interface NSUndoManager : NSObject
{
	NSMutableArray *_undoStack;		/* of FnUndoGroup objects */
	NSMutableArray *_redoStack;
	FnUndoGroup *_group;			/* the group being built, when one is open */
	NSString *_actionName;
	unsigned long _groupingLevel;
	NSUInteger _levelsOfUndo;		/* 0 means no limit, Apple's default */
	BOOL _undoing;
	BOOL _redoing;
	BOOL _registrationDisabled;
}

/* TARGET IS HELD WEAKLY and the object STRONGLY, which is Apple's contract and the reason an undo
 * cannot keep its own target alive: the pair would then never go away. */
- (void)registerUndoWithTarget:(id)target selector:(SEL)selector object:(nullable id)object;

/*
 * THE BLOCK FORM, and the shape of the block is the design rather than a detail: IT RECEIVES THE TARGET
 * AS ITS SINGLE ARGUMENT, precisely so that a caller uses the argument instead of capturing the target
 * — which is the cycle the two forms above warn about, avoided by construction. The block is COPIED
 * when it is registered (a block literal is a STACK object, and this one outlives the call), and the
 * target is held unowned, as always.
 */
- (void)registerUndoWithTarget:(id)target handler:(void (^)(id target))handler;

/*
 * THE PROXY FORM: the message sent to the returned object BECOMES the undo action, captured with the
 * TARGET's own METHOD SIGNATURE - which is what makes it usable when the inverse is more than a
 * target/selector/argument triple can carry. Allocation failure RAISES NSMallocException rather than
 * answering nil, so the contract is Apple's; that is the same choice the register records for
 * NSData's writers (D4).
 */
- (id)prepareWithInvocationTarget:(id)target;

- (void)undo;
- (void)redo;

@property (readonly) BOOL canUndo;
@property (readonly) BOOL canRedo;
@property (readonly) BOOL isUndoing;
@property (readonly) BOOL isRedoing;

/* AN ACTION NAME NAMES A GROUP, so it survives until the group is undone or redone - which is what
 * makes `Undo Delete` possible: the name is read back from the group about to be undone. */
- (void)setActionName:(NSString *)actionName;
@property (readonly, nullable) NSString *undoActionName;
@property (readonly, nullable) NSString *redoActionName;

- (void)beginUndoGrouping;
- (void)endUndoGrouping;
@property (readonly) NSInteger groupingLevel;

- (void)removeAllActions;

/* THE DEPTH AND THE COUNTS (§62.103): how deep the undo stack may grow, and how many top-level groups sit
 * on each stack - the number of times -undo/-redo can be invoked before there is nothing left to do. A
 * `levelsOfUndo` of 0 is NO LIMIT, Apple's default, so the setter only trims when given a positive one. */
@property NSUInteger levelsOfUndo;
@property (readonly) NSUInteger undoCount;
@property (readonly) NSUInteger redoCount;

- (void)removeAllActionsWithTarget:(id)target;

/* A PER-GROUP MESSAGE SENT LATER, like -undo but of the LAST group rather than the top-level close: -undo
 * closes an open top-level group first and then does this, so the two share one body. */
- (void)undoNestedGroup;

/* THE PER-GROUP USER INFO (§62.103): a caller hangs arbitrary values on the group it is building, keyed by
 * an NSUndoManagerUserInfoKey, and reads them back from the group at the top of either stack. */
- (void)setActionUserInfoValue:(nullable id)value forKey:(NSUndoManagerUserInfoKey)key;
- (nullable id)undoActionUserInfoValueForKey:(NSUndoManagerUserInfoKey)key;
- (nullable id)redoActionUserInfoValueForKey:(NSUndoManagerUserInfoKey)key;

/* AND WHETHER THE GROUP MAY BE DISCARDED, which is the surface the discardable-actions half names: mark
 * the group being built, and ask about the group at the top of either stack. */
- (void)setActionIsDiscardable:(BOOL)discardable;
@property (readonly) BOOL undoActionIsDiscardable;
@property (readonly) BOOL redoActionIsDiscardable;

/*
 * THE EIGHT NOTIFICATIONS (W4's centre, §22). THEY GO TO THE DEFAULT CENTRE, their OBJECT IS THE
 * MANAGER, and none carries a userInfo — the one documented key, `NSUndoManagerGroupIsDiscardableKey`,
 * is read from a WillClose notification's userInfo on systems old enough to carry one; Apple's current
 * notifications carry none, so this manager queries discardability through the two questions above rather
 * than through a posted dictionary. WHERE EACH IS POSTED is Apple's own wording,
 * and the wording is narrower than it is usually taken for: DidOpenUndoGroup is the OPEN, WillClose/
 * DidClose surround a CLOSE, and CHECKPOINT is posted when a group is deferred (a nested open), when a
 * group closes, and when the REDO STACK IS CHECKED — which is why a checkpoint observer that calls
 * -canRedo loops forever (Apple documents the hazard; this comment is where a reader meets it).
 */
extern NSString *const NSUndoManagerCheckpointNotification;
extern NSString *const NSUndoManagerDidCloseUndoGroupNotification;
extern NSString *const NSUndoManagerDidOpenUndoGroupNotification;
extern NSString *const NSUndoManagerDidRedoChangeNotification;
extern NSString *const NSUndoManagerDidUndoChangeNotification;
extern NSString *const NSUndoManagerWillCloseUndoGroupNotification;
extern NSString *const NSUndoManagerWillRedoChangeNotification;
extern NSString *const NSUndoManagerWillUndoChangeNotification;

- (void)disableUndoRegistration;
- (void)enableUndoRegistration;
@property (readonly) BOOL isUndoRegistrationEnabled;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSUNDOMANAGER_H */
