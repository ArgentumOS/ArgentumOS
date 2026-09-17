/*
 * libconfig_internal.h — the model libconfig.c and libconfig_plist.c share.
 *
 * NOT a public header. userland/libconfig.h is the API; this is the internal
 * representation that two translation units build, and it lives in a header for
 * exactly one reason: a .conf file may be the legacy "key = value" text or an
 * XML plist (docs/design/plist-config-plan.md, stage P3b), and BOTH spellings
 * must parse into the same flat, ordered entry list. Every reader in
 * libconfig.c resolves against that list, so nothing downstream of the parse
 * knows — or needs to know — which syntax the file used.
 */
#ifndef FNX_LIBCONFIG_INTERNAL_H
#define FNX_LIBCONFIG_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>

#include <libconfig.h>

/* ---- limits (docs/design/config-design.md §10, protective) ---------- */

#define CONF_MAX_LINE		4096
#define CONF_MAX_FILE		(1024 * 1024)
#define CONF_MAX_SEGMENT	64
#define CONF_MAX_KEY		255

/* ---- one parsed key/value ------------------------------------------
 *
 * `key` is the FULL dotted key ("window.width"): a group `key = { … }` is
 * flattened into dotted entries, and so is a nested dictionary in a plist,
 * because the two spellings must be indistinguishable to every reader
 * (docs §10.1 "reads never care which spelling").
 *
 * `comment` is the PROSE that preceded this entry, as '\n'-separated lines, or
 * NULL. It exists because a config file is mostly documentation: the plist
 * writer emits these lines as XML comments, so a rewrite does not delete the
 * text that explains a setting. A legacy `#` line and a plist `<!-- … -->` both
 * land here, which is what makes the conversion lossless.
 */
struct entry {
	char *key;
	char *comment;
	config_value_t val;
	struct entry *next;
};

/* Append one line of prose to a '\n'-joined blob (allocated on demand).
 * Normalization — and why an empty line stays an empty comment — is in the
 * implementation. */
config_err_t config_comment_append(char **blob, const char *line);

/* ---- the plist spelling (libconfig_plist.c) --------------------------
 *
 * Detection is by CONTENT, never by file name: a .conf whose first non-blank
 * character is '<' is a plist, and the core is the authority on whether it is a
 * VALID one (an unreadable plist is a parse error, not a fallback to the legacy
 * grammar — guessing a format is how a config file gets silently mangled).
 */
bool config_text_is_plist(const char *text, size_t len);

/* Parse a plist .conf into the shared entry model.
 *
 * `blocks`/`nblocks` receive the TOP-LEVEL names that were dictionaries — the
 * plist spelling of an explicit `key = { … }` group, which is what tells the
 * writer to keep them nested rather than flattening them to dotted keys.
 * `trailer` receives the prose that followed the last entry (or NULL), so a
 * file's closing words survive a rewrite too.
 *
 * On failure: CONFIG_ERR_PARSE with `err` filled, *list NULL, and any blocks
 * already allocated are freed here so the caller has nothing to release. */
config_err_t config_plist_parse(const char *text, size_t len, struct entry **list,
				char ***blocks, int *nblocks, char **trailer,
				char *err, size_t errsz);

/* Write the entry list to `fd` as an XML plist. The writer emits plists,
 * period (the user's decision, recorded in the plan): a file that was legacy
 * text comes back as a plist, which is why every reader had to accept both
 * before this stage could write anything. Comments ride out as XML comments and
 * `trailer` is the file's closing prose. */
config_err_t config_plist_write(int fd, struct entry *list, char **blocks,
				int nblocks, const char *trailer);

/* The prose that FOLLOWED the last entry of `path` — read from the file about to
 * be replaced, because a set or an unset cannot change words that stand after
 * the last entry, and the read paths have no use for them. Returns 0 with *out
 * set (malloc'd, '\n'-joined), or -1 when there is nothing to carry. */
int config_trailer_prose(const char *path, char **out);

#endif /* FNX_LIBCONFIG_INTERNAL_H */
