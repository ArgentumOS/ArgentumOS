/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * npropertylistserialization.m — the Objective-C skin over the C plist core.
 *
 * This file is CONVERSION ONLY: a plist_value_t tree into Foundation objects and
 * back. All the parsing and serialising is in userland/plist.c, because libconfig
 * consumes the same core from C and the two must agree on every file.
 */

#import <foundation/NSPropertyListSerialization.h>
#import <foundation/NSString.h>
#import <foundation/NSNumber.h>
#import <foundation/NSData.h>
#import <foundation/NSDate.h>
#import <foundation/NSArray.h>
#import <foundation/NSDictionary.h>
#import <foundation/NSError.h>

#include <plist.h>		/* the shared core: -Iinclude */
#include <stdlib.h>
#include <string.h>

#define FN_PLIST_ERROR_DOMAIN @"NSPropertyListErrorDomain"

static NSError *fn_plist_error(NSInteger code, const char *message)
{
	NSDictionary *info = nil;
	NSError *error;

	if (message != NULL) {
		info = [NSDictionary dictionaryWithObject:[NSString stringWithUTF8String:message]
						   forKey:NSLocalizedDescriptionKey];
	}
	error = [NSError errorWithDomain:FN_PLIST_ERROR_DOMAIN code:code userInfo:info];
	return error;
}

/* ---- the core's tree -> Foundation objects -------------------------------- */

static id fn_object_from_plist(const plist_value_t *value, NSPropertyListReadOptions options)
{
	BOOL mutable_leaves = (options & NSPropertyListMutableContainersAndLeaves) != 0;

	if (value == NULL) {
		return nil;
	}
	switch (value->type) {
	case PLIST_STRING:
		if (mutable_leaves) {
			return [NSMutableString stringWithUTF8String:value->u.string];
		}
		return [NSString stringWithUTF8String:value->u.string];
	case PLIST_INTEGER:
		return [NSNumber numberWithLongLong:value->u.integer];
	case PLIST_REAL:
		return [NSNumber numberWithDouble:value->u.real];
	case PLIST_BOOLEAN:
		return [NSNumber numberWithBool:value->u.boolean ? YES : NO];
	case PLIST_DATE:
		return [NSDate dateWithTimeIntervalSinceReferenceDate:value->u.date];
	case PLIST_DATA:
		return [NSData dataWithBytes:value->u.data.bytes length:value->u.data.length];
	case PLIST_ARRAY:
		{
			size_t i;
			id array = [NSMutableArray array];

			for (i = 0; i < value->u.array.count; i++) {
				id item = fn_object_from_plist(value->u.array.items[i], options);

				if (item == nil) {
					return nil;
				}
				[array addObject:item];
			}
			/* An immutable read hands back an immutable container: the option
			 * names what the CALLER gets, not what we build with. */
			if ((options & NSPropertyListMutableContainers) == 0 &&
			    (options & NSPropertyListMutableContainersAndLeaves) == 0) {
				return [[NSArray alloc] initWithArray:array];
			}
			return array;
		}
	case PLIST_DICTIONARY:
		{
			size_t i;
			id dictionary = [NSMutableDictionary dictionary];

			for (i = 0; i < value->u.dictionary.count; i++) {
				NSString *key = [NSString stringWithUTF8String:value->u.dictionary.keys[i]];
				id item = fn_object_from_plist(value->u.dictionary.values[i], options);

				if (key == nil || item == nil) {
					return nil;
				}
				[dictionary setObject:item forKey:key];
			}
			if ((options & NSPropertyListMutableContainers) == 0 &&
			    (options & NSPropertyListMutableContainersAndLeaves) == 0) {
				return [[NSDictionary alloc] initWithDictionary:dictionary];
			}
			return dictionary;
		}
	default:
		return nil;
	}
}

/* ---- Foundation objects -> the core's tree -------------------------------- */

