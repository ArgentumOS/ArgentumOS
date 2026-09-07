/*
 * libconfig.h — universal configuration access for the FNX OS userland.
 *
 * Design: docs/design/config-design.md
 *  - one .conf file per app domain in a Configuration/ dir; FNX's own
 *    apps use the reserved first-party pseudo-domain system.<app>
 *    (third-party apps keep reverse-DNS domains such as com.example.x)
 *  - three scopes: user (/Shared/, system) per the FSH origin model
 *  - plain-text "key = value" lines, types inferred, dot-nested keys
 *  - resolution precedence: system -> user -> shared
 *    (system is authoritative; user overrides shared defaults)
 *
 * This header is userland-only (the kernel does not include it). v1 is
 * single-threaded; no global state is shared between calls, but the
 * returned values are owned by the library until config_value_free().
 */

#ifndef FNX_LIBCONFIG_H
#define FNX_LIBCONFIG_H

/* Reserved first-party pseudo-domain root (docs/design/config-design.md §2):
 * FNX's own apps are system.<app>; third parties keep reverse-DNS
 * domains (com.example.<app>). This namespace is never handed out. */
#define LIBCONFIG_SYSTEM_DOMAIN	"system"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIBCONFIG_VERSION "0.1"

/* ------------------------------------------------------------------ */
/* Types                                                              */
/* ------------------------------------------------------------------ */

/* Scopes map 1:1 to Configuration/ directories of the FSH. */
typedef enum config_scope {
	CONFIG_SCOPE_USER = 0,	/* Users/$USER/Configuration   */
	CONFIG_SCOPE_SHARED,	/* /Shared/Configuration       */
	CONFIG_SCOPE_SYSTEM,	/* /System/Configuration       */
} config_scope_t;

/* Value types, mirroring the .conf inference rules (docs §10).
 * CONFIG_TYPE_RECORD (v2, docs §10.1) marks a record value — a block
 * (`key = { … }`) read back whole, or an anonymous element of a
 * bracketed array literal. */
typedef enum config_type {
	CONFIG_TYPE_STRING = 0,
	CONFIG_TYPE_BOOL,
	CONFIG_TYPE_INT,
	CONFIG_TYPE_FLOAT,
	CONFIG_TYPE_ARRAY,
	CONFIG_TYPE_RECORD,
} config_type_t;

typedef enum config_err {
	CONFIG_OK = 0,
	CONFIG_ERR_NOT_FOUND,	/* domain or key absent from all scopes   */
	CONFIG_ERR_TYPE,	/* value present, wrong type for getter    */
	CONFIG_ERR_PARSE,	/* .conf file malformed                    */
	CONFIG_ERR_IO,		/* open/read/write/rename failure          */
	CONFIG_ERR_INVALID,	/* bad domain name, key, scope, or NULL    */
	CONFIG_ERR_ACCESS,	/* permission denied                       */
	CONFIG_ERR_NOMEM,	/* allocation failure                      */
} config_err_t;

/*
 * A config value. `string` is a NUL-terminated copy owned by the
 * library; it stays valid until config_value_free() or until the next
 * call that returns a value from the same domain. `array.items` points
 * to `count` config_value_t elements (also lib-owned). A
 * CONFIG_TYPE_RECORD value is an ordered field map: `record.fields`
 * points to `count` config_record_field_t entries, each a malloc'd
 * single-segment name + lib-owned value (v2, docs §10.1).
 */
typedef struct config_value config_value_t;
typedef struct config_record_field config_record_field_t;
struct config_value {
	config_type_t type;
	union {
		const char *string;
		bool boolean;
		int64_t integer;
		double floating;
		struct {
			config_value_t *items;
			size_t count;
		} array;
		struct {
			config_record_field_t *fields;
			size_t count;
		} record;
	} v;
};

/* One field of a CONFIG_TYPE_RECORD value (v2, docs §10.1). `name` is a
 * malloc'd single-segment field name; `value` is the field's value.
 * Both are lib-owned and stay valid until the enclosing value is freed
 * with config_value_free(). */
struct config_record_field {
	char *name;
	config_value_t value;
};

/* ------------------------------------------------------------------ */
/* Reading — resolve system -> user -> shared                        */
/* ------------------------------------------------------------------ */

/*
 * Read a key with full precedence resolution. On CONFIG_OK, *out holds
 * the value and *found_scope (may be NULL) the scope it came from.
 */
config_err_t config_read(const char *domain, const char *key,
			 config_scope_t *found_scope, config_value_t *out);

/* Typed getters; return CONFIG_ERR_NOT_FOUND or CONFIG_ERR_TYPE. */
config_err_t config_get_string(const char *domain, const char *key,
			       const char **out);
config_err_t config_get_bool(const char *domain, const char *key,
			     bool *out);
config_err_t config_get_int(const char *domain, const char *key,
			    int64_t *out);
config_err_t config_get_float(const char *domain, const char *key,
			      double *out);
