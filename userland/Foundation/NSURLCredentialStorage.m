/*
 * NSURLCredentialStorage.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */

#import <Foundation/NSURLCredentialStorage.h>
#import <Foundation/NSURLCredential.h>
#import <Foundation/NSNotificationCenter.h>

NSString * const NSURLCredentialStorageChangedNotification =
	@"NSURLCredentialStorageChangedNotification";

@implementation NSURLCredentialStorage

+ (NSURLCredentialStorage *)sharedCredentialStorage
{
	static NSURLCredentialStorage *shared = nil;

	if(shared == nil) {
		shared = [[self alloc] init];
	}
	return shared;
}

- (instancetype)init
{
	if(!(self = [super init])) {
		return nil;
	}
	/* THE DICTIONARIES COPY THEIR KEYS, which is exactly why NSURLProtectionSpace had to implement -copy,
	 * -isEqual: and -hash before this class could work at all. */
	_credentials = [[NSMutableDictionary alloc] init];
	_defaultCredentials = [[NSMutableDictionary alloc] init];
	_lock = [[NSLock alloc] init];
	return self;
}

- (void)dealloc
{
	[_credentials release];
	[_defaultCredentials release];
	[_lock release];
	[super dealloc];
}

- (void)fn_notifyChanged
{
	[[NSNotificationCenter defaultCenter]
		postNotificationName:NSURLCredentialStorageChangedNotification object:self];
}

- (NSMutableArray *)fn_listForSpace:(NSURLProtectionSpace *)space create:(BOOL)create
{
	NSMutableArray *list = [_credentials objectForKey:space];

	if(list == nil && create) {
		list = [NSMutableArray array];
		[_credentials setObject:list forKey:space];
	}
	return list;
}

- (void)setCredential:(NSURLCredential *)credential
    forProtectionSpace:(NSURLProtectionSpace *)space
{
	NSMutableArray *list;
	NSUInteger i;
	BOOL changed = NO;

	if(credential == nil || space == nil) {
		return;
	}
	[_lock lock];
	list = [self fn_listForSpace:space create:YES];
	/* A CREDENTIAL ALREADY STORED FOR THIS SPACE IS REPLACED, NOT DUPLICATED - otherwise setting the same
	 * one twice would grow the list without bound and a lookup would return the stale copy first. */
	for(i = 0; i < [list count]; i++) {
		if([[list objectAtIndex:i] isEqual:credential]) {
			[list replaceObjectAtIndex:i withObject:credential];
			changed = YES;
			break;
		}
	}
	if(!changed) {
		[list addObject:credential];
		changed = YES;
	}
	[_lock unlock];
	/* ONLY A REAL CHANGE IS ANNOUNCED - the rule the cookie store's checks taught, applied here from the
	 * start rather than found by a probe. */
	if(changed) {
		[self fn_notifyChanged];
	}
}

- (void)removeCredential:(NSURLCredential *)credential
       forProtectionSpace:(NSURLProtectionSpace *)space
{
	NSMutableArray *list;
	NSUInteger i;
	BOOL removed = NO;

	if(credential == nil || space == nil) {
		return;
	}
	[_lock lock];
	list = [self fn_listForSpace:space create:NO];
	for(i = 0; list != nil && i < [list count]; i++) {
		if([[list objectAtIndex:i] isEqual:credential]) {
			[list removeObjectAtIndex:i];
			removed = YES;
			break;
		}
	}
	if(list != nil && [list count] == 0) {
		[_credentials removeObjectForKey:space];
	}
	[_lock unlock];
	if(removed) {
		[self fn_notifyChanged];
	}
}

- (void)setDefaultCredential:(NSURLCredential *)credential
	  forProtectionSpace:(NSURLProtectionSpace *)space
{
	if(credential == nil || space == nil) {
		return;
	}
	[_lock lock];
	[_defaultCredentials setObject:credential forKey:space];
	[_lock unlock];
	[self fn_notifyChanged];
}

- (NSURLCredential *)defaultCredentialForProtectionSpace:(NSURLProtectionSpace *)space
{
	NSURLCredential *credential;

	[_lock lock];
	credential = [[[_defaultCredentials objectForKey:space] retain] autorelease];
	[_lock unlock];
	return credential;
}

- (NSDictionary *)allCredentials
{
	NSDictionary *copy;

	[_lock lock];
	copy = [[_credentials copy] autorelease];
	[_lock unlock];
	return copy;
}

- (NSArray *)credentialsForProtectionSpace:(NSURLProtectionSpace *)space
{
	NSArray *list = nil;

	[_lock lock];
	list = [[[_credentials objectForKey:space] retain] autorelease];
	[_lock unlock];
	/* AN EMPTY ARRAY RATHER THAN nil: a space with no credentials is a question with an answer, and Apple's
	 * contract is that this returns the credentials (possibly none) rather than failing. */
	return (list != nil ? list : [NSArray array]);
}

/* --- THE TASK-SCOPED DOORS: the same answer, with the task accepted so Apple source compiles. --- */










- (void)removeCredential:(NSURLCredential *)credential
       forProtectionSpace:(NSURLProtectionSpace *)protectionSpace
		 options:(NSDictionary *)options
{
	(void)options;	/* see the header: no option is honoured here, and saying so beats pretending */
	[self removeCredential:credential forProtectionSpace:protectionSpace];
}

- (void)removeCredential:(NSURLCredential *)credential
       forProtectionSpace:(NSURLProtectionSpace *)protectionSpace
		    options:(NSDictionary *)options
		       task:(id)task
{
	(void)options;
	(void)task;	/* no per-task credential registry exists here */
	[self removeCredential:credential forProtectionSpace:protectionSpace];
}
@end
