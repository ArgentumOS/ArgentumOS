/*
 * coregraphics_dataprovider — the callback forms, and whether the callbacks are really used.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE QUESTION THIS PROBE EXISTS TO ANSWER is the one that separates a real implementation from a
 * declaration: A CALLBACK PROVIDER'S CALLBACKS MUST ACTUALLY BE CALLED, and the bytes they hand over must be
 * the bytes the rest of the library reads. So every source below COUNTS its calls, and the checks are about
 * both the data and the count — a provider that stored a NULL and answered nothing would fail the first,
 * and one that ignored its callbacks and somehow had the bytes anyway would fail the second.
 *
 * THE TWO FAMILIES ARE DIFFERENT CONTRACTS AND ARE MEASURED SEPARATELY: a SEQUENTIAL source is a cursor, so
 * this library reads it once into its own buffer and calls `rewind` afterwards to leave the source usable;
 * a DIRECT source is random access, so a byte pointer is BORROWED for the provider's life (and returned at
 * release) and `getBytesAtPosition` is used when there is no pointer. Each of those is a call count here.
 */
#import <Foundation/Foundation.h>
#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGColor.h>
#include <CoreGraphics/CGDataConsumer.h>

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int failures;

/* --- a CONSUMER's sink: it takes what it is given, until it says it cannot ---------------------- */
struct counting_sink {
	char buf[64];
	size_t len;
	size_t room;	/* 0 means unlimited; otherwise the sink refuses past this */
	int released;
};

static size_t sink_put(void *info, const void *buffer, size_t count)
{
	struct counting_sink *sink = info;
	size_t take = count;

	if (sink->room != 0 && take > sink->room) {
		take = sink->room;	/* A FULL SINK ANSWERS SHORT, and that is the whole contract */
	}
	memcpy(sink->buf + sink->len, buffer, take);
	sink->len += take;
	sink->room -= take;
	return take;
}

static void sink_release(void *info)
{
	((struct counting_sink *)info)->released++;
}


static void check(const char *name, int ok)
{
	printf("CG-DATAPROVIDER %-56s %s\n", name, ok ? "ok" : "FAIL");
	if (!ok) {
		failures++;
	}
}

/* --- a SEQUENTIAL source: a cursor over a known string ------------------------------------------ */
struct seq_source {
	const char *text;
	size_t len;
	size_t cursor;
	int get_bytes_calls;
	int rewind_calls;
	int release_info_calls;
};

static size_t seq_get_bytes(void *info, void *buffer, size_t count)
{
	struct seq_source *s = info;
	size_t left = s->len - s->cursor;
	size_t take = count < left ? count : left;

	s->get_bytes_calls++;
	if (take == 0) {
		return 0;
	}
	memcpy(buffer, s->text + s->cursor, take);
	s->cursor += take;
	return take;
}

static void seq_rewind(void *info)
{
	struct seq_source *s = info;

	s->rewind_calls++;
	s->cursor = 0;
}

static void seq_release_info(void *info)
{
	struct seq_source *s = info;

	s->release_info_calls++;
}

/* --- a DIRECT source: random access, and it offers BOTH accessors so each path can be timed ----- */
struct dir_source {
	const char *text;
	size_t len;
	int byte_pointer_calls;
	int release_byte_pointer_calls;
	int at_position_calls;
	int release_info_calls;
	int short_reads;	/* when set, getBytesAtPosition stops halfway */
};

static const void *dir_get_byte_pointer(void *info)
{
	struct dir_source *s = info;

	s->byte_pointer_calls++;
	return s->text;
}

static void dir_release_byte_pointer(void *info, const void *pointer)
{
	struct dir_source *s = info;

	if (pointer == s->text) {
		s->release_byte_pointer_calls++;
	}
}

static size_t dir_get_bytes_at_position(void *info, void *buffer, off_t position, size_t count)
{
	struct dir_source *s = info;
	size_t want = s->short_reads ? (count + 1) / 2 : count;	/* ALWAYS >= 1: a zero-length answer means
									 * the end of the source, and this source is not at
									 * its end */

	s->at_position_calls++;
	if (position < 0 || (size_t)position >= s->len) {
		return 0;
	}
	if (want > s->len - (size_t)position) {
		want = s->len - (size_t)position;
	}
	memcpy(buffer, s->text + position, want);
	return want;
}

static int data_is(NSData *data, const char *want)
{
	return data != nil && [data length] == (NSUInteger)strlen(want)
	       && memcmp([data bytes], want, strlen(want)) == 0;
}

