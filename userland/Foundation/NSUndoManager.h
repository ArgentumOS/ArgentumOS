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
 * NOTIFICATIONS (five of them) cannot be posted because THIS LIBRARY HAS NO NOTIFICATION CENTRE -
 * the notifications family is §12's W4 and is not built - so their absence is a DEPENDENCY rather
 * than an omission, which is the opposite of what this line used to claim ("this library has the
 * notification centre to carry them"). `-registerUndoWithTarget:handler:` (the block form) and
 * `-undoMenuTitleForUndoActionName:` (menu strings) remain named. What is here is the stack, its
 * grouping, its names, its switch, and `-prepareWithInvocationTarget:` with its proxy.
 */
#ifndef FOUNDATION_NSUNDOMANAGER_H
#define FOUNDATION_NSUNDOMANAGER_H

#import <Foundation/NSObject.h>
#import <Foundation/NSString.h>
#import <Foundation/NSArray.h>	/* NSMutableArray, for the two stacks */

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

- (void)disableUndoRegistration;
- (void)enableUndoRegistration;
@property (readonly) BOOL isUndoRegistrationEnabled;

@end

NS_ASSUME_NONNULL_END
#endif /* FOUNDATION_NSUNDOMANAGER_H */
