/*
 * foundation_portnames.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE NAMING HALF OF THE PORT FAMILY (§62.54): `NSPortNameServer`, `NSMessagePort`, and the three subclasses that
 * are three names for one mechanism.
 *
 * WHAT A NAME SERVER IS FOR IS A NAME THAT FINDS SOMETHING, so almost every check here is a DELIVERY rather than a
 * lookup: a port is registered under a name, the name is resolved by a DIFFERENT door, and a message sent to what
 * the name answered arrives at the port that registered it. A registry that stored and returned objects while
 * nothing could reach them would pass every lookup check there is.
 *
 * THE RULES THE HEADERS STATE ARE THE RULES THIS PROBE MEASURES, because they are choices and not facts:
 * a second registration REPLACES the first (the second port's delegate receives, the first's does not); a host is
 * answerable only when it is this machine; an invalidated port takes its own name out; and a SOCKET port cannot be
 * published here at all — its registration answers NO, because publishing an address needs an accept path that
 * `NSConnection` owns and this library does not have yet.
 */
#import <Foundation/Foundation.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-PORTNAMES %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-PORTNAMES %s FAIL: %s\n", name, [why UTF8String]);
	}
}

/* PUMPING IN FIXED STEPS, for the reason the mach-port probe records: the guest's clock rounds a small sleep up
 * hard, so a step count is the same number of opportunities where a duration would be the clock's business. */
static void fnPumpSeconds(double seconds)
{
	int steps = (int)(seconds / 0.02);
	int i;

	for (i = 0; i < steps; i++) {
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
	}
}

