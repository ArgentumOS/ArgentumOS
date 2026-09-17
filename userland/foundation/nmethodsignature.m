/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * nmethodsignature.m — the type-encoding parser.
 *
 * ARC file: it owns a C buffer and implements no -retain/-release.
 */

#import <foundation/NSMethodSignature.h>
#import <foundation/NSString.h>
#import <foundation/NSException.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ grammar */

/* The qualifiers that may precede a type. V is the odd one out — it is the
 * oneway marker, so it is RECORDED rather than skipped. */
static int fn_is_qualifier(char c)
{
	return c == 'r' || c == 'n' || c == 'N' || c == 'o' || c == 'O' ||
	       c == 'R' || c == 'V' || c == 'A';
}

static int fn_is_digit(char c)
{
	return c >= '0' && c <= '9';
}

static unsigned long fn_number(const char **p)
{
	unsigned long value = 0;

	while (fn_is_digit(**p)) {
		value = value * 10 + (unsigned long)(**p - '0');
		(*p)++;
	}
	return value;
}

/* The type proper, past the frame OFFSET the older encoding form writes between
 * types (`"@16@0:8"`): a number there is an offset, never a type. */
static const char *fn_type_start(const char *p)
{
	while (fn_is_digit(*p)) {
		p++;
	}
	return p;
}

/* A field's quoted name, if the encoding carries one. */
static const char *fn_skip_field_name(const char *q)
{
	while (*q == '=' || *q == '"') {
		if (*q == '=') {
			q++;
			continue;
		}
		q++;
		while (*q != '\0' && *q != '"') {
			q++;
		}
		if (*q == '"') {
			q++;
		}
	}
	return q;
}

/* Advance past ONE complete type. Every branch advances at least one byte, so a
 * malformed encoding cannot make this loop forever; `oneway` (optional) records
 * whether this type carried the V marker. */
static const char *fn_skip_type(const char *p, int *oneway)
{
	if (p == NULL) {
		return NULL;
	}
	while (fn_is_qualifier(*p)) {
		if (*p == 'V' && oneway != NULL) {
			*oneway = 1;
		}
		p++;
	}
	p = fn_type_start(p);		/* past a frame offset, if one is written */
	switch (*p) {
	case '\0':
		return p;
	case '^':			/* a pointer, then ONE more type */
		return fn_skip_type(p + 1, NULL);
	case '[':			/* [count type] */
		p++;
		(void)fn_number(&p);
		p = fn_skip_type(p, NULL);
		return (*p == ']') ? p + 1 : p;
	case '{':			/* {name=fields} */
	case '(':			/* (name=fields) */
	{
		char close = (*p == '{') ? '}' : ')';
		const char *q = p + 1;

		while (*q != '\0' && *q != '=' && *q != close) {
			q++;		/* the bare struct name */
		}
		if (*q == '=') {
			q++;
		}
		for (;;) {
			const char *next;

			q = fn_skip_field_name(q);
			if (*q == '\0' || *q == close) {
				break;
			}
			next = fn_skip_type(q, NULL);
			if (next == NULL || next <= q) {
				return NULL;	/* malformed: refuse rather than spin */
			}
			q = next;
		}
		return (*q == close) ? q + 1 : q;
	}
	case '@':			/* id, @"Class", or @? (a block) */
		p++;
		if (*p == '?') {
			return p + 1;
		}
		if (*p == '"') {
			p++;
			while (*p != '\0' && *p != '"') {
				p++;
			}
			return (*p == '"') ? p + 1 : p;
		}
		return p;
	case 'b':			/* a bitfield: b + digits */
		p++;
		(void)fn_number(&p);
		return p;
	case '!':			/* a vector: kept opaque */
		return p + 1;
	default:
		return p + 1;		/* one primitive code */
	}
}

/* The width and natural alignment of one type, in bytes, for THIS target. */
static void fn_type_metrics(const char *p, size_t *size, size_t *align)
{
	*size = 0;
	*align = 1;
	if (p == NULL) {
		return;
	}
	while (fn_is_qualifier(*p)) {
		p++;
	}
	p = fn_type_start(p);
	switch (*p) {
	case 'c': case 'C': case 'B':
		*size = 1;
		return;
	case 's': case 'S':
		*size = 2;
		*align = 2;
		return;
	case 'i': case 'I': case 'f':
		*size = 4;
		*align = 4;
		return;
	case 'l': case 'L': case 'q': case 'Q': case 'd':
		*size = 8;
		*align = 8;
		return;
	case 'D':
		*size = 16;
		*align = 16;
		return;
	case '*': case '@': case '#': case ':': case '^': case '?':
		*size = sizeof(void *);
		*align = sizeof(void *);
		return;
	case 'b':
		p++;
		*size = (size_t)((fn_number(&p) + 7) / 8);
		return;
	case '[':
	{
		size_t inner = 0, innerAlign = 1;

		p++;
		{
			unsigned long count = fn_number(&p);

			fn_type_metrics(p, &inner, &innerAlign);
			*size = (size_t)count * inner;
		}
		*align = innerAlign;
		return;
	}
	case '{': case '(':
	{
		char close = (*p == '{') ? '}' : ')';
		const char *q = p + 1;
		size_t total = 0, maxAlign = 1;
		int isStruct = (*p == '{');

		while (*q != '\0' && *q != '=' && *q != close) {
			q++;
		}
		if (*q == '=') {
			q++;
		}
		while (*q != '\0' && *q != close) {
			size_t fieldSize = 0, fieldAlign = 1;
			const char *next;

			q = fn_skip_field_name(q);
			if (*q == '\0' || *q == close) {
				break;
			}
			fn_type_metrics(q, &fieldSize, &fieldAlign);
			if (fieldAlign > maxAlign) {
				maxAlign = fieldAlign;
			}
			if (isStruct) {
				total = (total + fieldAlign - 1) / fieldAlign * fieldAlign;
				total += fieldSize;
			} else if (fieldSize > total) {
				total = fieldSize;
			}
			next = fn_skip_type(q, NULL);
			if (next == NULL || next <= q) {
				break;
			}
			q = next;
		}
		if (isStruct && maxAlign > 1) {
			total = (total + maxAlign - 1) / maxAlign * maxAlign;
		}
		*size = total;
		*align = maxAlign;
		return;
	}
	default:
		return;			/* void, or unknown: 0 bytes */
	}
}