/* Array getter: *items points to count lib-owned config_value_t. */
config_err_t config_get_array(const char *domain, const char *key,
			      config_value_t **items, size_t *count);

/* Which scope holds the effective value? (NOT_FOUND if none.) */
config_err_t config_resolve(const char *domain, const char *key,
			    config_scope_t *scope);

/* Read from one explicit scope (no precedence fallback). */
config_err_t config_read_scope(config_scope_t scope, const char *domain,
			       const char *key, config_value_t *out);

/*
 * v2 (docs §10.1): look up child field `name` in a CONFIG_TYPE_RECORD
 * value. On CONFIG_OK *out (may be NULL to test presence) is set to the
 * field's value pointer, lib-owned and valid until config_value_free()
 * on the containing value. Returns CONFIG_ERR_TYPE when `record` is not
 * a CONFIG_TYPE_RECORD and CONFIG_ERR_NOT_FOUND when the field is
 * absent.
 */
config_err_t config_record_child(const config_value_t *record,
				 const char *name,
				 config_value_t **out);

/* Prefix read for dot-nested keys: "window" -> every "window.*" key.
 * Returns a NULL-terminated array of "window.x" strings via *keys.
 * Caller frees with config_free_keys(). */
config_err_t config_get_all(const char *domain, const char *prefix,
			    char ***keys, config_value_t **values,
			    size_t *count);
void config_free_keys(char **keys, config_value_t *values, size_t count);

/* Record enumeration (group records, docs §3): the immediate children
 * of `group` that are containers (records) in one scope's domain file,
 * in source order. `group` may be "" for the top level. On success
 * *name is a malloc'd record name the caller frees. CONFIG_ERR_NOT_FOUND
 * when there is no (further) record; for config_record_next, a `prev`
 * that is not a record of the group is CONFIG_ERR_INVALID. */
config_err_t config_record_first(config_scope_t scope, const char *domain,
				 const char *group, char **name);
config_err_t config_record_next(config_scope_t scope, const char *domain,
				const char *group, const char *prev,
				char **name);

/* ------------------------------------------------------------------ */
/* Writing — explicit scope, atomic (temp + fsync + rename)           */
/* ------------------------------------------------------------------ */

config_err_t config_set_string(config_scope_t scope, const char *domain,
			       const char *key, const char *value);
config_err_t config_set_bool(config_scope_t scope, const char *domain,
			     const char *key, bool value);
config_err_t config_set_int(config_scope_t scope, const char *domain,
			    const char *key, int64_t value);
config_err_t config_set_float(config_scope_t scope, const char *domain,
			      const char *key, double value);
config_err_t config_set_array(config_scope_t scope, const char *domain,
			      const char *key, const config_value_t *items,
			      size_t count);

/* ------------------------------------------------------------------ */
/* Deletion                                                           */
/* ------------------------------------------------------------------ */

config_err_t config_unset(config_scope_t scope, const char *domain,
			  const char *key);
config_err_t config_remove_domain(config_scope_t scope,
				  const char *domain);

/* ------------------------------------------------------------------ */
/* Domains                                                            */
/* ------------------------------------------------------------------ */

/* List domains in one scope as "com.example.App" names. Caller frees
 * with config_free_domains(). */
config_err_t config_list_domains(config_scope_t scope, char ***domains,
				 size_t *count);
void config_free_domains(char **domains, size_t count);

/* ------------------------------------------------------------------ */
/* Utilities                                                          */
/* ------------------------------------------------------------------ */

/* Free a value returned by config_read()/config_read_scope() (recurses
 * into arrays). Never free library-returned pointers otherwise. */
void config_value_free(config_value_t *value);

const char *config_strerror(config_err_t err);
const char *config_scope_name(config_scope_t scope);

/* Resolve the on-disk path for a domain in a scope, for debugging:
 * e.g. CONFIG_SCOPE_USER, "system.dock" ->
 *      "Users/kyle/Configuration/system.dock.conf". */
config_err_t config_path(config_scope_t scope, const char *domain,
			 char *buf, size_t buflen);

/* Domain/key validation (also enforced by every entry point):
 *   domain: reverse-DNS, ^[A-Za-z_][A-Za-z0-9_.-]*$, no leading dot,
 *           no empty segment, no '/'
 *   key:    dot-separated segments, each ^[A-Za-z_][A-Za-z0-9_-]*$ */
bool config_valid_domain(const char *domain);
bool config_valid_key(const char *key);

/*
 * v2 (docs §10.2): true when `key` is a valid *addressing* key — plain
 * dotted keys plus `ident[i]` segments and bare all-digit segments
 * (array indexes): `rules[1].edits[0].value`, `rules[0].tests.1`.
 */
bool config_valid_address(const char *key);
/* True for pinned single-file domains (config-design §12: 'system.kernel'
 * lives on the ESP at /System/ESP/EFI/BOOT/kernel.conf - no scope roots, no
 * system/user/shared merge). */
bool config_is_pinned(const char *domain);

#ifdef __cplusplus
}
#endif

#endif /* FNX_LIBCONFIG_H */
