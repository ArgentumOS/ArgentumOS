/*
 * NSPortNameServer.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * ONE REGISTRY, SHARED BY THE THREE SUBCLASSES, and its rules are in the header. What is here is the table, the
 * host rule, and the subclasses — which are three names for one mechanism, so their implementations are a page
 * rather than three files that would say the same thing.
 *
 * THE ONE REFUSAL IS `NSSocketPortNameServer`'s, AND IT IS THE HONEST SPLIT OF THIS HALF OF THE FAMILY: a message
 * port and a mach port here are SOCKET PAIRS, so the far end is something this process holds and can hand out —
 * which is what makes an in-process registry work at all. A SOCKET port is an ADDRESS, and publishing one means
 * telling another process where to connect; serving that connection needs an accept path this library does not
 * have, and `NSConnection` is where it belongs.
 */

#import <Foundation/NSPortNameServer.h>
#import <Foundation/NSMessagePortNameServer.h>
#import <Foundation/NSSocketPortNameServer.h>
#import <Foundation/NSMachBootstrapServer.h>
#import <Foundation/NSPort.h>
#import <Foundation/NSMachPort.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSString.h>
#include <unistd.h>		/* gethostname: this machine's own name is a host this registry CAN answer for */

static NSMutableDictionary *gRegisteredNames = nil;

/* THE TABLE IS MADE ON FIRST USE rather than at load time: a program that never names a port should not carry it,
 * and a function called from the doors that need it is the only place that can decide that. */
static NSMutableDictionary *fn_name_registry(void)
{
	if (gRegisteredNames == nil) {
		gRegisteredNames = [[NSMutableDictionary alloc] init];
	}
	return gRegisteredNames;
}

/* THIS MACHINE'S OWN NAME, and an empty string when the kernel will not say — which makes the host check below
 * FAIL CLOSED rather than open. */
static NSString *fn_local_host_name(void)
{
	char buffer[256];

	if (gethostname(buffer, sizeof(buffer)) != 0) {
		return @"";
	}
	buffer[sizeof(buffer) - 1] = '\0';
	return [NSString stringWithUTF8String:buffer];
}

@implementation NSPortNameServer

+ (NSPortNameServer *)defaultPortNameServer
{
	return [NSMessagePortNameServer sharedInstance];
}

- (BOOL)registerPort:(NSPort *)port name:(NSString *)name
{
	if (port == nil || [name length] == 0) {
		return NO;
	}
	/* A NAME IS A KEY: the second registration replaces the first, which the header states as a rule. */
	[fn_name_registry() setObject:port forKey:name];
	return YES;
}

- (void)removePortForName:(NSString *)name
{
	if ([name length] == 0) {
		return;
	}
	[fn_name_registry() removeObjectForKey:name];
}

- (NSPort *)portForName:(NSString *)name
{
	if ([name length] == 0) {
		return nil;
	}
	return [fn_name_registry() objectForKey:name];
}

- (NSPort *)portForName:(NSString *)name host:(NSString *)host
{
	if (host == nil || [host length] == 0 || [host isEqual:@"localhost"] || [host isEqual:@"127.0.0.1"] ||
	    [host isEqual:fn_local_host_name()]) {
		return [self portForName:name];
	}
	return nil;		/* no mechanism here can ask another machine, so nil is the honest answer */
}


+ (NSPortNameServer *)systemDefaultPortNameServer
{
	/* APPLE'S OWN ALIAS of +defaultPortNameServer, kept because source calls it. */
	return [self defaultPortNameServer];
}
@end

/* ===================================================================================================
 * THE THREE NAMES FOR ONE MECHANISM
 * =================================================================================================== */

/* EVERY ONE OF THESE IS A SINGLETON, which is what Apple's are: a name server is a TABLE, and two tables that
 * disagreed would be a name that resolves differently depending on who asked. */
static NSMessagePortNameServer *gMessageServer = nil;
static NSSocketPortNameServer *gSocketServer = nil;
static NSMachBootstrapServer *gBootstrapServer = nil;

@implementation NSMessagePortNameServer

+ (NSMessagePortNameServer *)sharedInstance
{
	if (gMessageServer == nil) {
		gMessageServer = [[NSMessagePortNameServer alloc] init];
	}
	return gMessageServer;
}

@end

@implementation NSSocketPortNameServer

+ (NSSocketPortNameServer *)sharedInstance
{
	if (gSocketServer == nil) {
		gSocketServer = [[NSSocketPortNameServer alloc] init];
	}
	return gSocketServer;
}

- (BOOL)registerPort:(NSPort *)port name:(NSString *)name
{
	/* A SOCKET PORT IS AN ADDRESS RATHER THAN A PAIR, so there is no far end for this process to hand out and no
	 * accept path for a connection it would receive. THE DOOR REFUSES RATHER THAN STORING SOMETHING THAT CANNOT BE
	 * SENT TO — a registered name that threw on first use would be worse than a registration that says no. */
	if (![port isKindOfClass:[NSMachPort class]]) {
		return NO;
	}
	return [super registerPort:port name:name];
}


+ (unsigned short)defaultNameServerPortNumber
{
	/* THIS NAME SERVER IS IN-PROCESS, so there is no well-known remote port to answer — the number is ours and
	 * it is stated here rather than implied. */
	return 0;
}

- (NSPort *)portForName:(NSString *)name host:(NSString *)host nameServerPortNumber:(unsigned short)portNumber
{
	(void)portNumber;	/* see +defaultNameServerPortNumber: an in-process registry has nothing to address */
	return [self portForName:name host:host];
}

- (BOOL)registerPort:(NSPort *)port name:(NSString *)name nameServerPortNumber:(unsigned short)portNumber
{
	(void)portNumber;
	/* THROUGH THIS CLASS'S OWN DOOR, NOT super's: the door above REFUSES a socket port on purpose ("there is no
	 * far end for this process to hand out"), and calling super would store a name that cannot be sent to —
	 * exactly the state that refusal exists to prevent. */
	return [self registerPort:port name:name];
}
@end

@implementation NSMachBootstrapServer

+ (NSMachBootstrapServer *)sharedInstance
{
	if (gBootstrapServer == nil) {
		gBootstrapServer = [[NSMachBootstrapServer alloc] init];
	}
	return gBootstrapServer;
}

- (NSPort *)servicePortWithName:(NSString *)name
{
	/* APPLE'S DOOR FOR ASKING FOR A SERVICE by name, and here it is the registry lookup: this system has no
	 * bootstrap namespace to ask, which is why the substitution is stated in NSMachPort.h rather than implied. */
	return [self portForName:name];
}

@end
