/*
 * CGDataConsumerFoundation — the two convenience sinks, where the object is the `info` pointer.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * BOTH ARE THE CALLBACK FORM WITH A FOUNDATION OBJECT BEHIND IT, which is the design: the mutable data one
 * appends, the file one writes, and neither adds anything to the contract `CGDataConsumer.h` states. What
 * they DO add is the lifetime rule — the object, or the file descriptor, belongs to the consumer and is let
 * go by `releaseConsumer` — and, for the file, the refusal of every scheme that is not a local file.
 *
 * THE PARTIAL WRITE IS THE BUG THIS FILE'S LOOP EXISTS TO PREVENT: `write(2)` returning less than it was
 * given is ordinary, and a sink that answered the full count after a short write would tell its caller that
 * bytes landed which never did. The callback answers WHAT IT WROTE and the caller decides what to do.
 */
#import <Foundation/Foundation.h>

#include <CoreGraphics/CGDataConsumer.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* --- the mutable-data sink ---------------------------------------------------------------- */

static size_t fn_mutable_put(void *info, const void *buffer, size_t count)
{
	[(NSMutableData *)info appendBytes:buffer length:count];
	return count;
}

static void fn_mutable_release(void *info)
{
	[(NSMutableData *)info release];
}

CGDataConsumerRef CGDataConsumerCreateWithCFData(NSMutableData *data)
{
	static const CGDataConsumerCallbacks callbacks = { fn_mutable_put, fn_mutable_release };
	CGDataConsumerRef consumer;

	if (data == nil) {
		fprintf(stderr, "CG-REFUSE: CGDataConsumerCreateWithCFData needs a mutable data to append "
				"to\n");
		return NULL;
	}
	consumer = CGDataConsumerCreate((void *)[data retain], &callbacks);
	if (consumer == NULL) {
		[data release];
	}
	return consumer;
}

/* --- the file sink ------------------------------------------------------------------------ */

struct fn_file_sink {
	int fd;
};

static size_t fn_file_put(void *info, const void *buffer, size_t count)
{
	struct fn_file_sink *sink = info;
	size_t done = 0;

	while (done < count) {
		ssize_t n = write(sink->fd, (const char *)buffer + done, count - done);

		if (n <= 0) {
			break;	/* a full disk or an error: the count says how much LANDED */
		}
		done += (size_t)n;
	}
	return done;
}

static void fn_file_release(void *info)
{
	struct fn_file_sink *sink = info;

	if (sink->fd >= 0) {
		close(sink->fd);
	}
	free(sink);
}

CGDataConsumerRef CGDataConsumerCreateWithURL(NSURL *url)
{
	static const CGDataConsumerCallbacks callbacks = { fn_file_put, fn_file_release };
	struct fn_file_sink *sink;
	CGDataConsumerRef consumer;

	if (url == nil || ![url isFileURL]) {
		fprintf(stderr, "CG-REFUSE: CGDataConsumerCreateWithURL writes FILE URLs only; this library "
				"writes local files, and the mutable-data form is what to use otherwise\n");
		return NULL;
	}
	sink = malloc(sizeof *sink);
	if (sink == NULL) {
		return NULL;
	}
	sink->fd = open([[url path] fileSystemRepresentation], O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (sink->fd < 0) {
		fprintf(stderr, "CG-REFUSE: CGDataConsumerCreateWithURL cannot write %s: %s\n",
			[[url path] UTF8String], strerror(errno));
		free(sink);
		return NULL;
	}
	consumer = CGDataConsumerCreate(sink, &callbacks);
	if (consumer == NULL) {
		close(sink->fd);
		free(sink);
	}
	return consumer;
}
