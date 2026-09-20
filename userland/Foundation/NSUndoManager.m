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
	[_group addObject:action];
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