static BOOL fnPumpUntilCount(NSArray *received, NSUInteger wanted, double seconds)
{
	int steps = (int)(seconds / 0.02);
	int i;

	for (i = 0; i < steps; i++) {
		if ([received count] >= wanted) {
			return YES;
		}
		[[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode
					 beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
	}
	return [received count] >= wanted;
}

static NSMutableArray *fnComponents(NSString *first, NSString *second)
{
	NSMutableArray *components = [[NSMutableArray alloc] init];
	NSData *one = [first dataUsingEncoding:NSUTF8StringEncoding];
	NSData *two = second != nil ? [second dataUsingEncoding:NSUTF8StringEncoding] : nil;

	if (one != nil) {
		[components addObject:one];
	}
	if (two != nil) {
		[components addObject:two];
	}
	return components;
}

/* ---- a delegate that records what it was handed -------------------------- */

@interface FnNamedDelegate : NSObject <NSPortDelegate>
{
	NSMutableArray *_received;
	NSString *_label;
}
- (instancetype)initWithLabel:(NSString *)label;
- (NSArray *)received;
- (void)handlePortMessage:(NSPortMessage *)message;
@end

@implementation FnNamedDelegate

- (instancetype)initWithLabel:(NSString *)label
{
	self = [super init];
	_received = [[NSMutableArray alloc] init];
	_label = [label copy];
	return self;
}

- (NSArray *)received { return _received; }

- (void)handlePortMessage:(NSPortMessage *)message
{
	NSMutableString *line = [[NSMutableString alloc] init];
	NSArray *components = [message components];
	NSUInteger i;

	[line appendFormat:@"%@:id=%u", _label, [message msgid]];
	for (i = 0; i < [components count]; i++) {
		id one = [components objectAtIndex:i];
		NSString *text = nil;

		if ([one isKindOfClass:[NSData class]]) {
			text = [[NSString alloc] initWithData:one encoding:NSUTF8StringEncoding];
		}
		[line appendFormat:@"|%@", text != nil ? text : @"(not data)"];
	}
	[_received addObject:line];
}

@end

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGPIPE, SIG_IGN);

	NSPortNameServer *server = [NSPortNameServer defaultPortNameServer];

	/* --- A NAMED PORT, AND THE SERVER THAT ANSWERS FOR IT ---------------------------------------------- */
	{
		NSMessagePort *named = [[NSMessagePort alloc] initWithName:@"probe.named"];
		NSMessagePort *nameless = [[NSMessagePort alloc] initWithName:@""];

		check("a-named-message-port-reports-its-name",
		      named != nil && [named isValid] && [[named name] isEqual:@"probe.named"] &&
		      [named machPort] != 0 && nameless == nil,
		      [NSString stringWithFormat:@"a port named \"%@\" (handle %u), and an empty name answered %@",
			[named name], [named machPort], nameless == nil ? @"nil" : @"a port"]);

		check("the-default-name-server-is-the-message-port-one",
		      server != nil && [server isKindOfClass:[NSMessagePortNameServer class]] &&
		      [NSMessagePortNameServer sharedInstance] == [NSMessagePortNameServer sharedInstance] &&
		      [NSMessagePortNameServer sharedInstance] == server,
		      @"the default server is the message-port server, and asking for it twice answers the same one");
	}

	/* --- A NAME THAT FINDS SOMETHING TO SEND TO --------------------------------------------------------- */
	{
		NSPort *found = [server portForName:@"probe.named"];
		NSPort *absent = [server portForName:@"probe.absent"];

		check("a-registered-name-finds-a-port-to-send-to",
		      found != nil && [found isValid] && absent == nil,
		      [NSString stringWithFormat:@"\"probe.named\" answered %@ and \"probe.absent\" answered %@",
			found != nil ? @"a port" : @"nil", absent == nil ? @"nil" : @"a port"]);
	}

	/* --- THE POINT OF THE WHOLE HALF: A MESSAGE THAT GOES TO A NAME ------------------------------------- */
	{
		NSMessagePort *service = [[NSMessagePort alloc] initWithName:@"probe.roundtrip"];
		FnNamedDelegate *delegate = [[FnNamedDelegate alloc] initWithLabel:@"service"];
		NSMutableArray *components = fnComponents(@"hello", @"name");
		NSPortMessage *message;
		NSPort *toService;
		BOOL sent;

		[service setDelegate:delegate];
		[service scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];

		toService = [server portForName:@"probe.roundtrip"];
		message = [[NSPortMessage alloc] initWithSendPort:toService receivePort:nil components:components];
		[message setMsgid:99];
		sent = [message sendBeforeDate:[NSDate date]];
		fnPumpUntilCount([delegate received], 1, 2.0);

		check("a-message-sent-to-a-name-arrives-at-the-port-that-registered-it",
		      sent && [[delegate received] count] == 1 &&
		      [[[delegate received] objectAtIndex:0] isEqual:@"service:id=99|hello|name"],
		      [NSString stringWithFormat:@"sent=%d and the service recorded: %@", (int)sent,
			[[delegate received] componentsJoinedByString:@"; "]]);

		/* REMOVING IS EXPLICIT, and it is the same door a caller would use to withdraw a name. */
		[server removePortForName:@"probe.roundtrip"];
		check("removing-a-name-stops-the-lookup",
		      [server portForName:@"probe.roundtrip"] == nil,
		      @"after -removePortForName: the name resolves to nothing");

		[service removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[service invalidate];
	}

	/* --- A SECOND REGISTRATION REPLACES THE FIRST, AND THAT IS MEASURED BY WHO RECEIVES ----------------- */
	{
		NSMessagePort *first = [[NSMessagePort alloc] initWithName:@"probe.replaced"];
		NSMessagePort *second = [[NSMessagePort alloc] initWithName:@"probe.replaced"];
		FnNamedDelegate *firstDelegate = [[FnNamedDelegate alloc] initWithLabel:@"first"];
		FnNamedDelegate *secondDelegate = [[FnNamedDelegate alloc] initWithLabel:@"second"];
		NSPort *toName;
		NSPortMessage *message;

		[first setDelegate:firstDelegate];
		[first scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[second setDelegate:secondDelegate];
		[second scheduleInRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];

		toName = [server portForName:@"probe.replaced"];
		message = [[NSPortMessage alloc] initWithSendPort:toName receivePort:nil
					       components:fnComponents(@"to", nil)];
		[message setMsgid:5];
		(void)[message sendBeforeDate:[NSDate date]];
		fnPumpSeconds(1.0);

		check("a-second-registration-replaces-the-first",
		      [[firstDelegate received] count] == 0 && [[secondDelegate received] count] == 1 &&
		      [[[secondDelegate received] objectAtIndex:0] isEqual:@"second:id=5|to"],
		      [NSString stringWithFormat:@"the first port recorded %lu message(s) and the second %lu — a name "
			@"is a key, so the later registration is the one it means", (unsigned long)[[firstDelegate received] count],
			(unsigned long)[[secondDelegate received] count]]);

		[first removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[second removeFromRunLoop:[NSRunLoop currentRunLoop] forMode:NSDefaultRunLoopMode];
		[first invalidate];
		[second invalidate];
	}

	/* --- A HOST, AND ONE REGISTRY BEHIND THREE NAMES ---------------------------------------------------- */
	{
		NSMessagePort *named = [[NSMessagePort alloc] initWithName:@"probe.host"];
		NSPort *local = [server portForName:@"probe.host" host:@"localhost"];
		NSPort *loopback = [server portForName:@"probe.host" host:@"127.0.0.1"];
		NSPort *elsewhere = [server portForName:@"probe.host" host:@"elsewhere.example"];
		NSPort *machine = [server portForName:@"probe.host" host:nil];

		check("a-host-lookup-answers-only-for-this-machine",
		      local != nil && loopback != nil && machine != nil && elsewhere == nil,
		      @"localhost, 127.0.0.1 and no host answer the port; another machine's name answers nil, because a "
		      @"process-local registry cannot ask one");

		check("the-three-servers-answer-from-one-registry",
		      [[NSSocketPortNameServer sharedInstance] portForName:@"probe.host"] == local &&
		      [[NSMachBootstrapServer sharedInstance] servicePortWithName:@"probe.host"] == local &&
		      [[NSSocketPortNameServer sharedInstance] portForName:@"probe.absent"] == nil,
		      @"the same name resolves through the socket server and through the bootstrap server's "
		      @"-servicePortWithName:, because there is one mechanism here and so one table");

		/* A SOCKET PORT CANNOT BE PUBLISHED, and the door says so instead of storing something unreachable. */
		{
			NSSocketPort *socket = [[NSSocketPort alloc] init];
			BOOL refused = ![[NSSocketPortNameServer sharedInstance] registerPort:socket name:@"probe.socket"];

			check("a-socket-port-registration-is-refused",
			      refused && [[NSSocketPortNameServer sharedInstance] portForName:@"probe.socket"] == nil,
			      @"publishing a socket port means handing another process an address to connect to, and this "
			      @"library has no accept path for the connection — NSConnection owns that, so the door answers NO");
			[socket invalidate];
		}

		/* AND AN INVALIDATED PORT TAKES ITS OWN NAME OUT, which is the same rule as the explicit removal. */
		[named invalidate];
		check("an-invalidated-port-forgets-its-own-name",
		      [server portForName:@"probe.host"] == nil,
		      @"a name that outlives its port would answer with a port nobody is reading");
	}

	printf("FOUNDATION-PORTNAMES RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-PORTNAMES-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-PORTNAMES DONE\n");
	return failc ? 1 : 0;
}
