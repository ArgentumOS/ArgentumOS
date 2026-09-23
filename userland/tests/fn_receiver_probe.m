/*
 * THE RECEIVER, ALONE. Three rounds of full-probe runs could not tell "the receiver never starts" from
 * "something before my check blocks", because the probe does both and the run only says it died. This does
 * ONE thing and prints as it goes, so the harness window cannot hide the answer.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

int main(void)
{
	NSString *capture = @"/System/Temporary Files/fn_receiver_probe.bin";

	setvbuf(stdout, NULL, _IONBF, 0);
	printf("FNRCV-DIAG 1) alive\n");

	unlink("/System/Temporary Files/fn_receiver_probe.bin");
	/* NOT system(): MEASURED - it spawns /bin/sh -c and NEVER RETURNS in this guest, which is what made
	 * three rounds of this look like a receiver problem. fork()+execv() has nothing to wait on, and the
	 * redirects happen before the exec so the receiver's stdin is the FSH's null device, not the console. */
	printf("FNRCV-DIAG 2) forking the receiver\n");
	{
		pid_t pid = fork();

		if (pid == 0) {
			char *argv[5];
			int nul, out;

			argv[0] = "/System/Tools/netcat";
			argv[1] = "-l";
			argv[2] = "127.0.0.1";
			argv[3] = "46467";
			argv[4] = NULL;
			if ((nul = open("/System/Devices/null", O_RDONLY)) >= 0) { dup2(nul, 0); close(nul); }
			if ((out = open([capture UTF8String], O_WRONLY|O_CREAT|O_TRUNC, 0644)) >= 0) {
				dup2(out, 1);
				close(out);
			}
			execv("/System/Tools/netcat", argv);
			_exit(127);
		}
		printf("FNRCV-DIAG 3) fork returned %d\n", (int)pid);
	}
	usleep(500000);

	{
		NSData *now = [NSData dataWithContentsOfFile:capture];

		printf("FNRCV-DIAG 4) capture after half a second: %s\n",
		       now != nil ? "EXISTS (the receiver is waiting)" : "absent");
	}

	/* AND A CLIENT, SO THE RECEIVER HAS SOMETHING TO RECEIVE: the question is whether it is LISTENING,
	 * not whether it writes a file nobody connected to. */
	{
		NSURLSessionConfiguration *configuration = [NSURLSessionConfiguration defaultSessionConfiguration];
		NSURLSession *session = [NSURLSession sessionWithConfiguration:configuration];
		NSMutableURLRequest *request =
			[NSMutableURLRequest requestWithURL:[NSURL URLWithString:@"http://127.0.0.1:46467/"]];
		__block BOOL called = NO;
		NSURLSessionDataTask *task;
		int waited = 0;

		[request setHTTPMethod:@"POST"];
		[request setTimeoutInterval:3.0];
		/* THE BODY IS ON THE REQUEST, so this needs no upload class: this probe is about the RECEIVER,
		 * and the bridge sends any request's HTTPBody. */
		[request setHTTPBody:[NSData dataWithBytes:"PROBE-BODY-0123456789" length:21]];
		[NSURLProtocol registerClass:[FNCURLURLProtocol class]];
		task = [session dataTaskWithRequest:request
			    completionHandler:^(NSData *d, NSURLResponse *r, NSError *e) {
			(void)d; (void)r; (void)e;
			called = YES;
		}];
		printf("FNRCV-DIAG 5) task made, resuming\n");
		[task resume];
		while (!called && waited < 100) {
			usleep(100000);
			waited++;
		}
		printf("FNRCV-DIAG 6) upload ending: %s after %d tenths\n", called ? "arrived" : "NEVER", waited);
	}

	{
		NSData *after = [NSData dataWithContentsOfFile:capture];

		printf("FNRCV-DIAG 7) capture at the end: %s (%d bytes)\n",
		       after != nil ? "EXISTS" : "absent", after != nil ? (int)[after length] : -1);
	}

	printf("FNRCV RESULT ok=0 fail=0\n");
	printf("FNRCV-STATUS=0\n");
	printf("FNRCV DONE\n");
	return 0;
}
