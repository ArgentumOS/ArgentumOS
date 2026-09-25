/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_fileproviderservice, unit of 1 — W8 slice 9's acceptance. docs/design/foundation-plan.md §60.
 *
 * ONE unit, importing only <Foundation/Foundation.h>. NO FIXTURE AND NO TREE: the class's subject is a
 * subsystem this system does not have (a file provider extension, reached through XPC), so there is nothing
 * to build - and the probe's whole job is to check that the ABSENCE is answered rather than hidden.
 *
 *   provider-the-lookup-answers-an-empty-dictionary  the door Apple publishes FOR LOOKING is
 *                                          NSFileManager's, and its answer is empty AND CALLED: "no file
 *                                          provider service is registered for this item" is exactly what an
 *                                          extensionless system can say;
 *   provider-the-lookup-answers-for-a-missing-item-too  the answer does not depend on the item existing,
 *                                          because the reason there is no service is not the item;
 *   provider-a-service-can-be-made-and-its-name-is-empty  the ONE object a caller can hold is one it made,
 *                                          and its name is empty rather than missing;
 *   provider-the-connection-is-refused-by-name  "the custom communication channel" is an NSXPCConnection,
 *                                          and this system has no XPC - so the door answers nil WITH an
 *                                          error, which is the reason the class is worth declaring: a
 *                                          program can ASK, and finds out.
 */

#import <Foundation/Foundation.h>

#include <errno.h>
#include <stdio.h>

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-FILEPROVIDERSERVICE %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-FILEPROVIDERSERVICE %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static id fn_url(NSString *path)
{
	return [NSURL fileURLWithPath:path];
}

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSURL *item = fn_url(@"/System/Temporary Files/never-a-provider-item");
	NSURL *missing = fn_url(@"/System/Temporary Files/not-here-either");

	{
		__block NSUInteger calls = 0;
		__block BOOL empty = NO;
		__block BOOL errored = YES;

		[manager getFileProviderServicesForItemAtURL:item
				  completionHandler:^(NSDictionary *services, NSError *error) {
			calls++;
			empty = services != nil && [services count] == 0;
			errored = error != nil;
		}];
		check("provider-the-lookup-answers-an-empty-dictionary", calls == 1 && empty && !errored,
		      [NSString stringWithFormat:@"calls=%lu empty=%d error=%d", (unsigned long)calls,
			(int)empty, (int)errored]);
	}

	{
		__block NSUInteger calls = 0;
		__block BOOL empty = NO;
		__block BOOL errored = YES;

		[manager getFileProviderServicesForItemAtURL:missing
				  completionHandler:^(NSDictionary *services, NSError *error) {
			calls++;
			empty = services != nil && [services count] == 0;
			errored = error != nil;
		}];
		check("provider-the-lookup-answers-for-a-missing-item-too",
		      calls == 1 && empty && !errored,
		      [NSString stringWithFormat:@"calls=%lu empty=%d error=%d", (unsigned long)calls,
			(int)empty, (int)errored]);
	}

	{
		NSFileProviderService *service = [[NSFileProviderService alloc] init];

		check("provider-a-service-can-be-made-and-its-name-is-empty",
		      service != nil && [service name] != nil && [[service name] length] == 0,
		      [NSString stringWithFormat:@"name=%@", service != nil ? [service name] : (id)@"no service"]);
	}

	{
		NSFileProviderService *service = [[NSFileProviderService alloc] init];
		__block NSUInteger calls = 0;
		__block BOOL gotConnection = YES;
		__block BOOL errored = NO;

		[service getFileProviderConnectionWithCompletionHandler:
			^(NSXPCConnection *connection, NSError *error) {
			calls++;
			gotConnection = connection != nil;
			errored = error != nil && [error code] == ENOTSUP;
		}];
		check("provider-the-connection-is-refused-by-name",
		      calls == 1 && !gotConnection && errored,
		      [NSString stringWithFormat:@"calls=%lu connection=%d refused=%d", (unsigned long)calls,
			(int)gotConnection, (int)errored]);
	}

	printf("FOUNDATION-FILEPROVIDERSERVICE RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-FILEPROVIDERSERVICE DONE\n");
	return failc == 0 ? 0 : 1;
}
