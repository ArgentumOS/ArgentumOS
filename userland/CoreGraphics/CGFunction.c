/*
 * CGFunction — the caller's callbacks, the contract around them, and nothing else.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THIS OBJECT OWNS NOTHING OF THE CALLER'S. The `info` pointer is theirs, the callbacks are theirs,
 * and `releaseInfo` is the one place that hands `info` back — which is exactly Apple's arrangement
 * and the reason `releaseInfo` exists rather than this library inventing a free: only the caller
 * knows how their info was made. What this file does own is the DOMAIN and RANGE arrays, COPIED out
 * of the caller's argument so that a caller may pass a stack array and forget it.
 *
 * THE DEFAULT DOMAIN IS 0…1 AND THE DEFAULT RANGE IS UNBOUNDED, and the difference between those two
 * defaults is not symmetry: a missing domain has an obvious reading (a function of one parameter over
 * the unit interval, which is what a shading always is), while a missing range has none — there is no
 * interval to invent — so absent means "do not clamp" and the caller's numbers are passed through.
 */
#include <CoreGraphics/CGFunction.h>
#include <CoreGraphics/CGFunction_internal.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct CGFunction {
	int refcount;
	void *info;
	size_t domain_dimension;
	CGFloat *domain;              /* 2 * domain_dimension pairs, or NULL for 0…1 everywhere */
	size_t range_dimension;
	CGFloat *range;               /* 2 * range_dimension pairs, or NULL for unbounded */
	CGFunctionCallbacks callbacks;
};

CGFunctionRef CGFunctionCreate(void *info, size_t domainDimension, const CGFloat *domain,
			       size_t rangeDimension, const CGFloat *range,
			       const CGFunctionCallbacks *callbacks)
{
	CGFunctionRef function;
	size_t i;

	if (callbacks == NULL) {
		fprintf(stderr, "CG-REFUSE: CGFunctionCreate needs callbacks; without an evaluate "
				"there is no function to call\n");
		return NULL;
	}
	/* THE VERSION IS CHECKED BEFORE ANYTHING IS READ FROM THE STRUCT, which is the whole point of
	 * having one: a struct from a call site compiled against a newer header is a struct whose
	 * fields are not where this code thinks they are, and reading it is the failure the version
	 * number exists to prevent. */
	if (callbacks->version != 0) {
		fprintf(stderr, "CG-REFUSE: CGFunctionCreate was given callbacks of version %u; this "
				"library knows version 0\n", callbacks->version);
		return NULL;
	}
	if (callbacks->evaluate == NULL) {
		fprintf(stderr, "CG-REFUSE: CGFunctionCreate needs an evaluate callback\n");
		return NULL;
	}
	if (domainDimension == 0 || rangeDimension == 0) {
		fprintf(stderr, "CG-REFUSE: CGFunctionCreate needs a non-zero dimension on both "
				"sides\n");
		return NULL;
	}
	if (domain != NULL) {
		for (i = 0; i < domainDimension; i++) {
			if (domain[2 * i] > domain[2 * i + 1]) {
				fprintf(stderr, "CG-REFUSE: CGFunctionCreate domain entry %lu has its "
						"low above its high\n", (unsigned long)i);
				return NULL;
			}
		}
	}
	if (range != NULL) {
		for (i = 0; i < rangeDimension; i++) {
			if (range[2 * i] > range[2 * i + 1]) {
				fprintf(stderr, "CG-REFUSE: CGFunctionCreate range entry %lu has its "
						"low above its high\n", (unsigned long)i);
				return NULL;
			}
		}
	}
	function = calloc(1, sizeof(struct CGFunction));
	if (function == NULL) {
		return NULL;
	}
	function->refcount = 1;
	function->info = info;
	function->domain_dimension = domainDimension;
	function->range_dimension = rangeDimension;
	function->callbacks = *callbacks;
	if (domain != NULL) {
		function->domain = calloc(2 * domainDimension, sizeof(CGFloat));
		if (function->domain == NULL) {
			free(function);
			return NULL;
		}
		memcpy(function->domain, domain, 2 * domainDimension * sizeof(CGFloat));
	}
	if (range != NULL) {
		function->range = calloc(2 * rangeDimension, sizeof(CGFloat));
		if (function->range == NULL) {
			free(function->domain);
			free(function);
			return NULL;
		}
		memcpy(function->range, range, 2 * rangeDimension * sizeof(CGFloat));
	}
	return function;
}

CGFunctionRef CGFunctionRetain(CGFunctionRef function)
{
	if (function != NULL) {
		function->refcount++;
	}
	return function;
}

void CGFunctionRelease(CGFunctionRef function)
{
	if (function == NULL) {
		return;
	}
	if (--function->refcount > 0) {
		return;
	}
	/* THE CALLER'S `info` GOES BACK THROUGH THEIR OWN CALLBACK, exactly once, and only when the last
	 * reference goes — which is what makes a function safe to hand to two shadings. */
	if (function->callbacks.releaseInfo != NULL) {
		function->callbacks.releaseInfo(function->info);
	}
	free(function->range);
	free(function->domain);
	free(function);
}

size_t cg_function_domain_dimension(CGFunctionRef function)
{
	return function == NULL ? 0 : function->domain_dimension;
}

size_t cg_function_range_dimension(CGFunctionRef function)
{
	return function == NULL ? 0 : function->range_dimension;
}

void cg_function_evaluate(CGFunctionRef function, const CGFloat *in, CGFloat *out)
{
	size_t i;

	if (function == NULL || function->callbacks.evaluate == NULL) {
		return;
	}
	function->callbacks.evaluate(function->info, in, out);
	if (function->range == NULL) {
		return;
	}
	/* THE CLAMP IS APPLIED HERE AND NOT IN THE CALLBACK, so that every caller of every function gets
	 * it: a shading that clamped for itself would paint a different picture from one that did not,
	 * and the caller's own function would have to know which of them it was talking to. */
	for (i = 0; i < function->range_dimension; i++) {
		if (out[i] < function->range[2 * i]) {
			out[i] = function->range[2 * i];
		} else if (out[i] > function->range[2 * i + 1]) {
			out[i] = function->range[2 * i + 1];
		}
	}
}
