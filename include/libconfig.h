/*
 * libconfig.h — universal configuration access for the FNX OS userland.
 *
 * Design: docs/config-design.md
 *  - one .conf file per app domain in a Configuration/ dir; FNX's own
 *    apps use the reserved first-party pseudo-domain system.config.<app>
 *    (third-party apps keep reverse-DNS domains such as com.example.x)
 *  - three scopes: user (/Shared/, system) per the FSH origin model
 *  - plain-text "key = value" lines, types inferred, dot-nested keys
 *  - resolution precedence: user -> shared -> system
 *
 * This header is userland-only (the kernel does not include it). v1 is
 * single-threaded; no global state is shared between calls, but the
 * returned values are owned by the library until config_value_free().
 */

#ifndef FNX_LIBCONFIG_H
#define FNX_LIBCONFIG_H

/* Reserved first-party pseudo-domain root (docs/config-design.md §2):
 * FNX's own apps are system.config.<app>; third parties keep reverse-DNS
 * domains (com.example.<app>). This namespace is never handed out. */
#define LIBCONFIG_SYSTEM_DOMAIN	"system.config"

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

/* Value types, mirroring the .conf inference rules (docs §10). */
typedef enum config_type {
	CONFIG_TYPE_STRING = 0,
	CONFIG_TYPE_BOOL,
	CONFIG_TYPE_INT,
	CONFIG_TYPE_FLOAT,
	CONFIG_TYPE_ARRAY,
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
 * to `count` config_value_t elements (also lib-owned).
 */
typedef struct config_value config_value_t;
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
	} v;
};

/* ------------------------------------------------------------------ */
/* Reading — resolve user -> shared -> system                         */
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

/* Prefix read for dot-nested keys: "window" -> every "window.*" key.
 * Returns a NULL-terminated array of "window.x" strings via *keys.
 * Caller frees with config_free_keys(). */
config_err_t config_get_all(const char *domain, const char *prefix,
			    char ***keys, config_value_t **values,
			    size_t *count);
void config_free_keys(char **keys, config_value_t *values, size_t count);

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
 * e.g. CONFIG_SCOPE_USER, "system.config.dock" ->
 *      "Users/kyle/Configuration/system.config.dock.conf". */
config_err_t config_path(config_scope_t scope, const char *domain,
			 char *buf, size_t buflen);

/* Domain/key validation (also enforced by every entry point):
 *   domain: reverse-DNS, ^[A-Za-z_][A-Za-z0-9_.-]*$, no leading dot,
 *           no empty segment, no '/'
 *   key:    dot-separated segments, each ^[A-Za-z_][A-Za-z0-9_-]*$ */
bool config_valid_domain(const char *domain);
bool config_valid_key(const char *key);

#ifdef __cplusplus
}
#endif

#endif /* FNX_LIBCONFIG_H */
