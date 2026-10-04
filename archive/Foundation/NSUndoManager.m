/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUndoManager.m — the implementation (W2h).
 */
#import <Foundation/NSUndoManager.h>
#import <Foundation/NSException.h>
#import <Foundation/NSInvocation.h>
#import <Foundation/NSMethodSignature.h>
#import <Foundation/NSProxy.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSDictionary.h>	/* NSMutableDictionary, for a group's userInfo */
#import <objc/runtime.h>

/*
 * THE BLOCKS RUNTIME'S TWO FUNCTIONS, DECLARED HERE BECAUSE THE HEADER IS STAGED NOWHERE: libobjc2 links
 * BlocksRuntime (measured: both symbols are dynamic exports of libobjc.so), while <Block.h> exists in
 * neither the host's /usr/include nor the compiler's resource directory nor the guest's prefix. THE COPY
 * IS THE POINT OF THEM: a block literal is a STACK object and dies with the frame that made it, so
 * storing one without copying it stores garbage the moment the caller returns - which is exactly the
 * "stored blocks" dependency §12.6 names for the operations family.
 */
extern void *_Block_copy(const void *aBlock);
extern void _Block_release(const void *aBlock);

/* THE INVOCATION FORM OF A REGISTERED UNDO (W2h's `-prepareWithInvocationTarget:`), and the private
 * door the proxy below reaches the manager through. */
@interface NSUndoManager (FNPrivate)
- (void)registerUndoWithTarget:(id)target invocation:(NSInvocation *)invocation;
@end

/* THE EIGHT NAMES, defined here and declared in the header (W4's centre carries them, §22). */
const NSUInteger NSUndoCloseGroupingRunLoopOrdering = 350000;
NSUndoManagerUserInfoKey const NSUndoManagerGroupIsDiscardableKey = @"NSUndoManagerGroupIsDiscardableKey";
NSString *const NSUndoManagerCheckpointNotification = @"NSUndoManagerCheckpointNotification";
NSString *const NSUndoManagerDidCloseUndoGroupNotification = @"NSUndoManagerDidCloseUndoGroupNotification";
NSString *const NSUndoManagerDidOpenUndoGroupNotification = @"NSUndoManagerDidOpenUndoGroupNotification";
NSString *const NSUndoManagerDidRedoChangeNotification = @"NSUndoManagerDidRedoChangeNotification";
NSString *const NSUndoManagerDidUndoChangeNotification = @"NSUndoManagerDidUndoChangeNotification";
NSString *const NSUndoManagerWillCloseUndoGroupNotification = @"NSUndoManagerWillCloseUndoGroupNotification";
NSString *const NSUndoManagerWillRedoChangeNotification = @"NSUndoManagerWillRedoChangeNotification";
NSString *const NSUndoManagerWillUndoChangeNotification = @"NSUndoManagerWillUndoChangeNotification";

/* ONE REGISTERED UNDO, as an object rather than a dictionary: the TARGET IS NOT RETAINED - an undo
 * that kept its own target alive could never be collected - while the argument IS, because a caller
 * usually hands over a value it is about to replace. */
@interface FnUndoAction : NSObject
{
	id _target;		/* NOT retained, deliberately */
	SEL _selector;
	id _argument;		/* retained */
}
@end

@implementation FnUndoAction

- (instancetype)initWithTarget:(id)target selector:(SEL)selector object:(id)argument
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_target = target;
	_selector = selector;
	_argument = [argument retain];
	return self;
}

- (id)target
{
	return _target;
}

- (void)invoke
{
	/* THE MESSAGE IS SENT WITH THE ARGUMENT, which is the whole mechanism: whoever registered this
	 * pair declared that sending it reverses the change. A target that has gone away is skipped
	 * rather than sent to nil-adjacent memory - the contract says the reference is unowned. */
	if (_target != nil && _selector != NULL) {
		[_target performSelector:_selector withObject:_argument];
	}
}

- (void)dealloc
{
	[_argument release];
	[super dealloc];
}

@end

/*
 * THE SAME THING, FOR A CAPTURED MESSAGE. The triple form says the inverse in three pieces; this one
 * holds the CALL, so an argument the caller built (and a return type the caller reads) survives.
 * THE TARGET IS STILL UNOWNED, for the reason the triple form gives.
 */
