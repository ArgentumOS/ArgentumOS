/*
 * foundation_websocket — THE WEBSOCKET VALUES: the message and the two enums (§59 slice 1).
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * NO SERVER, NO SOCKET, NO FRAMING. Slice 1 of §59 is the VALUE half of the WebSocket family, so this probe is
 * the shape NSURLError's was: build the values, ask them what they are, and assert the answers.
 *
 * WHAT IT PINS, AND WHY EACH ONE IS WORTH A CHECK:
 *   * THE EXCLUSIVITY RULE, which Apple's page states in words - "if initialized with data, the string property
 *     will be nil and vice versa". Both directions are asserted SEPARATELY because a plausible wrong
 *     implementation (a text message handing back its UTF-8 as `data`) fails only one of them;
 *   * THE COPY, which is OUR choice rather than Apple's (they do not say what happens to the object handed in)
 *     - asserted, because a choice nobody asserts is a choice nobody has;
 *   * THE TWO MESSAGE-TYPE VALUES (ours, D2) and THE THIRTEEN CLOSE CODES, twelve of which are RFC 6455 §7.4.1's
 *     and one of which (`...Invalid`) is not in the RFC at all and is therefore ours.
 *
 * ARC, like every probe.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-WEBSOCKET %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-WEBSOCKET %s FAIL: %s\n", name, [[why description] UTF8String]);
	}
}

/* EVERY CLOSE CODE THE RFC DEFINES, IN THE ORDER THE RFC LISTS THEM, so the check below is a table rather than a
 * pile of comparisons - and so the one exception is visibly not in it. */
static const struct {
	const char *name;
	NSInteger code;
	int inRFC;
} fn_codes[] = {
	{ "Invalid", 0, 0 },
	{ "NormalClosure", 1000, 1 },
	{ "GoingAway", 1001, 1 },
	{ "ProtocolError", 1002, 1 },
	{ "UnsupportedData", 1003, 1 },
	{ "NoStatusReceived", 1005, 1 },
	{ "AbnormalClosure", 1006, 1 },
	{ "InvalidFramePayloadData", 1007, 1 },
	{ "PolicyViolation", 1008, 1 },
	{ "MessageTooBig", 1009, 1 },
	{ "MandatoryExtensionMissing", 1010, 1 },
	{ "InternalServerError", 1011, 1 },
	{ "TLSHandshakeFailure", 1015, 1 },
};

