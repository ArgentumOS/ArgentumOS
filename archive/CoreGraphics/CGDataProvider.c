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
};

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
	if (provider->owns_data) {
		free((void *)provider->data);
	} else if (provider->release != NULL) {
		provider->release(provider->info, provider->data, provider->size);
	}
	free(provider);
}

const void *cg_dataprovider_bytes(CGDataProviderRef provider, size_t *size)
{
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