static plist_value_t *fn_plist_from_object(id object)
{
	if (object == nil) {
		return NULL;
	}
	if ([object isKindOfClass:[NSString class]]) {
		return plist_new_string([(NSString *)object UTF8String]);
	}
	if ([object isKindOfClass:[NSNumber class]]) {
		const char *kind = [(NSNumber *)object objCType];

		/*
		 * A BOOLEAN AND A char ARE INDISTINGUISHABLE IN THIS NSNumber: both carry
		 * @encode of a one-byte signed type, and unlike Cocoa there is no
		 * separate CFBoolean tag to consult. The rule here is the one that keeps
		 * PLISTS round-tripping: a one-byte number whose value is 0 or 1 becomes
		 * a boolean, anything else stays an integer. A caller who means the
		 * character 'A' gets an integer, which is the honest reading; a caller who
		 * means YES gets <true/>. Recorded because it is a real limitation, not
		 * an oversight.
		 */
		if (kind != NULL && (kind[0] == 'c' || kind[0] == 'C' || kind[0] == 'B')) {
			long long value = [(NSNumber *)object longLongValue];

			if (value == 0 || value == 1) {
				return plist_new_boolean((int)value);
			}
			return plist_new_integer(value);
		}
		if (kind != NULL && (kind[0] == 'f' || kind[0] == 'd')) {
			return plist_new_real([(NSNumber *)object doubleValue]);
		}
		return plist_new_integer([(NSNumber *)object longLongValue]);
	}
	if ([object isKindOfClass:[NSDate class]]) {
		return plist_new_date([(NSDate *)object timeIntervalSinceReferenceDate]);
	}
	if ([object isKindOfClass:[NSData class]]) {
		NSUInteger length = [(NSData *)object length];
		unsigned char *bytes = (unsigned char *)malloc(length == 0 ? 1 : length);
		plist_value_t *value;

		if (bytes == NULL) {
			return NULL;
		}
		[(NSData *)object getBytes:bytes length:length];
		value = plist_new_data(bytes, length);
		free(bytes);
		return value;
	}
	if ([object isKindOfClass:[NSArray class]]) {
		NSUInteger i;
		plist_value_t *array = plist_new_array();

		if (array == NULL) {
			return NULL;
		}
		for (i = 0; i < [(NSArray *)object count]; i++) {
			plist_value_t *item = fn_plist_from_object([(NSArray *)object objectAtIndex:i]);

			if (item == NULL || plist_array_append(array, item) != 0) {
				plist_free(item);
				plist_free(array);
				return NULL;
			}
		}
		return array;
	}
	if ([object isKindOfClass:[NSDictionary class]]) {
		NSUInteger i;
		NSArray *keys = [(NSDictionary *)object allKeys];
		plist_value_t *dictionary = plist_new_dictionary();

		if (dictionary == NULL) {
			return NULL;
		}
		for (i = 0; i < [keys count]; i++) {
			id key = [keys objectAtIndex:i];
			plist_value_t *item;

			/* A plist KEY is a string by definition, so a non-string key cannot
			 * be written — refused rather than stringified silently. */
			if (![key isKindOfClass:[NSString class]]) {
				plist_free(dictionary);
				return NULL;
			}
			item = fn_plist_from_object([(NSDictionary *)object objectForKey:key]);
			if (item == NULL ||
			    plist_dictionary_set(dictionary, [(NSString *)key UTF8String], item) != 0) {
				plist_free(item);
				plist_free(dictionary);
				return NULL;
			}
		}
		return dictionary;
	}
	return NULL;		/* anything else cannot be written, and says so */
}

/* ---- the public API ------------------------------------------------------ */

@implementation NSPropertyListSerialization

+ (id)propertyListWithData:(NSData *)data
		   options:(NSPropertyListReadOptions)options
		    format:(NSPropertyListFormat *)format
		     error:(NSError **)errorPtr
{
	char message[256];
	plist_value_t *root;
	id object;

	if (errorPtr != NULL) {
		*errorPtr = nil;
	}
	if (data == nil) {
		if (errorPtr != NULL) {
			*errorPtr = fn_plist_error(1, "no data");
		}
		return nil;
	}
	if (format != NULL) {
		*format = NSPropertyListXMLFormat_v1_0;
	}
	root = plist_parse((const char *)[data bytes], [data length], message, sizeof(message));
	if (root == NULL) {
		if (errorPtr != NULL) {
			*errorPtr = fn_plist_error(1, message[0] != '\0' ? message : "malformed property list");
		}
		return nil;
	}
	object = fn_object_from_plist(root, options);
	plist_free(root);
	if (object == nil && errorPtr != NULL) {
		*errorPtr = fn_plist_error(1, "property list contains an object this Foundation cannot represent");
	}
	return object;
}

+ (NSData *)dataWithPropertyList:(id)plist
			  format:(NSPropertyListFormat)format
			 options:(NSPropertyListWriteOptions)options
			   error:(NSError **)errorPtr
{
	plist_value_t *root;
	char *text;
	size_t length = 0;
	NSData *data;

	(void)options;
	if (errorPtr != NULL) {
		*errorPtr = nil;
	}
	if (format != NSPropertyListXMLFormat_v1_0) {
		if (errorPtr != NULL) {
			*errorPtr = fn_plist_error(2, "only XML (v1.0) property lists are supported");
		}
		return nil;
	}
	root = fn_plist_from_object(plist);
	if (root == NULL) {
		if (errorPtr != NULL) {
			*errorPtr = fn_plist_error(2, "the object tree contains something this Foundation cannot write");
		}
		return nil;
	}
	text = plist_serialize(root, &length);
	plist_free(root);
	if (text == NULL) {
		if (errorPtr != NULL) {
			*errorPtr = fn_plist_error(2, "could not serialise");
		}
		return nil;
	}
	data = [NSData dataWithBytes:text length:length];
	free(text);
	return data;
}

+ (BOOL)propertyList:(id)plist isValidForFormat:(NSPropertyListFormat)format
{
	if (format != NSPropertyListXMLFormat_v1_0) {
		return NO;		/* the named-but-unsupported formats are not "valid" here */
	}
	if (plist == nil) {
		return NO;
	}
	{
		plist_value_t *root = fn_plist_from_object(plist);
		BOOL valid = (root != NULL);

		plist_free(root);
		return valid;
	}
}

@end
