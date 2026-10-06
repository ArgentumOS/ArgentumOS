/*
 * CGDataProvider — the bytes, and who lets go of them.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
#include <CoreGraphics/CGDataProvider.h>
#include <CoreGraphics/CGDataProvider_internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

struct CGDataProvider {
	int refcount;
	const void *data;
	size_t size;
	void *info;
	CGDataProviderReleaseDataCallback release;
	/* SET WHEN THIS OBJECT ALLOCATED THE BYTES ITSELF — the filename form — so that releasing
	 * frees them instead of calling a callback the caller never gave. THE TWO FORMS ARE NOT
	 * THE SAME CONTRACT and the flag is what keeps them apart: adopting a caller's static array
	 * and freeing it would be a crash in somebody else's memory. */
	int owns_data;
	/* THE CALLBACK FORMS, and at most one is set — plus the flag that says a direct callbacks' byte
	 * pointer is OUTSTANDING, so releasing the provider returns it instead of leaking the caller's
	 * block. See the header for what each family promises. */
	const CGDataProviderSequentialCallbacks *sequential;
	const CGDataProviderDirectCallbacks *direct;
	int borrowed_byte_pointer;
};

/* THE MATERIALISATION, ONCE: a callback provider has no flat buffer, and this is where one is made —
 * borrowed from a direct source that offers a byte pointer, or read into a buffer this provider owns.
 * CALLED FROM `cg_dataprovider_bytes` AND NOWHERE ELSE, so every reader in the library (the decoders,
 * `CGDataProviderCopyData`) goes through it without knowing which kind of provider it has. */
static void fn_materialise(CGDataProviderRef provider);

CGDataProviderRef CGDataProviderCreateWithData(void *info, const void *data, size_t size,
					       CGDataProviderReleaseDataCallback releaseData)
{
	CGDataProviderRef provider;

	if (data == NULL) {
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateWithData needs data — a provider "
				"with no bytes cannot answer the one question it exists for\n");
		return NULL;
	}
	provider = calloc(1, sizeof(struct CGDataProvider));
	if (provider == NULL) {
		return NULL;
	}
	provider->refcount = 1;
	provider->data = data;
	provider->size = size;
	provider->info = info;
	provider->release = releaseData;
	provider->owns_data = 0;
	return provider;
}

CGDataProviderRef CGDataProviderCreateWithFilename(const char *filename)
{
	CGDataProviderRef provider;
	unsigned char *buf;
	FILE *f;
	long len;
	size_t got;

	if (filename == NULL) {
		return NULL;
	}
	f = fopen(filename, "rb");
	if (f == NULL) {
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateWithFilename cannot open %s\n",
			filename);
		return NULL;
	}
	if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateWithFilename cannot size %s\n",
			filename);
		fclose(f);
		return NULL;
	}
	/* ONE BYTE MORE THAN THE FILE, so that a zero-length file still yields a pointer worth
	 * having: a provider whose data is NULL is the case the constructor above refuses, and an
	 * empty profile should be refused by the PARSER rather than by this. */
	buf = malloc((size_t)len + 1);
	if (buf == NULL) {
		fclose(f);
		return NULL;
	}
	got = fread(buf, 1, (size_t)len, f);
	fclose(f);
	if (got != (size_t)len) {
		/* A SHORT READ IS A FAILURE AND NOT A SMALLER PROVIDER: a truncated profile parses as
		 * a different profile, which is a wrong answer rather than a missing one. */
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateWithFilename read %lu of %ld bytes "
				"from %s\n", (unsigned long)got, len, filename);
		free(buf);
		return NULL;
	}
	provider = calloc(1, sizeof(struct CGDataProvider));
	if (provider == NULL) {
		free(buf);
		return NULL;
	}
	provider->refcount = 1;
	provider->data = buf;
	provider->size = (size_t)len;
	provider->owns_data = 1;
	return provider;
}

CGDataProviderRef CGDataProviderRetain(CGDataProviderRef provider)
{
	if (provider != NULL) {
		provider->refcount++;
	}
	return provider;
}

void CGDataProviderRelease(CGDataProviderRef provider)
{
	if (provider == NULL) {
		return;
	}
	if (--provider->refcount > 0) {
		return;
	}
	/* THE CALLBACK FORMS FIRST, in the order the contract implies: the borrowed byte pointer goes back
	 * to whoever lent it, the buffer this provider read is freed, and only then does the caller's
	 * `releaseInfo` run — an info that owns the source must outlive every use of it. */
	if (provider->sequential != NULL || provider->direct != NULL) {
		if (provider->borrowed_byte_pointer && provider->direct != NULL
		    && provider->direct->releaseBytePointer != NULL) {
			provider->direct->releaseBytePointer(provider->info, provider->data);
		}
		if (provider->owns_data) {
			free((void *)provider->data);
		}
		if (provider->sequential != NULL && provider->sequential->releaseInfo != NULL) {
			provider->sequential->releaseInfo(provider->info);
		}
		if (provider->direct != NULL && provider->direct->releaseInfo != NULL) {
			provider->direct->releaseInfo(provider->info);
		}
		free(provider);
		return;
	}
	if (provider->owns_data) {
		free((void *)provider->data);
	} else if (provider->release != NULL) {
		provider->release(provider->info, provider->data, provider->size);
	}
	free(provider);
}