@interface FnUndoInvocation : NSObject
{
	id _target;			/* NOT retained, deliberately */
	NSInvocation *_invocation;	/* retained */
}
- (instancetype)initWithTarget:(id)target invocation:(NSInvocation *)invocation;
- (void)invoke;
- (id)target;
@end

@implementation FnUndoInvocation

- (instancetype)initWithTarget:(id)target invocation:(NSInvocation *)invocation
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_target = target;
	_invocation = [invocation retain];
	return self;
}

- (id)target
{
	return _target;
}

- (void)invoke
{
	/* THE TARGET IS SET AT REPLAY TIME, not at capture: the invocation was captured with the PROXY as
	 * its target (that is what the runtime delivers to -forwardInvocation:), and the caller meant the
	 * object the proxy stood for. A target that has gone away is skipped, as above. */
	if (_target != nil && _invocation != nil) {
		[_invocation setTarget:_target];
		[_invocation invoke];
	}
}

- (void)dealloc
{
	[_invocation release];
	[super dealloc];
}

@end

/*
 * THE BLOCK FORM (W2h's `-registerUndoWithTarget:handler:`): the action holds a COPY of the handler and
 * hands it the target at replay time. The handler's argument IS the target for the reason the header
 * gives - a caller that uses it captures nothing.
 */
@interface FnUndoBlock : NSObject
{
	id _target;		/* NOT retained, deliberately */
	void (^_handler)(id);	/* COPIED to the heap: this object outlives the caller's frame */
}
- (instancetype)initWithTarget:(id)target handler:(void (^)(id))handler;
- (void)invoke;
- (id)target;
@end

@implementation FnUndoBlock

- (instancetype)initWithTarget:(id)target handler:(void (^)(id))handler
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_target = target;
	_handler = handler != nil ? _Block_copy(handler) : nil;
	return self;
}

- (id)target
{
	return _target;
}

- (void)invoke
{
	/* THE TARGET IS THE ARGUMENT, which is the whole of Apple's contract for this form. A handler that
	 * has gone (a nil one) is skipped rather than called, the same way a NULL selector is. */
	if (_handler != nil) {
		_handler(_target);
	}
}

- (void)dealloc
{
	if (_handler != nil) {
		_Block_release(_handler);
	}
	[super dealloc];
}

@end

/*
 * THE PROXY `-prepareWithInvocationTarget:` HANDS BACK. It stands for the target: the signature it
 * answers with is the TARGET's, and the message sent to it is registered as that target's undo action
 * instead of being performed. NOTHING RETAINS IT - it exists for the one message - which is why it
 * holds the manager and the target unowned, exactly as the action it creates will.
 */
@interface FnUndoProxy : NSProxy
{
	NSUndoManager *_manager;	/* NOT retained */
	id _target;			/* NOT retained */
}
- (instancetype)initWithUndoManager:(NSUndoManager *)manager target:(id)target;
@end

@implementation FnUndoProxy

/* NO [super init]: NSProxy is a ROOT class and declares no -init at all - measured, because NSObject's
 * is NOT inherited here (NSProxy does not inherit from NSObject). */
- (instancetype)initWithUndoManager:(NSUndoManager *)manager target:(id)target
{
	_manager = manager;
	_target = target;
	return self;
}

- (NSMethodSignature *)methodSignatureForSelector:(SEL)selector
{
	/* THE TARGET'S SIGNATURE, which is the whole reason this is a proxy: the caller is writing a real
	 * message of the target, and the capture has to agree with it type for type. A selector the target
	 * does not implement falls through to NSProxy's own answer, which RAISES. */
	if (_target != nil && [_target respondsToSelector:selector]) {
		return [_target methodSignatureForSelector:selector];
	}
	return [super methodSignatureForSelector:selector];
}

- (void)forwardInvocation:(NSInvocation *)invocation
{
	[_manager registerUndoWithTarget:_target invocation:invocation];
}

@end

/*
 * ONE UNDO GROUP. It used to be a bare NSMutableArray, and that is precisely why the DISCARDABLE-ACTIONS
 * half and the per-group USER INFO were impossible here: there was nowhere to hang either. A group is a
 * LIST OF ACTIONS plus the two per-group facts Apple records against it — the userInfo a caller sets with
 * `-setActionUserInfoValue:forKey:` and the discardable flag `-setActionIsDiscardable:` marks — so the
 * group becomes an object of its own. THE ACTIONS ARE RETAINED (as the array retained them before) and the
 * userInfo is CREATED ONLY WHEN A CALLER SETS SOMETHING, so a manager that never touches it allocates none.
 */
