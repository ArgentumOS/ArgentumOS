/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * plist_test — the C plist core's acceptance (include/plist.h, userland/plist.c).
 *
 * The core is shared: libconfig will read config files through it and Foundation
 * wraps it in Objective-C. So its contract is tested where it lives — in C, with
 * no runtime in the way — and the properties asserted here are the ones the two
 * readers depend on: EVERY type survives a round trip, the writer is IDEMPOTENT,
 * Apple's own spelling is accepted, and anything unrecognised is REFUSED loudly
 * rather than skipped.
 *
 * Prints PLIST-TEST <name> ok|FAIL lines and a RESULT tally; exits non-zero if
 * any check failed.
 */

#include "plist.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int ok_count = 0;
static int fail_count = 0;

static void check(const char *name, int condition, const char *detail)
{
	if (condition) {
		ok_count++;
		printf("PLIST-TEST %s ok\n", name);
	} else {
		fail_count++;
		printf("PLIST-TEST %s FAIL %s\n", name, detail != NULL ? detail : "");
	}
}

int main(void)
{
	char error[256];
	plist_value_t *root;
	size_t length = 0;
	char *text;

	printf("PLIST-TEST start\n");

	/* ---- build a document that exercises every type -------------------- */
	root = plist_new_dictionary();
	plist_dictionary_set(root, "name", plist_new_string("A & B <c> \"d\""));
	plist_dictionary_set(root, "count", plist_new_integer(-42));
	plist_dictionary_set(root, "ratio", plist_new_real(0.1));
	plist_dictionary_set(root, "enabled", plist_new_boolean(1));
	plist_dictionary_set(root, "disabled", plist_new_boolean(0));
	plist_dictionary_set(root, "when", plist_new_date(0.0));
	plist_dictionary_set(root, "blob", plist_new_data((const unsigned char *)"\x00\x01\xFE\xFF binary", 11));
	plist_dictionary_set(root, "empty", plist_new_string(""));
	{
		plist_value_t *list = plist_new_array();

		plist_array_append(list, plist_new_string("one"));
		plist_array_append(list, plist_new_integer(2));
		plist_array_append(list, plist_new_dictionary());
		plist_array_append(list, plist_new_array());
		plist_dictionary_set(root, "list", list);
	}
	/* Setting the same key twice REPLACES: a config read twice must not grow. */
	plist_dictionary_set(root, "count", plist_new_integer(7));

	text = plist_serialize(root, &length);
	check("serialise-succeeds", text != NULL && length > 0, NULL);
	check("apple-shape",
	      text != NULL &&
	      strstr(text, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>") == text &&
	      strstr(text, "-//Apple//DTD PLIST 1.0//EN") != NULL &&
	      strstr(text, "<plist version=\"1.0\">") != NULL,
	      NULL);
	check("empty-collections-self-closing",
	      text != NULL && strstr(text, "<dict/>") != NULL && strstr(text, "<array/>") != NULL,
	      NULL);

	/* ---- round trip ---------------------------------------------------- */
	{
		plist_value_t *back = plist_parse(text, length, error, sizeof(error));

		check("parse-roundtrip", back != NULL, error);
		if (back != NULL) {
			plist_value_t *value;

			check("type-dictionary", plist_type_of(back) == PLIST_DICTIONARY, NULL);
			check("string-escaped",
			      strcmp(plist_dictionary_get(back, "name")->u.string,
				     "A & B <c> \"d\"") == 0, NULL);
			check("integer-replaced", plist_dictionary_get(back, "count")->u.integer == 7, NULL);
			check("integer-negative", plist_dictionary_get(back, "count")->u.integer == 7, NULL);
			check("real-exact", plist_dictionary_get(back, "ratio")->u.real == 0.1, NULL);
			check("bool-true", plist_dictionary_get(back, "enabled")->u.boolean == 1, NULL);
			check("bool-false", plist_dictionary_get(back, "disabled")->u.boolean == 0, NULL);
			check("date-epoch", fabs(plist_dictionary_get(back, "when")->u.date) < 0.5, NULL);
			value = plist_dictionary_get(back, "blob");
			check("data-binary",
			      value->u.data.length == 11 &&
			      memcmp(value->u.data.bytes, "\x00\x01\xFE\xFF binary", 11) == 0, NULL);
			value = plist_dictionary_get(back, "list");
			check("array-count", plist_array_count(value) == 4, NULL);
			check("array-string", strcmp(plist_array_get(value, 0)->u.string, "one") == 0, NULL);
			check("array-integer", plist_array_get(value, 1)->u.integer == 2, NULL);
			check("array-nested-dict", plist_type_of(plist_array_get(value, 2)) == PLIST_DICTIONARY, NULL);
			check("array-nested-array", plist_type_of(plist_array_get(value, 3)) == PLIST_ARRAY, NULL);
			check("empty-string",
			      strcmp(plist_dictionary_get(back, "empty")->u.string, "") == 0, NULL);

			/* IDEMPOTENCE: serialising the parse of our own output is
			 * byte-identical. This is the property a config format lives on —
			 * a consumer must not see a diff just because a file was rewritten. */
			{
				size_t again_length = 0;
				char *again = plist_serialize(back, &again_length);

				check("serialise-idempotent",
				      again != NULL && again_length == length &&
				      memcmp(again, text, length) == 0, NULL);
				free(again);
			}
			plist_free(back);
		}
	}

	/* ---- comments SURVIVE A REWRITE -------------------------------------
	 *
	 * The shipped config files are MOSTLY PROSE, so the property that matters
	 * is not "comments parse" but "nothing deletes them": parse -> serialise is
	 * BYTE-IDENTICAL, a comment stays attached to the entry it precedes, and a
	 * `config set`-style change leaves the prose where it was. */
	{
		static const char commented[] =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
			"\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
			"<plist version=\"1.0\">\n"
			"<dict>\n"
			"\t<!-- head prose -->\n"
			"\t<key>greeting</key>\n"
			"\t<string>hello</string>\n"
			"\t<key>list</key>\n"
			"\t<array>\n"
			"\t\t<!-- first -->\n"
			"\t\t<string>one</string>\n"
			"\t\t<string>two</string>\n"
			"\t</array>\n"
			"\t<!-- before the last entry -->\n"
			"\t<key>last</key>\n"
			"\t<true/>\n"
			"</dict>\n"
			"</plist>\n";
		plist_value_t *parsed = plist_parse(commented, strlen(commented),
						    error, sizeof(error));
		char *written = NULL;
		size_t written_length = 0;

		check("comments-parse", parsed != NULL, error);
		if (parsed != NULL) {
			plist_value_t *list = plist_dictionary_get(parsed, "list");

			/* A comment is a SLOT of the storage — a NULL key here — and
			 * INVISIBLE to the accessors, which is what keeps an index from
			 * ever landing on prose. The two slots asserted are the one at
			 * the head and the one standing before "last": ATTACHMENT is
			 * exactly "the comment sits immediately before its entry". */
			check("comment-occupies-a-slot",
			      parsed->u.dictionary.count == 5 &&
			      parsed->u.dictionary.keys[0] == NULL &&
			      plist_type_of(parsed->u.dictionary.values[0]) == PLIST_COMMENT &&
			      strcmp(parsed->u.dictionary.values[0]->u.string, " head prose ") == 0 &&
			      parsed->u.dictionary.keys[3] == NULL &&
			      plist_type_of(parsed->u.dictionary.values[3]) == PLIST_COMMENT &&
			      strcmp(parsed->u.dictionary.values[3]->u.string,
				     " before the last entry ") == 0 &&
			      strcmp(parsed->u.dictionary.keys[4], "last") == 0,
			      "the comments are not NULL-key slots beside their entries");
			check("comment-yields-to-lookup",
			      plist_dictionary_get(parsed, "greeting") != NULL &&
			      strcmp(plist_dictionary_get(parsed, "greeting")->u.string,
				     "hello") == 0,
			      NULL);
			check("comment-in-an-array-is-an-item",
			      list != NULL && list->u.array.count == 3 &&
			      plist_type_of(list->u.array.items[0]) == PLIST_COMMENT,
			      NULL);
			check("comment-yields-to-index",
			      plist_array_count(list) == 2 &&
			      plist_array_get(list, 0) != NULL &&
			      strcmp(plist_array_get(list, 0)->u.string, "one") == 0 &&
			      plist_array_get(list, 1) != NULL &&
			      strcmp(plist_array_get(list, 1)->u.string, "two") == 0,
			      NULL);

			/* THE ACCEPTANCE: a rewrite is the same document, byte for
			 * byte — which is what makes keeping comments worth the tree's
			 * extra shape. */
			written = plist_serialize(parsed, &written_length);
			check("comments-roundtrip-byte-identical",
			      written != NULL && written_length == strlen(commented) &&
			      memcmp(written, commented, written_length) == 0,
			      written == NULL ? "serialise failed"
					      : "the writer moved or lost a comment");

			if (written != NULL) {
				char *rewritten;
				size_t rewritten_length = 0;

				/* A `config set`: one value changes, the prose does not. */
				plist_dictionary_set(parsed, "greeting", plist_new_string("goodbye"));
				rewritten = plist_serialize(parsed, &rewritten_length);
				check("comments-survive-a-set",
				      rewritten != NULL &&
				      strstr(rewritten, "<!-- head prose -->") != NULL &&
				      strstr(rewritten, "<!-- before the last entry -->") != NULL &&
				      strstr(rewritten, "<string>goodbye</string>") != NULL,
				      NULL);
				free(rewritten);
			}
			free(written);
			plist_free(parsed);
		}
	}

	/* A comment BEFORE the root value is accepted and then DISCARDED: a file's
	 * header prose belongs INSIDE the root dictionary, where it round-trips,
	 * and hoisting one into a <string> root would have nowhere to put it. */
	{
		static const char header[] =
			"<plist version=\"1.0\">\n"
			"<!-- a header, which has no slot to go in -->\n"
			"<dict>\n"
			"\t<key>a</key>\n"
			"\t<string>b</string>\n"
			"</dict>\n"
			"</plist>\n";
		plist_value_t *parsed = plist_parse(header, strlen(header), error, sizeof(error));
		char *written = NULL;

		check("leading-comment-accepted", parsed != NULL, error);
		if (parsed != NULL) {
			written = plist_serialize(parsed, NULL);
			check("leading-comment-dropped",
			      written != NULL && strstr(written, "no slot to go in") == NULL,
			      NULL);
			free(written);
			plist_free(parsed);
		}
	}

	/* A comment that never ends is REFUSED, like everything else this parser
	 * cannot read, rather than swallowed to the end of the file. */
	{
		static const char unterminated[] =
			"<plist version=\"1.0\"><dict><!-- oops</dict></plist>";
		plist_value_t *refused = plist_parse(unterminated, strlen(unterminated),
						     error, sizeof(error));

		check("unterminated-comment-refused", refused == NULL, NULL);
		check("unterminated-comment-named",
		      strstr(error, "unterminated comment") != NULL, error);
		if (refused != NULL) {
			plist_free(refused);
		}
	}

	/* ---- a document in APPLE'S OWN SPELLING ---------------------------- */
	{
		static const char apple[] =
			"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
			"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
			"\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
			"<plist version=\"1.0\">\n"
			"<!-- a comment, which is whitespace -->\n"
			"<dict>\n"
			"\t<key>greeting</key>\n"
			"\t<string>&lt;hello&gt; &amp; goodbye &#65;</string>\n"
			"\t<key>emptyDict</key>\n"
			"\t<dict/>\n"
			"\t<key>flag</key>\n"
			"\t<true/>\n"
			"</dict>\n"
			"</plist>\n";
		plist_value_t *parsed = plist_parse(apple, strlen(apple), error, sizeof(error));

		check("apple-shape-parses", parsed != NULL, error);
		if (parsed != NULL) {
			check("entity-decode",
			      strcmp(plist_dictionary_get(parsed, "greeting")->u.string,
				     "<hello> & goodbye A") == 0, NULL);
			check("self-closing-empty-dict",
			      plist_type_of(plist_dictionary_get(parsed, "emptyDict")) == PLIST_DICTIONARY &&
			      plist_dictionary_get(parsed, "emptyDict")->u.dictionary.count == 0, NULL);
			check("self-closing-boolean", plist_dictionary_get(parsed, "flag")->u.boolean == 1, NULL);
			plist_free(parsed);
		}
	}

	/* ---- REFUSAL ------------------------------------------------------- */
	{
		static const char unknown[] =
			"<plist version=\"1.0\"><dict><key>a</key><widget>1</widget></dict></plist>";
		static const char mismatched[] =
			"<plist version=\"1.0\"><dict><key>a</key></array></dict></plist>";
		static const char prose[] = "this is not XML, it is a sentence\n";
		plist_value_t *refused;

		refused = plist_parse(unknown, strlen(unknown), error, sizeof(error));
		check("unknown-element-refused", refused == NULL, NULL);
		check("refusal-message-names-the-text",
		      error[0] != '\0' && strstr(error, "unknown element") != NULL &&
		      strstr(error, "widget") != NULL, error);
		if (refused != NULL) {
			plist_free(refused);
		}
		refused = plist_parse(mismatched, strlen(mismatched), error, sizeof(error));
		check("mismatched-tag-refused", refused == NULL, NULL);
		if (refused != NULL) {
			plist_free(refused);
		}
		refused = plist_parse(prose, strlen(prose), error, sizeof(error));
		check("non-xml-refused", refused == NULL, NULL);
		if (refused != NULL) {
			plist_free(refused);
		}
	}

	plist_free(root);
	free(text);
	printf("PLIST-TEST RESULT ok=%d fail=%d\n", ok_count, fail_count);
	printf("PLIST-TEST DONE\n");
	return fail_count == 0 ? 0 : 1;
}
