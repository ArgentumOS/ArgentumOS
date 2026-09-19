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
 * WHAT IS NOT, named rather than discovered: the NOTIFICATIONS (five of them, and this library has the
 * notification centre to carry them), `-prepareWithInvocationTarget:` and its forwarding proxy,
 * `-registerUndoWithTarget:handler:` (the block form) and `-undoMenuTitleForUndoActionName:` (menu
 * strings). What is here is the stack, its grouping, its names and its switch.
 */
#ifndef FOUNDATION_NSUNDOMANAGER_H
#define FOUNDATION_NSUNDOMANAGER_H

#import <foundation/NSObject.h>
#import <foundation/NSString.h>
#import <foundation/NSArray.h>	/* NSMutableArray, for the two stacks */

NS_ASSUME_NONNULL_BEGIN

@interface NSUndoManager : NSObject
{
	NSMutableArray *_undoStack;		/* of groups; each group is an array of actions */
	NSMutableArray *_redoStack;
	NSMutableArray *_group;			/* the group being built, when one is open */
	NSString *_actionName;
	unsigned long _groupingLevel;
	BOOL _undoing;
	BOOL _redoing;
	BOOL _registrationDisabled;
}

/* TARGET IS HELD WEAKLY and the object STRONGLY, which is Apple's contract and the reason an undo
 * cannot keep its own target alive: the pair would then never go away. */
- (void)registerUndoWithTarget:(id)target selector:(SEL)selector object:(nullable id)object;

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

- (void)disableUndoRegistration;
- (void)enableUndoRegistration;
@property (readonly) BOOL isUndoRegistrationEnabled;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSUNDOMANAGER_H */