@interface FnUndoGroup : NSObject
{
	NSMutableArray *_actions;	/* retained actions */
	NSMutableDictionary *_userInfo;	/* lazily created, retained */
	BOOL _discardable;
}
- (void)addAction:(id)action;
- (NSUInteger)count;
- (id)actionAtIndex:(NSUInteger)index;
- (void)removeActionsWithTarget:(id)target;
- (void)setUserInfoValue:(id)value forKey:(id)key;
- (id)userInfoValueForKey:(id)key;
- (void)setDiscardable:(BOOL)flag;
- (BOOL)isDiscardable;
@end

@implementation FnUndoGroup

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_actions = [[NSMutableArray alloc] init];
	return self;
}

- (void)dealloc
{
	[_actions release];
	[_userInfo release];
	[super dealloc];
}

- (void)addAction:(id)action
{
	[_actions addObject:action];
}

- (NSUInteger)count
{
	return [_actions count];
}

- (id)actionAtIndex:(NSUInteger)index
{
	return [_actions objectAtIndex:index];
}

/* REMOVAL IS BY TARGET IDENTITY, which is what "the operations associated with the specified target"
 * means: two equal-but-distinct objects are different undo targets here. */
- (void)removeActionsWithTarget:(id)target
{
	NSUInteger j = 0;

	while (j < [_actions count]) {
		id action = [_actions objectAtIndex:j];

		if ([action target] == target) {
			[_actions removeObjectAtIndex:j];
		} else {
			j++;
		}
	}
}

- (void)setUserInfoValue:(id)value forKey:(id)key
{
	if (key == nil) {
		return;
	}
	/* A NIL VALUE REMOVES, which is the dictionary's own meaning of setObject:nil and the reason this
	 * goes through removeObjectForKey: rather than raising. The dictionary is built on first use. */
	if (value == nil) {
		[_userInfo removeObjectForKey:key];
		return;
	}
	if (_userInfo == nil) {
		_userInfo = [[NSMutableDictionary alloc] init];
	}
	[_userInfo setObject:value forKey:key];
}

- (id)userInfoValueForKey:(id)key
{
	return key != nil ? [_userInfo objectForKey:key] : nil;
}

- (void)setDiscardable:(BOOL)flag
{
	_discardable = flag;
}

- (BOOL)isDiscardable
{
	return _discardable;
}

@end

@implementation NSUndoManager

- (instancetype)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_undoStack = [[NSMutableArray alloc] init];
	_redoStack = [[NSMutableArray alloc] init];
	_group = nil;
	_groupingLevel = 0;
	_levelsOfUndo = 0;
	return self;
}

- (void)dealloc
{
	[_undoStack release];
	[_redoStack release];
	[_group release];
	[_actionName release];
	[super dealloc];
}

/* REGISTRATION OPENS A GROUP IF NONE IS OPEN, which is what makes the simple case simple, and CLEARS
 * THE REDO STACK, which is what makes redo mean "the future of the change you just made". */
- (void)fnRegisterAction:(id)action
{
	if (_registrationDisabled) {
		/* DISABLED MEANS DISABLED, and THAT IS THE ONLY REASON TO DROP A REGISTRATION: a registration
		 * made WHILE AN UNDO RUNS is the redo, and it lands in the inverse group this manager opened
		 * for exactly that purpose. Blocking it here - which my first version did - leaves the redo
		 * stack empty and makes -redo a no-op. */
		return;
	}
	if (_group == nil) {
		[self beginUndoGrouping];
	}
	[_group addAction:action];
	/* CLEARING THE REDO STACK IS WHAT MAKES REDO MEAN THE FUTURE OF THE CHANGE JUST MADE, and it must
	 * not happen while an undo or a redo is itself registering: those registrations ARE that stack. */
	if (!_redoing && !_undoing) {
		[_redoStack removeAllObjects];
	}
}

- (void)registerUndoWithTarget:(id)target selector:(SEL)selector object:(id)argument
{
	FnUndoAction *action = [[FnUndoAction alloc] initWithTarget:target selector:selector object:argument];

	[self fnRegisterAction:action];
	[action release];
}

