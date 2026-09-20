/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUndoManager.m — the implementation (W2h).
 */
#import <Foundation/NSUndoManager.h>
#import <Foundation/NSException.h>
#import <objc/runtime.h>

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
- (void)registerUndoWithTarget:(id)target selector:(SEL)selector object:(id)argument
{
	FnUndoAction *action;

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
	action = [[FnUndoAction alloc] initWithTarget:target selector:selector object:argument];
	[_group addObject:action];
	[action release];
	/* CLEARING THE REDO STACK IS WHAT MAKES REDO MEAN THE FUTURE OF THE CHANGE JUST MADE, and it must
	 * not happen while an undo or a redo is itself registering: those registrations ARE that stack. */
	if (!_redoing && !_undoing) {
		[_redoStack removeAllObjects];
	}
}

- (void)beginUndoGrouping
{
	if (_group != nil) {
		_groupingLevel++;
		return;
	}
	_group = [[NSMutableArray alloc] init];
	_groupingLevel = 1;
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
		return;
	}
	if ([_group count] > 0) {
		[_undoStack addObject:_group];
		[_group release];
		_group = nil;
	} else {
		[_group release];
		_group = nil;
	}
	_groupingLevel = 0;
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
	NSMutableArray *group;
	NSMutableArray *inverse = [[NSMutableArray alloc] init];
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
		[[group objectAtIndex:(NSUInteger)i] invoke];
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
	 * have never seen, and an undo of nothing is what a caller would get. */
	if (_groupingLevel == 1) {
		[self endUndoGrouping];
	}
	if ([_undoStack count] == 0) {
		return;
	}
	_undoing = YES;
	[self performFromStack:_undoStack toStack:_redoStack];
	_undoing = NO;
}

- (void)redo
{
	if (_groupingLevel == 1) {
		[self endUndoGrouping];
	}
	if ([_redoStack count] == 0) {
		return;
	}
	_redoing = YES;
	[self performFromStack:_redoStack toStack:_undoStack];
	_redoing = NO;
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

- (NSString *)description
{
	return [NSString stringWithFormat:@"<%@: %@>", [self class],
		[self canUndo] ? @"can undo" : @"nothing to undo"];
}

@end
