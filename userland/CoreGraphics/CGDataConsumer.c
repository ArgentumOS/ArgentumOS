/*
 * CGDataConsumer — the sink, and the one callback that defines it.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THIS FILE IS SMALL BECAUSE THE CONTRACT IS: a consumer holds an `info` pointer and a `putBytes`, and every
 * byte that goes anywhere goes through that one function. THE FOUNDATION-OBJECT FORMS ARE NOT HERE — they are
 * a mutable data's append and a file's write, both callbacks over this mechanism — so this file never sees
 * Foundation at all, and each half of the type lives where its language does.
 */
#include <CoreGraphics/CGDataConsumer.h>

#include <stdio.h>
#include <stdlib.h>

struct CGDataConsumer {
	int refcount;
	void *info;
	const CGDataConsumerCallbacks *callbacks;
};

CGDataConsumerRef CGDataConsumerCreate(void *info, const CGDataConsumerCallbacks *callbacks)
{
	CGDataConsumerRef consumer;

	if (callbacks == NULL || callbacks->putBytes == NULL) {
		fprintf(stderr, "CG-REFUSE: CGDataConsumerCreate needs callbacks, and a putBytes to take "
				"bytes with — a sink that cannot answer for them is worse than one never made\n");
		return NULL;
	}
	consumer = calloc(1, sizeof(struct CGDataConsumer));
	if (consumer == NULL) {
		return NULL;
	}
	consumer->refcount = 1;
	consumer->info = info;
	consumer->callbacks = callbacks;
	return consumer;
}

size_t CGDataConsumerPutBytes(CGDataConsumerRef consumer, const void *buffer, size_t count)
{
	if (consumer == NULL || buffer == NULL) {
		return 0;
	}
	/* THE CALLBACK'S ANSWER IS THE ANSWER, including zero: this door does not retry, does not split the
	 * write and does not decide a short write was probably fine. A caller with more to write calls again
	 * with what is left, which is what makes a full sink visible. */
	return consumer->callbacks->putBytes(consumer->info, buffer, count);
}

CGDataConsumerRef CGDataConsumerRetain(CGDataConsumerRef consumer)
{
	if (consumer != NULL) {
		consumer->refcount++;
	}
	return consumer;
}

void CGDataConsumerRelease(CGDataConsumerRef consumer)
{
	if (consumer == NULL) {
		return;
	}
	if (--consumer->refcount > 0) {
		return;
	}
	/* THE CALLER'S `releaseConsumer` RUNS LAST, so an info that owns what the sink wrote to still has it
	 * while the consumer itself goes away. */
	if (consumer->callbacks->releaseConsumer != NULL) {
		consumer->callbacks->releaseConsumer(consumer->info);
	}
	free(consumer);
}