/* One type as its own NUL-terminated string, which is what the accessors answer:
 * a pointer into the whole encoding would hand back the entire rest of it, and a
 * trailing frame-size number is not part of the type. */
static char *fn_copy_type(const char *start, int *oneway)
{
	const char *end = fn_skip_type(start, oneway);
	size_t length;
	char *copy;

	if (end == NULL || end <= start) {
		return NULL;
	}
	length = (size_t)(end - start);
	copy = (char *)malloc(length + 1);
	if (copy == NULL) {
		return NULL;
	}
	memcpy(copy, start, length);
	copy[length] = '\0';
	return copy;
}

/* ------------------------------------------------------------------- class */

/* The designated initializer is NOT public API: Cocoa's only documented
 * constructor is the class method below, and the inventory is asserted against
 * Cocoa, so this stays out of the header. */
@interface NSMethodSignature ()
- (id)initWithObjCTypes:(const char *)types;
@end

@implementation NSMethodSignature

+ (NSMethodSignature *)signatureWithObjCTypes:(const char *)types
{
	if (types == NULL) {
		return nil;
	}
	return [[self alloc] initWithObjCTypes:types];
}

- (id)initWithObjCTypes:(const char *)types
{
	const char *q;
	size_t count = 0, i;
	int oneway = 0;

	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (types == NULL) {
		return self;		/* the empty signature */
	}
	_types = (char *)malloc(strlen(types) + 1);
	if (_types == NULL) {
		return self;
	}
	strcpy(_types, types);
	/* The return type first, then every later type as an argument. Each is stored
	 * as its OWN string (see fn_copy_type), because both accessors answer one
	 * type, not a slice of the encoding. */
	_returnType = fn_copy_type(fn_type_start(_types), &oneway);
	for (q = fn_skip_type(fn_type_start(_types), &oneway);
	     q != NULL && *q != '\0'; ) {
		const char *start = fn_type_start(q);
		const char *next;

		if (*start == '\0') {
			break;		/* a trailing number is a frame size, not a type */
		}
		next = fn_skip_type(start, &oneway);
		if (next == NULL || next <= start) {
			break;
		}
		count++;
		q = next;
	}
	if (count > 0) {
		_argumentTypes = (const char **)malloc(count * sizeof(const char *));
	}
	if (_argumentTypes != NULL) {
		q = fn_skip_type(fn_type_start(_types), &oneway);
		for (i = 0; i < count; i++) {
			const char *start;
			const char *next;

			if (q == NULL || *q == '\0') {
				break;
			}
			start = fn_type_start(q);
			if (*start == '\0') {
				break;
			}
			next = fn_skip_type(start, &oneway);
			if (next == NULL || next <= start) {
				break;
			}
			_argumentTypes[i] = fn_copy_type(start, &oneway);
			q = next;
		}
		_argumentCount = i;
	}
	_oneway = oneway ? YES : NO;
	if (_returnType != NULL) {
		size_t size = 0, align = 1;

		fn_type_metrics(_returnType, &size, &align);
		_returnLength = size;
	}
	for (i = 0; i < _argumentCount; i++) {
		size_t size = 0, align = 1;

		fn_type_metrics(_argumentTypes[i], &size, &align);
		_frameLength += ((size + 7) / 8) * 8;
	}
	return self;
}

- (void)dealloc
{
	size_t i;

	free(_types);
	if (_returnType != NULL) {
		free((void *)_returnType);
	}
	for (i = 0; i < _argumentCount; i++) {
		free((void *)_argumentTypes[i]);
	}
	free(_argumentTypes);
}

- (NSUInteger)numberOfArguments
{
	return _argumentCount;
}

- (const char *)getArgumentTypeAtIndex:(NSUInteger)index
{
	if (index >= _argumentCount) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSMethodSignature: index %lu out of range (%lu argument(s))",
				   (unsigned long)index, (unsigned long)_argumentCount];
	}
	return _argumentTypes[index];
}

- (const char *)methodReturnType
{
	return _returnType;
}

- (NSUInteger)methodReturnLength
{
	return _returnLength;
}

- (NSUInteger)frameLength
{
	return _frameLength;
}

- (BOOL)isOneway
{
	return _oneway;
}

- (NSString *)description
{
	return [NSString stringWithFormat:@"<NSMethodSignature: %lu argument(s), return %s, frame %lu>",
					  (unsigned long)_argumentCount,
					  (_returnType != NULL) ? _returnType : "?",
					  (unsigned long)_frameLength];
}

@end