const void *cg_dataprovider_bytes(CGDataProviderRef provider, size_t *size)
{
	/* A CALLBACK PROVIDER HAS NO BUFFER UNTIL SOMEBODY ASKS, and this is the only place that asks: every
	 * reader in the library comes through here, so none of them has to know which kind it has. */
	if (provider != NULL && provider->data == NULL
	    && (provider->sequential != NULL || provider->direct != NULL)) {
		fn_materialise(provider);
	}
	if (provider == NULL) {
		if (size != NULL) {
			*size = 0;
		}
		return NULL;
	}
	if (size != NULL) {
		*size = provider->size;
	}
	return provider->data;
}

/* ------------------------------------------------------------------------- */
/* The callback forms                                                        */
/* ------------------------------------------------------------------------- */

CGDataProviderRef CGDataProviderCreateSequential(void *info,
						 const CGDataProviderSequentialCallbacks *callbacks)
{
	CGDataProviderRef provider;

	if (callbacks == NULL || callbacks->getBytes == NULL) {
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateSequential needs callbacks, and a getBytes "
				"to read with — a provider that cannot answer for its bytes is worse than one "
				"that was never made\n");
		return NULL;
	}
	provider = calloc(1, sizeof(struct CGDataProvider));
	if (provider == NULL) {
		return NULL;
	}
	provider->refcount = 1;
	provider->info = info;
	provider->sequential = callbacks;
	return provider;
}

CGDataProviderRef CGDataProviderCreateDirect(void *info, off_t size,
					     const CGDataProviderDirectCallbacks *callbacks)
{
	CGDataProviderRef provider;

	if (callbacks == NULL || size < 0
	    || (callbacks->getBytePointer == NULL && callbacks->getBytesAtPosition == NULL)) {
		fprintf(stderr, "CG-REFUSE: CGDataProviderCreateDirect needs a non-negative size and a way "
				"to reach the bytes (a byte pointer or getBytesAtPosition)\n");
		return NULL;
	}
	provider = calloc(1, sizeof(struct CGDataProvider));
	if (provider == NULL) {
		return NULL;
	}
	provider->refcount = 1;
	provider->info = info;
	provider->direct = callbacks;
	provider->size = (size_t)size;
	return provider;
}

/* THE ONE PLACE A CALLBACK PROVIDER BECOMES A BUFFER, and it happens at most once: the second reader of a
 * sequential source would otherwise read nothing, because the source's cursor has already passed the end. */
static void fn_materialise(CGDataProviderRef provider)
{
	if (provider->direct != NULL) {
		if (provider->direct->getBytePointer != NULL) {
			const void *p = provider->direct->getBytePointer(provider->info);

			if (p != NULL) {
				provider->data = p;
				provider->borrowed_byte_pointer = 1;
				return;
			}
		}
		{
			unsigned char *buf = malloc(provider->size > 0 ? provider->size : 1);
			size_t done = 0;

			if (buf == NULL) {
				return;
			}
			while (done < provider->size) {
				size_t got = provider->direct->getBytesAtPosition(provider->info, buf + done,
										  (off_t)done,
										  provider->size - done);

				if (got == 0) {
					break;	/* the source ended early: the length is the honest one */
				}
				done += got;
			}
			provider->data = buf;
			provider->size = done;
			provider->owns_data = 1;
		}
		return;
	}
	if (provider->sequential != NULL) {
		size_t cap = 4096, done = 0;
		unsigned char *buf = malloc(cap);

		if (buf == NULL) {
			return;
		}
		for (;;) {
			size_t got;

			if (done == cap) {
				unsigned char *grown = realloc(buf, cap * 2);

				if (grown == NULL) {
					break;
				}
				buf = grown;
				cap *= 2;
			}
			got = provider->sequential->getBytes(provider->info, buf + done, cap - done);
			if (got == 0) {
				break;
			}
			done += got;
		}
		/* THE SOURCE IS LEFT USABLE, which is what `rewind` is for: this library read it to its end. */
		if (provider->sequential->rewind != NULL) {
			provider->sequential->rewind(provider->info);
		}
		provider->data = buf;
		provider->size = done;
		provider->owns_data = 1;
	}
}