/* REACHED ONLY THROUGH THE PROXY. The two forms differ in WHAT the action holds, not in when it is
 * recorded or which stack it lands on: the same rule, in one place. */
- (void)registerUndoWithTarget:(id)target invocation:(NSInvocation *)invocation
{
	FnUndoInvocation *action = [[FnUndoInvocation alloc] initWithTarget:target invocation:invocation];

	[self fnRegisterAction:action];
	[action release];
}

- (void)registerUndoWithTarget:(id)target handler:(void (^)(id target))handler
{
	FnUndoBlock *action = [[FnUndoBlock alloc] initWithTarget:target handler:handler];

	[self fnRegisterAction:action];
	[action release];
}

- (id)prepareWithInvocationTarget:(id)target
{
	FnUndoProxy *proxy = [[FnUndoProxy alloc] initWithUndoManager:self target:target];

	if (proxy == nil) {
		/* THE DECLARED RETURN IS NONNULL, so allocation failure is an exception rather than a nil
		 * every caller would have to test for. */
		[NSException raise:NSMallocException
			    format:@"-[NSUndoManager prepareWithInvocationTarget:] out of memory"];
	}
	return [proxy autorelease];
}

/* THE ONE PLACE A NOTIFICATION IS POSTED FROM: the default centre, with the manager as the object and
 * no userInfo. */
- (void)fnPost:(NSString *)name
{
	[[NSNotificationCenter defaultCenter] postNotificationName:name object:self];
}

- (void)beginUndoGrouping
{
	if (_group != nil) {
		_groupingLevel++;
		/* A NESTED OPEN IS A CHECKPOINT; A TOP-LEVEL ONE IS NOT, which is the "except when it opens a
		 * top-level group" in Apple's sentence about this notification. */
		[self fnPost:NSUndoManagerCheckpointNotification];
		return;
	}
	_group = [[FnUndoGroup alloc] init];
	_groupingLevel = 1;
	[self fnPost:NSUndoManagerDidOpenUndoGroupNotification];
}

- (void)endUndoGrouping
{
	if (_group == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"-[NSUndoManager endUndoGrouping] without a matching begin"];
		return;
	}
	if (_groupingLevel > 1) {
		_groupingLevel--;
		[self fnPost:NSUndoManagerCheckpointNotification];
		return;
	}
	[self fnPost:NSUndoManagerWillCloseUndoGroupNotification];
	if ([_group count] > 0) {
		[_undoStack addObject:_group];
		[_group release];
		_group = nil;
	} else {
		[_group release];
		_group = nil;
	}
	_groupingLevel = 0;
	/* THE PAIR IS COMPLETE ON THE FAR SIDE OF THE CLOSE: the did follows the work, and the checkpoint
	 * follows both, because that is the moment the manager's state is settled. */
	[self fnPost:NSUndoManagerDidCloseUndoGroupNotification];
	[self fnPost:NSUndoManagerCheckpointNotification];
}

- (NSInteger)groupingLevel
{
	return (NSInteger)_groupingLevel;
}

- (BOOL)canUndo
{
	return [_undoStack count] > 0 || (_group != nil && [_group count] > 0);
}

- (BOOL)canRedo
{
	/* CHECKING THE REDO STACK IS ITSELF A CHECKPOINT, because it is the question whose answer changes
	 * when the stacks move: Apple's page lists this call among the three things that post it. */
	[self fnPost:NSUndoManagerCheckpointNotification];
	return [_redoStack count] > 0;
}

- (BOOL)isUndoing
{
	return _undoing;
}

- (BOOL)isRedoing
{
	return _redoing;
}

/*
 * THE TWO HALVES ARE ONE OPERATION and the difference is the flags: the group comes off one stack,
 * its actions run in REVERSE (the last change first, which is what makes an undo correct), and each
 * one registers its own inverse into the group being built on the other stack.
 */
