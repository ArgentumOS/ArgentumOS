/*
 * THE DISCRIMINATOR: the same library method called TWICE, one argument different.
 *
 *   (1) completionHandler:nil  -> no block crosses the boundary at all
 *   (2) completionHandler:^... -> the same call with a block
 *
 * Compiled -fno-objc-arc. If (1) survives and (2) faults, the BLOCK ARGUMENT is the trigger; if (1)
 * faults too, it is the method itself (an inherited method dispatched on the subclass).
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

int main(void)
{
	NSURLRequest *request;
	NSURLSessionDataTask *task;
	__block BOOL called = NO;

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("FNBLOCK-DIAG a) alive (MRC build)\n");
	request = [NSURLRequest requestWithURL:[NSURL URLWithString:@"file:///tmp/x"]];
	printf("FNBLOCK-DIAG b) request made\n");

	printf("FNBLOCK-DIAG 1) about to init with a NIL handler\n");
	task = [[NSURLSessionDataTask alloc] fnInitWithRequest:request
						    identifier:1
					     completionHandler:nil];
	printf("FNBLOCK-DIAG 2) NIL-handler init SURVIVED: %s\n", task != nil ? "yes" : "no");

	printf("FNBLOCK-DIAG 3) about to init with a BLOCK\n");
	task = [[NSURLSessionDataTask alloc] fnInitWithRequest:request
						    identifier:2
					     completionHandler:^(NSData *data, NSURLResponse *response, NSError *error) {
		(void)data; (void)response; (void)error;
		called = YES;
	}];
	printf("FNBLOCK-DIAG 4) BLOCK-handler init SURVIVED: %s\n", task != nil ? "yes" : "no");
	printf("FNBLOCK-DIAG 5) the block %s captured\n", called ? "ran" : "was only stored");

	printf("FNBLOCK RESULT ok=0 fail=0\n");
	printf("FNBLOCK-STATUS=0\n");
	printf("FNBLOCK DONE\n");
	return 0;
}