int main(void)
{
	const char *TEXT = "the bytes are the provider's";

	/* --- SEQUENTIAL -------------------------------------------------------------------- */
	{
		struct seq_source src;
		CGDataProviderSequentialCallbacks cb;
		CGDataProviderRef provider;
		NSData *data;

		memset(&src, 0, sizeof src);
		src.text = TEXT;
		src.len = strlen(TEXT);
		memset(&cb, 0, sizeof cb);
		cb.version = 0;
		cb.getBytes = seq_get_bytes;
		cb.rewind = seq_rewind;
		cb.releaseInfo = seq_release_info;

		provider = CGDataProviderCreateSequential(&src, &cb);
		check("a sequential provider is created", provider != NULL);
		check("...and nothing has been read from the source YET (the read is lazy)",
		      src.get_bytes_calls == 0);
		data = CGDataProviderCopyData(provider);
		check("CopyData returns the source's bytes", data_is(data, TEXT));
		check("...which means the callbacks were ACTUALLY CALLED", src.get_bytes_calls > 0);
		check("...and the source was rewound once, so the caller's source is usable",
		      src.rewind_calls == 1 && src.cursor == 0);
		{
			int before = src.get_bytes_calls;
			NSData *again = CGDataProviderCopyData(provider);

			/* THE CLAIM IS THAT THE SOURCE IS NOT READ AGAIN, and the instrument is A COUNT THAT DOES
			 * NOT GROW — not an absolute number: one materialisation of this source calls getBytes
			 * TWICE (once for the data, once to be told the source has ended, since a sequential
			 * source says "no more" by answering zero). The first version of this check asserted 1
			 * and failed against working code. */
			check("a second read gives the same bytes from the SAME buffer (read once, cached)",
			      data_is(again, TEXT) && src.get_bytes_calls == before);
			printf("CG-DATAPROVIDER %-56s again=%d bytes, calls before/after=%d/%d, cursor=%zu\n",
			       "...readout", (int)[again length], before, src.get_bytes_calls, src.cursor);
		}
		CGDataProviderRelease(provider);
		check("...and releasing the provider runs the source's releaseInfo",
		      src.release_info_calls == 1);
	}

	/* --- DIRECT, by byte pointer (the borrowed path) ------------------------------------ */
	{
		struct dir_source src;
		CGDataProviderDirectCallbacks cb;
		CGDataProviderRef provider;
		NSData *data;

		memset(&src, 0, sizeof src);
		src.text = TEXT;
		src.len = strlen(TEXT);
		memset(&cb, 0, sizeof cb);
		cb.version = 0;
		cb.getBytePointer = dir_get_byte_pointer;
		cb.releaseBytePointer = dir_release_byte_pointer;
		cb.getBytesAtPosition = dir_get_bytes_at_position;
		cb.releaseInfo = NULL;

		provider = CGDataProviderCreateDirect(&src, (off_t)src.len, &cb);
		check("a direct provider is created", provider != NULL);
		data = CGDataProviderCopyData(provider);
		check("CopyData returns the block the byte pointer named", data_is(data, TEXT));
		check("...by BORROWING it: the pointer callback ran once and getBytesAtPosition never",
		      src.byte_pointer_calls == 1 && src.at_position_calls == 0);
		CGDataProviderRelease(provider);
		check("...and the borrow was RETURNED at release", src.release_byte_pointer_calls == 1);
	}

	/* --- DIRECT, by position (no pointer, and a source that ends early) ----------------- */
	{
		struct dir_source src;
		CGDataProviderDirectCallbacks cb;
		CGDataProviderRef provider;
		NSData *data;

		memset(&src, 0, sizeof src);
		src.text = TEXT;
		src.len = strlen(TEXT);
		src.short_reads = 1;	/* returns half of what it is asked for, so the loop must iterate */
		memset(&cb, 0, sizeof cb);
		cb.version = 0;
		cb.getBytesAtPosition = dir_get_bytes_at_position;

		provider = CGDataProviderCreateDirect(&src, (off_t)src.len, &cb);
		data = CGDataProviderCopyData(provider);
		check("a direct source with no byte pointer is read by position, and the bytes are right",
		      data_is(data, TEXT));
		check("...which took MORE THAN ONE call, because the source answers short",
		      src.at_position_calls > 1);
		CGDataProviderRelease(provider);
	}

	/* --- AND WHAT IS REFUSED ----------------------------------------------------------- */
	{
		CGDataProviderSequentialCallbacks seq;
		CGDataProviderDirectCallbacks dir;

		memset(&seq, 0, sizeof seq);
		memset(&dir, 0, sizeof dir);
		check("a sequential provider with no getBytes is refused rather than made",
		      CGDataProviderCreateSequential(NULL, &seq) == NULL);
		check("...and one with no callbacks at all is refused too",
		      CGDataProviderCreateSequential(NULL, NULL) == NULL);
		check("a direct provider with NEITHER accessor is refused",
		      CGDataProviderCreateDirect(NULL, 4, &dir) == NULL);
		dir.getBytesAtPosition = dir_get_bytes_at_position;
		check("...and a negative size is refused even with one",
		      CGDataProviderCreateDirect(NULL, (off_t)-1, &dir) == NULL);
	}

	/* --- THE CONSUMER: the sink half, and what a full sink says --------------------------- */
	{
		struct counting_sink sink;
		CGDataConsumerCallbacks cb;
		CGDataConsumerRef consumer;

		memset(&sink, 0, sizeof sink);
		memset(&cb, 0, sizeof cb);
		cb.putBytes = sink_put;
		cb.releaseConsumer = sink_release;
		consumer = CGDataConsumerCreate(&sink, &cb);
		check("a callback consumer is created", consumer != NULL);
		check("...and it takes what its callback took",
		      CGDataConsumerPutBytes(consumer, "hello", 5) == 5 && sink.len == 5
		      && memcmp(sink.buf, "hello", 5) == 0);
		check("...and nothing has released the sink while the consumer lives", sink.released == 0);
		CGDataConsumerRelease(consumer);
		check("...and releasing it runs releaseConsumer", sink.released == 1);

		memset(&sink, 0, sizeof sink);
		sink.room = 3;	/* a sink that can only take three bytes */
		consumer = CGDataConsumerCreate(&sink, &cb);
		check("a FULL sink answers SHORT, and the count says how much landed",
		      CGDataConsumerPutBytes(consumer, "hello", 5) == 3 && sink.len == 3);
		CGDataConsumerRelease(consumer);
		check("a consumer with no putBytes is refused",
		      CGDataConsumerCreate(&sink, NULL) == NULL);
	}

	/* --- AND THE TWO FOUNDATION SINKS, over the same mechanism ---------------------------- */
	{
		NSMutableData *data = [[NSMutableData alloc] init];
		CGDataConsumerRef consumer = CGDataConsumerCreateWithCFData(data);

		check("the mutable-data consumer is created", consumer != NULL);
		CGDataConsumerPutBytes(consumer, TEXT, strlen(TEXT));
		check("...and the bytes are IN the data it appends to",
		      [data length] == strlen(TEXT) && memcmp([data bytes], TEXT, strlen(TEXT)) == 0);
		CGDataConsumerRelease(consumer);
		[data release];
	}

	{
		NSURL *url = [NSURL fileURLWithPath:@"/tmp/cg_consumer_probe.bin"];
		CGDataConsumerRef consumer = CGDataConsumerCreateWithURL(url);

		check("the file consumer is created", consumer != NULL);
		if (consumer != NULL) {
			CGDataProviderRef provider;

			CGDataConsumerPutBytes(consumer, TEXT, strlen(TEXT));
			CGDataConsumerRelease(consumer);
			/* WHAT WAS WRITTEN IS READ BACK BY A PROVIDER OVER THE SAME URL: one check that ties the
			 * two halves of the type together, and the only way to know the bytes reached the file. */
			provider = CGDataProviderCreateWithURL(url);
			check("...and a provider over the SAME URL reads back exactly what was written",
			      provider != NULL && data_is(CGDataProviderCopyData(provider), TEXT));
			CGDataProviderRelease(provider);
		}
		unlink("/tmp/cg_consumer_probe.bin");
		check("a non-file URL is refused by the file consumer (and would be by the provider too)",
		      CGDataConsumerCreateWithURL([NSURL URLWithString:@"http://example.com/x"]) == NULL);
		check("the consumer's type id is its own, and not zero",
		      CGDataConsumerGetTypeID() != (CGTypeID)0
		      && CGDataConsumerGetTypeID() != CGDataProviderGetTypeID());
	}

	/* --- THE CONSTANT COLORS: names in, colors out, and THE SAME COLOR TWICE --------------- */
	{
		CGColorRef white = CGColorGetConstantColor(kCGColorWhite);
		CGColorRef black = CGColorGetConstantColor(kCGColorBlack);
		CGColorRef clear = CGColorGetConstantColor(kCGColorClear);
		const CGFloat *w = white != NULL ? CGColorGetComponents(white) : NULL;
		const CGFloat *b = black != NULL ? CGColorGetComponents(black) : NULL;

		check("the three constant colors are made", white != NULL && black != NULL && clear != NULL);
		check("...and they are three DIFFERENT colors",
		      white != black && white != clear && black != clear);
		check("...with the alpha Apple's names imply: white and black opaque, clear not",
		      CGColorGetAlpha(white) == 1.0 && CGColorGetAlpha(black) == 1.0
		      && CGColorGetAlpha(clear) == 0.0);
		check("...and the gray components are 1 and 0, alpha last",
		      w != NULL && b != NULL && w[0] == 1.0 && b[0] == 0.0);
		check("...and the SAME object comes back, because a constant is not a copy",
		      CGColorGetConstantColor(kCGColorWhite) == white
		      && CGColorGetConstantColor(kCGColorClear) == clear);
		check("an unknown name gets NULL rather than a made-up color",
		      CGColorGetConstantColor(@"kCGColorPlum") == NULL && CGColorGetConstantColor(nil) == NULL);
	}

	printf("CG-DATAPROVIDER: %s\n", failures == 0 ? "all checks passed" : "FAILURES");
	return failures;
}