- (void)performFromStack:(NSMutableArray *)from toStack:(NSMutableArray *)to
{
	FnUndoGroup *group;
	FnUndoGroup *inverse = [[FnUndoGroup alloc] init];
	NSInteger i;

	if ([from count] == 0) {
		[inverse release];
		return;
	}
	group = [[from lastObject] retain];
	[from removeLastObject];
	[_group release];
	_group = inverse;	/* what the actions register lands here */
	_groupingLevel = 1;
	for (i = (NSInteger)[group count] - 1; i >= 0; i--) {
		[[group actionAtIndex:(NSUInteger)i] invoke];
	}
	_groupingLevel = 0;
	[group release];
	/*
	 * NO RELEASE OF _group HERE. _group is a BORROWED reference to `inverse` - assigned without a
	 * retain two screens up - and `inverse` already owns itself from its alloc and is released once
	 * at the end of this method. Releasing it through _group as well was ONE RELEASE TOO MANY: the
	 * array was freed while `to` still held it, so the next pass's group contained dangling actions.
	 * THAT IS THE HOST SEgfault, AND IT WAS FOUND BY THE HOST LOOP: glibc reuses the freed memory
	 * and the class lookup returns nil, while musl leaves it intact and the redo APPEARS to work.
	 */
	_group = nil;
	if ([inverse count] > 0) {
		[to addObject:inverse];
	}
	[inverse release];
}

- (void)undo
{
	/* CLOSE AN OPEN GROUP FIRST, which is Apple's stated behaviour and the reason an ungrouped caller
	 * works at all: without it, the actions registered since the last begin sit in a group the stacks
	 * have never seen, and an undo of nothing is what a caller would get. -undo IS "close, then
	 * -undoNestedGroup", which is why the work itself lives in the one method below. */
	if (_groupingLevel == 1) {
		[self endUndoGrouping];
	}
	[self undoNestedGroup];
}

/* THE SAME WORK, WITHOUT CLOSING A TOP-LEVEL GROUP: it undoes the LAST group on the undo stack (in this
 * model a CLOSED group, since only closed groups reach the stack), recording its actions on the redo stack
 * as one group. An empty stack is the same quiet no-op -undo has. */
- (void)undoNestedGroup
{
	if ([_undoStack count] == 0) {
		return;
	}
	/* WILL, THEN CHECKPOINT, THEN THE WORK — Apple's documented order for an undo — and the DID after
	 * it, once the manager's state has settled. */
	[self fnPost:NSUndoManagerWillUndoChangeNotification];
	[self fnPost:NSUndoManagerCheckpointNotification];
	_undoing = YES;
	[self performFromStack:_undoStack toStack:_redoStack];
	_undoing = NO;
	[self fnPost:NSUndoManagerDidUndoChangeNotification];
}

- (void)redo
{
	if (_groupingLevel == 1) {
		[self endUndoGrouping];
	}
	if ([_redoStack count] == 0) {
		return;
	}
	[self fnPost:NSUndoManagerWillRedoChangeNotification];
	[self fnPost:NSUndoManagerCheckpointNotification];
	_redoing = YES;
	[self performFromStack:_redoStack toStack:_undoStack];
	_redoing = NO;
	[self fnPost:NSUndoManagerDidRedoChangeNotification];
}

- (void)setActionName:(NSString *)actionName
{
	[_actionName release];
	_actionName = [actionName copy];
}

- (NSString *)undoActionName
{
	return _actionName;
}

- (NSString *)redoActionName
{
	return _actionName;
}

- (void)removeAllActions
{
	[_undoStack removeAllObjects];
	[_redoStack removeAllObjects];
	/* THE OPEN GROUP IS A STACK TOO: leaving it would make canUndo true after a caller asked for
	 * everything to be forgotten. */
	[_group release];
	_group = nil;
	_groupingLevel = 0;
}

- (void)disableUndoRegistration
{
	_registrationDisabled = YES;
}

- (void)enableUndoRegistration
{
	_registrationDisabled = NO;
}

- (BOOL)isUndoRegistrationEnabled
{
	return !_registrationDisabled;
}

/* THE COUNTS ARE GROUPS, which is what an undo IS here: one entry on the undo stack is one invocation of
 * -undo, so the count of entries is the number of times the caller can still undo. THE OPEN GROUP IS NOT
 * COUNTED (it is not yet an action), exactly as -canUndo distinguishes it. */
- (NSUInteger)undoCount
{
	return [_undoStack count];
}

- (NSUInteger)redoCount
{
	return [_redoStack count];
}

- (NSUInteger)levelsOfUndo
{
	return _levelsOfUndo;
}