int main(void)
{
	NSData *payload = [@"bytes" dataUsingEncoding:NSUTF8StringEncoding];
	NSMutableData *mutablePayload = [[NSMutableData alloc] initWithBytes:"abc" length:3];
	NSURLSessionWebSocketMessage *asData;
	NSURLSessionWebSocketMessage *asString;
	NSURLSessionWebSocketMessage *copyOfMutable;
	NSURLSessionWebSocketMessage *fromNil;
	NSString *told = nil;
	NSInteger wrong = 0;
	int i, fromRFC = 0;

	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE TWO MESSAGES ------------------------------------------------------------------------------ */
	asData = [[NSURLSessionWebSocketMessage alloc] initWithData:payload];
	asString = [[NSURLSessionWebSocketMessage alloc] initWithString:@"hello"];

	check("a-data-message-holds-data-and-no-string",
	      asData.type == NSURLSessionWebSocketMessageTypeData && [asData.data isEqualToData:payload] &&
	      asData.string == nil,
	      [NSString stringWithFormat:@"type=%ld string=%@", (long)asData.type, asData.string]);
	/* THE OTHER DIRECTION, AND THE ONE A PLAUSIBLE WRONG IMPLEMENTATION WOULD FAIL: a text message that also
	 * answered `data` with its UTF-8 bytes would pass the check above and fail this one. */
	check("a-string-message-holds-a-string-and-no-data",
	      asString.type == NSURLSessionWebSocketMessageTypeString &&
	      [asString.string isEqualToString:@"hello"] && asString.data == nil,
	      [NSString stringWithFormat:@"type=%ld data=%@", (long)asString.type, asString.data]);

	/* --- THE COPY, WHICH IS OUR CHOICE AND NOBODY ELSE'S ------------------------------------------------- */
	copyOfMutable = [[NSURLSessionWebSocketMessage alloc] initWithData:mutablePayload];
	[mutablePayload appendBytes:"def" length:3];
	told = [[NSString alloc] initWithData:copyOfMutable.data encoding:NSUTF8StringEncoding];
	check("the-message-copied-its-payload-rather-than-borrowing-it",
	      [copyOfMutable.data length] == 3 && [told isEqualToString:@"abc"],
	      [NSString stringWithFormat:@"the message now holds %@ after its source grew", copyOfMutable.data]);
	/* NO release CALLS ANYWHERE IN THIS PROBE: probes are ARC, the LIBRARY is MRC - and writing the library's
	 * ownership into a probe is not a style slip, it is a compile error ("ARC forbids explicit message send of
	 * 'release'"), which is how this file learned the difference. */

	/* AND A nil PAYLOAD IS REFUSED RATHER THAN KEPT, because a message with neither payload would make the
	 * exclusivity rule false - and the page gives two initialisers and no third. */
	fromNil = [[NSURLSessionWebSocketMessage alloc] initWithData:nil];
	check("and-a-nil-payload-makes-no-message", fromNil == nil,
	      @"a message must hold exactly one of data and string");

	/* --- THE TYPE VALUES (OURS, D2) --------------------------------------------------------------------- */
	check("the-two-message-types-are-the-values-we-chose",
	      NSURLSessionWebSocketMessageTypeData == 0 && NSURLSessionWebSocketMessageTypeString == 1,
	      [NSString stringWithFormat:@"data=%ld string=%ld",
	       (long)NSURLSessionWebSocketMessageTypeData, (long)NSURLSessionWebSocketMessageTypeString]);

	/* --- THE CLOSE CODES, AND THE ONE THAT IS NOT IN THE RFC -------------------------------------------- */
	for(i = 0; i < (int)(sizeof(fn_codes) / sizeof(fn_codes[0])); i++) {
		NSInteger held = 0;

		switch(i) {
		case 0: held = NSURLSessionWebSocketCloseCodeInvalid; break;
		case 1: held = NSURLSessionWebSocketCloseCodeNormalClosure; break;
		case 2: held = NSURLSessionWebSocketCloseCodeGoingAway; break;
		case 3: held = NSURLSessionWebSocketCloseCodeProtocolError; break;
		case 4: held = NSURLSessionWebSocketCloseCodeUnsupportedData; break;
		case 5: held = NSURLSessionWebSocketCloseCodeNoStatusReceived; break;
		case 6: held = NSURLSessionWebSocketCloseCodeAbnormalClosure; break;
		case 7: held = NSURLSessionWebSocketCloseCodeInvalidFramePayloadData; break;
		case 8: held = NSURLSessionWebSocketCloseCodePolicyViolation; break;
		case 9: held = NSURLSessionWebSocketCloseCodeMessageTooBig; break;
		case 10: held = NSURLSessionWebSocketCloseCodeMandatoryExtensionMissing; break;
		case 11: held = NSURLSessionWebSocketCloseCodeInternalServerError; break;
		case 12: held = NSURLSessionWebSocketCloseCodeTLSHandshakeFailure; break;
		}
		if(held == fn_codes[i].code) {
			fromRFC += fn_codes[i].inRFC;
		} else {
			wrong++;
			printf("FOUNDATION-WEBSOCKET-DIAG %s is %ld, the table says %ld\n",
			       fn_codes[i].name, (long)held, (long)fn_codes[i].code);
		}
	}
	/* TWELVE, AND THE COUNT IS THE CHECK: it says the table above was walked and matched, rather than that a
	 * spot check happened to pass on the two codes anybody would have written down. */
	check("the-twelve-closed-codes-are-the-rfcs", wrong == 0 && fromRFC == 12,
	      [NSString stringWithFormat:@"%d of 13 matched, %d from RFC 6455", 13 - wrong, fromRFC]);
	check("and-the-code-that-is-not-in-the-rfc-is-ours", NSURLSessionWebSocketCloseCodeInvalid == 0,
	      @"Apple publishes the name and not the value, so 0 is our choice (D2)");

	printf("FOUNDATION-WEBSOCKET RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-WEBSOCKET-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-WEBSOCKET DONE\n");
	return failc ? 1 : 0;
}