/* SETTING A LIMIT TRIMS THE STACK AT ONCE, which is Apple's "setting a limit below the prior one
 * immediately drops old undo groups": the OLDEST groups are at the BOTTOM, so index 0 goes first. A limit
 * of 0 is no limit, so the loop never runs and the stack is left whole. */
- (void)setLevelsOfUndo:(NSUInteger)levels
{
	_levelsOfUndo = levels;
	if (levels > 0) {
		while ([_undoStack count] > levels) {
			[_undoStack removeObjectAtIndex:0];
		}
	}
}

- (void)fnRemoveActionsWithTarget:(id)target fromStack:(NSMutableArray *)stack
{
	NSUInteger i = 0;

	while (i < [stack count]) {
		FnUndoGroup *group = [stack objectAtIndex:i];

		[group removeActionsWithTarget:target];
		/* AN EMPTY GROUP IS AN UNDO OF NOTHING and would leave -canUndo answering yes, so it is dropped. */
		if ([group count] == 0) {
			[stack removeObjectAtIndex:i];
		} else {
			i++;
		}
	}
}

- (void)removeAllActionsWithTarget:(id)target
{
	[self fnRemoveActionsWithTarget:target fromStack:_undoStack];
	[self fnRemoveActionsWithTarget:target fromStack:_redoStack];
	/* THE OPEN GROUP IS A STACK TOO, exactly as -removeAllActions says: a caller clearing a target's
	 * operations means the one being built as well, or that target's next undo would still be there. */
	if (_group != nil) {
		[_group removeActionsWithTarget:target];
	}
}

/* THE PER-GROUP USER INFO. THE SETTER ATTACHES TO THE CURRENT GROUP, opening one if a caller sets a value
 * before registering anything - the same auto-open -fnRegisterAction: performs - so the value lands on the
 * group the next action joins. The dictionaries are read back from the group at the TOP of either stack,
 * which is the group the next undo (or redo) would replay. */
- (void)setActionUserInfoValue:(id)value forKey:(NSUndoManagerUserInfoKey)key
{
	if (key == nil) {
		return;
	}
	if (_group == nil) {
		[self beginUndoGrouping];
	}
	[_group setUserInfoValue:value forKey:key];
}

- (id)undoActionUserInfoValueForKey:(NSUndoManagerUserInfoKey)key
{
	return [[_undoStack lastObject] userInfoValueForKey:key];
}

- (id)redoActionUserInfoValueForKey:(NSUndoManagerUserInfoKey)key
{
	return [[_redoStack lastObject] userInfoValueForKey:key];
}

/* AND WHETHER THE GROUP MAY BE DISCARDED, the surface the discardable-actions half names: the setter marks
 * the current group (auto-opening one, as the user-info setter does), the two questions read the group at
 * the top of their stack. A nil group answers NO, which is the safe default Apple's "can this be thrown
 * away?" wants. */
- (void)setActionIsDiscardable:(BOOL)discardable
{
	if (_group == nil) {
		[self beginUndoGrouping];
	}
	[_group setDiscardable:discardable];
}

- (BOOL)undoActionIsDiscardable
{
	return [[_undoStack lastObject] isDiscardable];
}

- (BOOL)redoActionIsDiscardable
{
	return [[_redoStack lastObject] isDiscardable];
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@>", [self class],
		[self canUndo] ? @"can undo" : @"nothing to undo"];
}


- (NSString *)undoMenuTitleForUndoActionName:(NSString *)actionName
{
	/* APPLE'S PATTERN IS LOCALIZED and this tree ships no localization tables, so the pattern IS the
	 * contract and it is stated: "Undo", or "Undo <name>" when the group has one. An empty name gets the
	 * bare verb, as Apple's own default does. */
	if (actionName == nil || [actionName length] == 0) {
		return @"Undo";
	}
	return [NSString stringWithFormat:@"Undo %@", actionName];
}

- (NSString *)redoMenuTitleForUndoActionName:(NSString *)actionName
{
	if (actionName == nil || [actionName length] == 0) {
		return @"Redo";
	}
	return [NSString stringWithFormat:@"Redo %@", actionName];
}

- (NSString *)undoMenuItemTitle
{
	return [self undoMenuTitleForUndoActionName:[self undoActionName]];
}

- (NSString *)redoMenuItemTitle
{
	return [self redoMenuTitleForUndoActionName:[self redoActionName]];
}
@end
