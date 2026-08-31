/*
 * fnx/fs/bfs/query.c - the volume query engine (Haiku's BQuery).
 *
 * Linux's ABI has no BeOS fs_query syscall, so the query surface is a
 * BFS ioctl (BFS_IOC_QUERY) + a userland tool. The engine follows
 * Haiku's QueryParser semantics:
 *
 *   expr   := orexpr
 *   orexpr := andexpr | orexpr '||' andexpr
 *   andexpr:= term | andexpr '&&' term
 *   term   := '(' expr ')' | '!' term | equation
 *   equation := attr op value
 *   op     := '=' | '!=' | '>' | '>=' | '<' | '<='
 *
 * '!' only negates a parenthesized term and is resolved by DeMorgan at
 * parse time (Haiku's Complement()); values are quoted with ' or " or
 * run to the next operator / ')' ; '*' '?' '[' are wildcards for
 * '=' / '!=' on STRING indices only.
 *
 * Each equation resolves its attribute to an index file (the indices
 * dir's STRING tree), determines the index's key type from the inode
 * mode, converts the value text per that type, then walks the index
 * B+tree (in key order, expanding fragment/duplicate chains) and keeps
 * the inodes whose key satisfies the op. AND/OR combine inode sets;
 * a missing index yields the empty set for that equation (Haiku's
 * B_ENTRY_NOT_FOUND).
 *
 * Copyright 2026, the FNX project.
 * Distributed under the terms of the Fiwix License.
 */

#include <fnx/kernel.h>
#include <fnx/types.h>
#include <fnx/errno.h>
#include <fnx/fs.h>
#include <fnx/bfs.h>
#include <fnx/buffer.h>
#include <fnx/string.h>
#include <fnx/stat.h>

extern int bfs_btree_find(struct inode *, const char *, __ino_t *);
extern int bfs_btree_iterate_values(struct inode *, int,
	int (*)(const char *, int, __ino_t, void *), void *);

/* ---- the parsed expression tree -------------------------------- */

enum {
	BQ_FORMULA = 1,		/* a leaf: attr op value */
	BQ_AND,
	BQ_OR
};

/* comparison ops (Haiku's QueryParser::ops) */
enum {
	BQ_OP_EQUAL = 0,
	BQ_OP_NOT_EQUAL,
	BQ_OP_GREATER_THAN,
	BQ_OP_GREATER_THAN_OR_EQUAL,
	BQ_OP_LESS_THAN,
	BQ_OP_LESS_THAN_OR_EQUAL
};

struct bfs_qnode {
	int type;
	int op;					/* BQ_FORMULA: the comparison op */
	char attr[BFS_BTREE_MAX_KEY_LEN];
	int vlen;
	char value[BFS_BTREE_MAX_KEY_LEN];	/* raw value text */
	struct bfs_qnode *l, *r;		/* BQ_AND / BQ_OR children */
};

/* ---- an inode set: a sorted list of inode numbers -----------------
 * kmalloc() caps at PAGE_SIZE (4KB), so a set that can hold a whole
 * volume's matches (thousands of inodes) is a linked list of 4KB
 * chunks; each chunk holds up to 1020 sorted inos. The set stays
 * sorted at insert time (binary search + memmove, with a cascade
 * overflow into the next chunk), so AND/OR remain sorted merges. */

#define BFS_QCHUNK_CAP	1020	/* inos[1020] is the overflow slot;
				 * 1021*4 + 4 + 8 = 4096 == PAGE_SIZE */

struct bfs_qchunk {
	__u32 inos[BFS_QCHUNK_CAP + 1];	/* inos[CAP] = overflow slot */
	__u32 count;
	struct bfs_qchunk *next;
};

struct bfs_qset {
	struct bfs_qchunk *head, *tail;
	__u32 count;			/* total entries across all chunks */
};

static void bfs_qset_free(struct bfs_qset *s)
{
	struct bfs_qchunk *c = s->head;

	while(c) {
		struct bfs_qchunk *n = c->next;

		kfree((addr_t)c);
		c = n;
	}
	s->head = s->tail = NULL;
	s->count = 0;
}

static struct bfs_qchunk *bfs_qchunk_new(void)
{
	struct bfs_qchunk *c;

	if(!(c = (struct bfs_qchunk *)kmalloc(sizeof(struct bfs_qchunk)))) {
		return NULL;
	}
	c->count = 0;
	c->next = NULL;
	return c;
}

/* binary search within one chunk: the insert offset for 'ino' */
static __u32 bfs_qchunk_bsearch(const struct bfs_qchunk *c, __u32 ino)
{
	__u32 lo = 0, hi = c->count, mid;

	while(lo < hi) {
		mid = (lo + hi) >> 1;
		if(c->inos[mid] < ino) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return lo;
}

/* insert 'ino' at offset 'off' of chunk 'c', cascading the overflow
 * into the next chunk (creating it when needed). The pushed element
 * is the chunk's old maximum, so it always lands at the front of the
 * next chunk (the set is deduplicated, so it cannot collide there). */
static int bfs_qchunk_insert(struct bfs_qset *s, struct bfs_qchunk *c,
			     __u32 off, __u32 ino)
{
	for(;;) {
		__u32 tailv;

		memmove(&c->inos[off + 1], &c->inos[off],
			(c->count - off) * sizeof(__u32));
		c->inos[off] = ino;
		c->count++;
		s->count++;
		if(c->count <= BFS_QCHUNK_CAP) {
			return 0;
		}
		/* overflow: the last element falls into the next chunk */
		tailv = c->inos[BFS_QCHUNK_CAP];
		c->count = BFS_QCHUNK_CAP;
		s->count--;
		if(!c->next) {
			struct bfs_qchunk *n;

			if(!(n = bfs_qchunk_new())) {
				return -ENOMEM;
			}
			c->next = n;
			s->tail = n;
		}
		c = c->next;
		off = 0;
		ino = tailv;
	}
}

/* insert, keeping the set sorted + deduplicated */
static int bfs_qset_add(struct bfs_qset *s, __u32 ino)
{
	struct bfs_qchunk *c;
	__u32 off;

	if(!ino) {
		return 0;
	}
	if(!s->head) {
		if(!(c = bfs_qchunk_new())) {
			return -ENOMEM;
		}
		s->head = s->tail = c;
		c->inos[0] = ino;
		c->count = 1;
		s->count = 1;
		return 0;
	}
	/* find the first chunk whose maximum >= ino (chunks are sorted) */
	c = s->head;
	while(c->next && c->inos[c->count - 1] < ino) {
		c = c->next;
	}
	if(c->inos[c->count - 1] < ino) {
		/* append past the tail */
		if(c->count < BFS_QCHUNK_CAP) {
			c->inos[c->count++] = ino;
			s->count++;
			return 0;
		}
		if(!(c->next = bfs_qchunk_new())) {
			return -ENOMEM;
		}
		s->tail = c->next;
		c = c->next;
		off = 0;
	} else {
		off = bfs_qchunk_bsearch(c, ino);
		if(off < c->count && c->inos[off] == ino) {
			return 0;	/* already there */
		}
	}
	return bfs_qchunk_insert(s, c, off, ino);
}

/* s1 AND s2 -> s1 (in place; the result fits in s1's own chunks) */
static void bfs_qset_and(struct bfs_qset *s1, struct bfs_qset *s2)
{
	struct bfs_qchunk *c1 = s1->head, *c2 = s2->head;
	struct bfs_qchunk *w = s1->head, *lastw = NULL;
	__u32 i = 0, j = 0, wi = 0, n = 0;

	while(c1 && c2) {
		while(c1 && i >= c1->count) {
			c1 = c1->next;
			i = 0;
		}
		while(c2 && j >= c2->count) {
			c2 = c2->next;
			j = 0;
		}
		if(!c1 || !c2) {
			break;
		}
		if(c1->inos[i] < c2->inos[j]) {
			i++;
		} else if(c1->inos[i] > c2->inos[j]) {
			j++;
		} else {
			if(wi == BFS_QCHUNK_CAP) {
				w = w->next;
				wi = 0;
			}
			w->inos[wi++] = c1->inos[i];
			lastw = w;
			n++;
			i++;
			j++;
		}
	}
	if(!lastw) {
		bfs_qset_free(s1);	/* empty result */
		return;
	}
	lastw->count = wi;
	{
		struct bfs_qchunk *c = lastw->next;

		while(c) {
			struct bfs_qchunk *nxt = c->next;

			kfree((addr_t)c);
			c = nxt;
		}
		lastw->next = NULL;
	}
	s1->tail = lastw;
	s1->count = n;
}

/* s1 OR s2 -> *out (a fresh set; frees s1 and s2) */
static int bfs_qset_or(struct bfs_qset *s1, struct bfs_qset *s2,
		       struct bfs_qset *out)
{
	struct bfs_qchunk *c1 = s1->head, *c2 = s2->head, *tail = NULL;
	__u32 i = 0, j = 0, wi = 0;

	out->head = out->tail = NULL;
	out->count = 0;
	while(c1 || c2) {
		__u32 v;

		while(c1 && i >= c1->count) {
			c1 = c1->next;
			i = 0;
		}
		while(c2 && j >= c2->count) {
			c2 = c2->next;
			j = 0;
		}
		if(!c1 && !c2) {
			break;
		}
		if(c1 && (!c2 || c1->inos[i] < c2->inos[j])) {
			v = c1->inos[i++];
		} else if(c2 && (!c1 || c2->inos[j] < c1->inos[i])) {
			v = c2->inos[j++];
		} else {
			v = c1->inos[i++];
			j++;		/* equal: emit once */
		}
		if(!tail || wi == BFS_QCHUNK_CAP) {
			struct bfs_qchunk *n;

			if(!(n = bfs_qchunk_new())) {
				if(tail) {
					bfs_qset_free(out);
				}
				bfs_qset_free(s1);
				bfs_qset_free(s2);
				return -ENOMEM;
			}
			if(tail) {
				tail->count = wi;
				tail->next = n;
			} else {
				out->head = n;
			}
			tail = n;
			wi = 0;
		}
		tail->inos[wi++] = v;
		out->count++;
	}
	if(tail) {
		tail->count = wi;
		out->tail = tail;
	}
	bfs_qset_free(s1);
	bfs_qset_free(s2);
	return 0;
}

/* ---- value conversion per index type --------------------------- */

/* the kernel libc only has 32-bit strtol: local 64-bit / float
 * parsers for the query values (Haiku uses strtol/strtoul base-0 and
 * strtod) */

static __u64 bfs_q_strtou64(const char *s, int base)
{
	__u64 value = 0;

	while(*s == ' ' || *s == '\t') {
		s++;
	}
	if(*s == '+') {
		s++;
	}
	if(!base) {
		if(*s == '0') {
			base = 8;
			s++;
			if(*s == 'x' || *s == 'X') {
				base = 16;
				s++;
			}
		} else {
			base = 10;
		}
	}
	while(*s) {
		int c = (unsigned char)*s;

		if(c >= '0' && c <= '9') {
			c -= '0';
		} else if(c >= 'a' && c <= 'f') {
			c = c - 'a' + 10;
		} else if(c >= 'A' && c <= 'F') {
			c = c - 'A' + 10;
		} else {
			break;
		}
		if(c >= base) {
			break;
		}
		value = value * base + c;
		s++;
	}
	return value;
}

static __s64 bfs_q_strto64(const char *s, int base)
{
	int neg = 0;

	while(*s == ' ' || *s == '\t') {
		s++;
	}
	if(*s == '-') {
		neg = 1;
		s++;
	} else if(*s == '+') {
		s++;
	}
	{
		__u64 u = bfs_q_strtou64(s, base);
		return neg ? (__s64)(0 - u) : (__s64)u;
	}
}

/* strtod: [+-] digits [. digits] [e[+-]digits]. The kernel is built
 * -mno-sse, so a double cannot be RETURNED by value (the x86-64 ABI
 * uses xmm0) — the result goes through an out parameter (x87 handles
 * the arithmetic) */
static void bfs_q_strtod(const char *s, double *out)
{
	double value = 0, frac = 0, scale = 0.1;
	int neg = 0, exp = 0, expneg = 0;

	while(*s == ' ' || *s == '\t') {
		s++;
	}
	if(*s == '-') {
		neg = 1;
		s++;
	} else if(*s == '+') {
		s++;
	}
	while(*s >= '0' && *s <= '9') {
		value = value * 10 + (*s - '0');
		s++;
	}
	if(*s == '.') {
		s++;
		while(*s >= '0' && *s <= '9') {
			frac += (*s - '0') * scale;
			scale *= 0.1;
			s++;
		}
	}
	value += frac;
	if(*s == 'e' || *s == 'E') {
		s++;
		if(*s == '-') {
			expneg = 1;
			s++;
		} else if(*s == '+') {
			s++;
		}
		while(*s >= '0' && *s <= '9') {
			exp = exp * 10 + (*s - '0');
			s++;
		}
	}
	if(exp > 10000) {
		/* clamp: a crafted "1e999999999" must not loop forever */
		exp = 10000;
	}
	while(exp--) {
		value *= expneg ? 0.1 : 10.0;
	}
	*out = neg ? -value : value;
}

static int bfs_qtype_size(int dtype)
{
	switch(dtype) {
	case BFS_BTREE_INT8_TYPE:
		return 1;
	case BFS_BTREE_INT16_TYPE:
		return 2;
	case BFS_BTREE_INT32_TYPE:
	case BFS_BTREE_UINT32_TYPE:
	case BFS_BTREE_FLOAT_TYPE:
		return 4;
	case BFS_BTREE_INT64_TYPE:
	case BFS_BTREE_UINT64_TYPE:
	case BFS_BTREE_DOUBLE_TYPE:
		return 8;
	default:
		return 0;
	}
}

/* parse the raw value text into a fixed-size key buffer of the
 * index's type (Haiku's ParseValue: strtol/strtoul base 0 / strtod) */
static int bfs_q_parse_value(int dtype, const char *v, int vlen, char *out)
{
	char tmp[64];
	int n = (vlen < 63) ? vlen : 63;

	memcpy_b(tmp, v, n);
	tmp[n] = 0;

	switch(dtype) {
	case BFS_BTREE_INT8_TYPE: {
		__s8 x = (__s8)bfs_q_strto64(tmp, 0);
		memcpy_b(out, &x, 1);
		return 1;
	}
	case BFS_BTREE_INT16_TYPE: {
		__s16 x = (__s16)bfs_q_strto64(tmp, 0);
		memcpy_b(out, &x, 2);
		return 2;
	}
	case BFS_BTREE_INT32_TYPE: {
		__s32 x = (__s32)bfs_q_strto64(tmp, 0);
		memcpy_b(out, &x, 4);
		return 4;
	}
	case BFS_BTREE_UINT32_TYPE: {
		__u32 x = (__u32)bfs_q_strtou64(tmp, 0);
		memcpy_b(out, &x, 4);
		return 4;
	}
	case BFS_BTREE_INT64_TYPE: {
		__s64 x = bfs_q_strto64(tmp, 0);
		memcpy_b(out, &x, 8);
		return 8;
	}
	case BFS_BTREE_UINT64_TYPE: {
		__u64 x = bfs_q_strtou64(tmp, 0);
		memcpy_b(out, &x, 8);
		return 8;
	}
	case BFS_BTREE_FLOAT_TYPE: {
		double d;
		float x;
		bfs_q_strtod(tmp, &d);
		x = (float)d;
		memcpy_b(out, &x, 4);
		return 4;
	}
	case BFS_BTREE_DOUBLE_TYPE: {
		double x;
		bfs_q_strtod(tmp, &x);
		memcpy_b(out, &x, 8);
		return 8;
	}
	default:
		return -EINVAL;	/* STRING values stay raw */
	}
}

/* ---- pattern matching for STRING '=' / '!=' (Haiku's matchString:
 * '*' any run, '?' one char, '[...]' a class with ^/! inversion and
 * a-b ranges; '\\' escapes the next char) ------------------------ */

static int bfs_q_class_match(const char **pp, char c)
{
	const char *p = *pp;
	int invert = 0, matched = 0, first = 1;

	if(*p == '^' || *p == '!') {
		invert = 1;
		p++;
	}
	while(*p && *p != ']') {
		char lo = *p;
		if(p[1] == '-' && p[2] && p[2] != ']') {
			char hi = p[2];
			if(c >= lo && c <= hi) {
				matched = 1;
			}
			p += 3;
		} else {
			if(c == lo) {
				matched = 1;
			}
			p++;
		}
		first = 0;
	}
	if(*p != ']') {
		return -1;	/* unterminated class */
	}
	*pp = p + 1;
	(void)first;
	return (invert ? !matched : matched);
}

static int bfs_q_pattern_match(const char *pat, const char *s)
{
	for(;;) {
		switch(*pat) {
		case 0:
			return (*s == 0);
		case '*':
			while(*pat == '*') {
				pat++;
			}
			if(!*pat) {
				return 1;
			}
			for(; *s; s++) {
				if(bfs_q_pattern_match(pat, s)) {
					return 1;
				}
			}
			return 0;
		case '?':
			if(!*s) {
				return 0;
			}
			pat++;
			s++;
			break;
		case '[': {
			int m;
			pat++;
			if(!*s) {
				return 0;
			}
			if((m = bfs_q_class_match(&pat, *s)) < 0) {
				return 0;
			}
			if(!m) {
				return 0;
			}
			s++;
			break;
		}
		case '\\':
			if(pat[1]) {
				pat++;
			}
			/* fall through */
		default:
			if(*pat != *s) {
				return 0;
			}
			pat++;
			s++;
			break;
		}
	}
}

static int bfs_q_is_pattern(const char *v, int vlen)
{
	int i;

	for(i = 0; i < vlen; i++) {
		if(v[i] == '*' || v[i] == '?' || v[i] == '[') {
			return 1;
		}
	}
	return 0;
}

/* ---- the per-equation index walk -------------------------------- */

struct bfs_q_ctx {
	struct bfs_qnode *node;
	int dtype;
	char key[BFS_BTREE_MAX_KEY_LEN];
	int keylen;
	int use_pattern;
	struct bfs_qset set;
};

/* does the stored key satisfy (op, value)? */
static int bfs_q_key_matches(struct bfs_q_ctx *c, const char *key, int keylen)
{
	struct bfs_qnode *n = c->node;
	int cmp;

	if(c->dtype == BFS_BTREE_STRING_TYPE) {
		if((n->op == BQ_OP_EQUAL || n->op == BQ_OP_NOT_EQUAL)
		   && c->use_pattern) {
			char v[BFS_BTREE_MAX_KEY_LEN + 1];
			int m;

			memcpy_b(v, n->value, n->vlen);
			v[n->vlen] = 0;
			{
				char k[BFS_BTREE_MAX_KEY_LEN + 1];
				memcpy_b(k, key, keylen);
				k[keylen] = 0;
				m = bfs_q_pattern_match(v, k);
			}
			return (n->op == BQ_OP_EQUAL) ? m : !m;
		}
		cmp = strncmp(key, n->value, (keylen < n->vlen) ? keylen : n->vlen);
		if(cmp == 0) {
			cmp = keylen - n->vlen;
		}
	} else {
		/* numeric: compare the stored key with the parsed value */
		__s64 s1 = 0, s2 = 0;
		__u64 u1 = 0, u2 = 0;
		int sz = bfs_qtype_size(c->dtype);

		if(keylen != sz) {
			return 0;
		}
		switch(c->dtype) {
		case BFS_BTREE_INT8_TYPE:
			memcpy_b(&s1, key, 1);
			memcpy_b(&s2, c->key, 1);
			cmp = (s1 < s2) ? -1 : (s1 > s2) ? 1 : 0;
			break;
		case BFS_BTREE_INT16_TYPE:
			memcpy_b(&s1, key, 2);
			memcpy_b(&s2, c->key, 2);
			cmp = (s1 < s2) ? -1 : (s1 > s2) ? 1 : 0;
			break;
		case BFS_BTREE_INT32_TYPE:
			memcpy_b(&s1, key, 4);
			memcpy_b(&s2, c->key, 4);
			cmp = (s1 < s2) ? -1 : (s1 > s2) ? 1 : 0;
			break;
		case BFS_BTREE_INT64_TYPE:
			memcpy_b(&s1, key, 8);
			memcpy_b(&s2, c->key, 8);
			cmp = (s1 < s2) ? -1 : (s1 > s2) ? 1 : 0;
			break;
		case BFS_BTREE_UINT32_TYPE:
			memcpy_b(&u1, key, 4);
			memcpy_b(&u2, c->key, 4);
			cmp = (u1 < u2) ? -1 : (u1 > u2) ? 1 : 0;
			break;
		case BFS_BTREE_UINT64_TYPE:
			memcpy_b(&u1, key, 8);
			memcpy_b(&u2, c->key, 8);
			cmp = (u1 < u2) ? -1 : (u1 > u2) ? 1 : 0;
			break;
		case BFS_BTREE_FLOAT_TYPE: {
			float f1, f2;
			memcpy_b(&f1, key, 4);
			memcpy_b(&f2, c->key, 4);
			cmp = (f1 < f2) ? -1 : (f1 > f2) ? 1 : 0;
			break;
		}
		case BFS_BTREE_DOUBLE_TYPE: {
			double d1, d2;
			memcpy_b(&d1, key, 8);
			memcpy_b(&d2, c->key, 8);
			cmp = (d1 < d2) ? -1 : (d1 > d2) ? 1 : 0;
			break;
		}
		default:
			return 0;
		}
	}

	switch(n->op) {
	case BQ_OP_EQUAL:
		return cmp == 0;
	case BQ_OP_NOT_EQUAL:
		return cmp != 0;
	case BQ_OP_GREATER_THAN:
		return cmp > 0;
	case BQ_OP_GREATER_THAN_OR_EQUAL:
		return cmp >= 0;
	case BQ_OP_LESS_THAN:
		return cmp < 0;
	case BQ_OP_LESS_THAN_OR_EQUAL:
		return cmp <= 0;
	}
	return 0;
}

static int bfs_q_collect_cb(const char *key, int keylen, __ino_t ino, void *arg)
{
	struct bfs_q_ctx *c = (struct bfs_q_ctx *)arg;

	if(bfs_q_key_matches(c, key, keylen)) {
		return bfs_qset_add(&c->set, (__u32)ino);
	}
	return 0;
}

/* the index file's key type from its raw inode mode (Haiku's stat
 * bits). i_mode itself is only 16 bits (__mode_t) — the index type
 * bits live in the top half of the raw 32-bit mode */
static int bfs_q_index_dtype(struct inode *idx)
{
	__u32 m = idx->u.bfs.raw.mode;

	if(m & BFS_S_STR_INDEX) {
		return BFS_BTREE_STRING_TYPE;
	}
	if(m & BFS_S_INT_INDEX) {
		return BFS_BTREE_INT32_TYPE;
	}
	if(m & BFS_S_UINT_INDEX) {
		return BFS_BTREE_UINT32_TYPE;
	}
	if(m & BFS_S_LONG_LONG_INDEX) {
		return BFS_BTREE_INT64_TYPE;
	}
	if(m & BFS_S_ULONG_LONG_INDEX) {
		return BFS_BTREE_UINT64_TYPE;
	}
	if(m & BFS_S_FLOAT_INDEX) {
		return BFS_BTREE_FLOAT_TYPE;
	}
	if(m & BFS_S_DOUBLE_INDEX) {
		return BFS_BTREE_DOUBLE_TYPE;
	}
	return -EINVAL;
}

/* resolve 'attr' to its index file inode (iget'd) */
static struct inode *bfs_q_find_index(struct superblock *sb, const char *attr,
				      int *dtype)
{
	struct inode *dir, *idx;
	__ino_t ino;

	if(!sb->u.bfs.indices_inode) {
		return NULL;
	}
	if(!(dir = iget(sb, sb->u.bfs.indices_inode))) {
		return NULL;
	}
	ino = 0;
	bfs_btree_find(dir, attr, &ino);
	iput(dir);
	if(!ino) {
		return NULL;
	}
	if(!(idx = iget(sb, ino))) {
		return NULL;
	}
	if((*dtype = bfs_q_index_dtype(idx)) < 0) {
		iput(idx);
		return NULL;
	}
	return idx;
}

/*
 * Evaluate one equation into a set: walk the attr's index tree and
 * keep the inodes whose key satisfies (op, value). A missing index
 * yields the empty set.
 */
static int bfs_q_eval_formula(struct superblock *sb, struct bfs_qnode *n,
			      struct bfs_qset *out)
{
	struct inode *idx;
	struct bfs_q_ctx c;
	int res;

	memset_b(&c, 0, sizeof(c));
	c.node = n;
	c.dtype = BFS_BTREE_STRING_TYPE;
	/* c.set is zeroed by memset_b; chunks allocate on demand */
	res = 0;

	idx = bfs_q_find_index(sb, n->attr, &c.dtype);
	if(!idx) {
		bfs_qset_free(&c.set);
		*out = c.set;
		return 0;	/* unknown index: empty set */
	}

	if(c.dtype == BFS_BTREE_STRING_TYPE) {
		/* string values stay raw; patterns only for = / != */
		memcpy_b(c.key, n->value, n->vlen);
		c.keylen = n->vlen;
		c.use_pattern = (n->op == BQ_OP_EQUAL || n->op == BQ_OP_NOT_EQUAL)
			&& bfs_q_is_pattern(n->value, n->vlen);
	} else {
		if((c.keylen = bfs_q_parse_value(c.dtype, n->value, n->vlen,
						 c.key)) < 0) {
			iput(idx);
			bfs_qset_free(&c.set);
			return -EINVAL;
		}
		c.use_pattern = 0;
	}

	res = bfs_btree_iterate_values(idx, c.dtype, bfs_q_collect_cb, &c);
	iput(idx);
	if(res < 0) {
		bfs_qset_free(&c.set);
		return res;
	}
	*out = c.set;
	return 0;
}

/* ---- the parser (recursive descent) ---------------------------- */

static const char *bfs_q_skip(const char *p)
{
	while(*p == ' ' || *p == '\t' || *p == '\n') {
		p++;
	}
	return p;
}

/* DeMorgan negation of a whole subtree (Haiku's Complement) */
static int bfs_q_negate(struct bfs_qnode *n);

static struct bfs_qnode *bfs_q_new(int type)
{
	struct bfs_qnode *n;

	if(!(n = (struct bfs_qnode *)kmalloc(sizeof(struct bfs_qnode)))) {
		return NULL;
	}
	memset_b(n, 0, sizeof(*n));
	n->type = type;
	return n;
}

static int bfs_q_negate(struct bfs_qnode *n)
{
	struct bfs_qnode *t;

	if(!n) {
		return -ENOMEM;
	}
	if(n->type == BQ_FORMULA) {
		switch(n->op) {
		case BQ_OP_EQUAL:
			n->op = BQ_OP_NOT_EQUAL;
			break;
		case BQ_OP_NOT_EQUAL:
			n->op = BQ_OP_EQUAL;
			break;
		case BQ_OP_GREATER_THAN:
			n->op = BQ_OP_LESS_THAN_OR_EQUAL;
			break;
		case BQ_OP_GREATER_THAN_OR_EQUAL:
			n->op = BQ_OP_LESS_THAN;
			break;
		case BQ_OP_LESS_THAN:
			n->op = BQ_OP_GREATER_THAN_OR_EQUAL;
			break;
		case BQ_OP_LESS_THAN_OR_EQUAL:
			n->op = BQ_OP_GREATER_THAN;
			break;
		}
		return 0;
	}
	/* DeMorgan: !(A && B) = !A || !B, !(A || B) = !A && !B */
	if((bfs_q_negate(n->l)) < 0) {
		return -ENOMEM;
	}
	if((bfs_q_negate(n->r)) < 0) {
		return -ENOMEM;
	}
	t = n->l;
	n->l = n->r;
	n->r = t;
	n->type = (n->type == BQ_AND) ? BQ_OR : BQ_AND;
	return 0;
}

static void bfs_q_free(struct bfs_qnode *n)
{
	if(!n) {
		return;
	}
	if(n->type != BQ_FORMULA) {
		bfs_q_free(n->l);
		bfs_q_free(n->r);
	}
	kfree((addr_t)n);
}

static struct bfs_qnode *bfs_q_parse_expr(const char **pp, int *err, int depth);

/* equation: attr op value */
static struct bfs_qnode *bfs_q_parse_formula(const char **pp, int *err)
{
	const char *p = *pp;
	struct bfs_qnode *n;
	char *q;
	int len;

	if(!(n = bfs_q_new(BQ_FORMULA))) {
		*err = -ENOMEM;
		return NULL;
	}
	q = n->attr;
	while(*p && *p != '=' && *p != '!' && *p != '>' && *p != '<'
	      && *p != ' ' && *p != '\t' && *p != '(' && *p != ')' ) {
		if(q - n->attr >= BFS_BTREE_MAX_KEY_LEN - 1) {
			*err = -EINVAL;
			bfs_q_free(n);
			return NULL;
		}
		*q++ = *p++;
	}
	*q = 0;
	p = bfs_q_skip(p);
	if(!*p) {
		*err = -EINVAL;
		bfs_q_free(n);
		return NULL;
	}
	if(*p == '=') {
		n->op = BQ_OP_EQUAL;
		p++;
	} else if(*p == '!' && p[1] == '=') {
		n->op = BQ_OP_NOT_EQUAL;
		p += 2;
	} else if(*p == '>' && p[1] == '=') {
		n->op = BQ_OP_GREATER_THAN_OR_EQUAL;
		p += 2;
	} else if(*p == '>') {
		n->op = BQ_OP_GREATER_THAN;
		p++;
	} else if(*p == '<' && p[1] == '=') {
		n->op = BQ_OP_LESS_THAN_OR_EQUAL;
		p += 2;
	} else if(*p == '<') {
		n->op = BQ_OP_LESS_THAN;
		p++;
	} else {
		*err = -EINVAL;
		bfs_q_free(n);
		return NULL;
	}
	p = bfs_q_skip(p);
	if(!*p) {
		*err = -EINVAL;
		bfs_q_free(n);
		return NULL;
	}
	/* the value: quoted (' or ") or a bare run */
	q = n->value;
	if(*p == '\'' || *p == '"') {
		char quote = *p++;
		while(*p && *p != quote) {
			if(*p == '\\' && p[1]) {
				p++;
			}
			if(q - n->value >= BFS_BTREE_MAX_KEY_LEN - 1) {
				*err = -EINVAL;
				bfs_q_free(n);
				return NULL;
			}
			*q++ = *p++;
		}
		if(*p != quote) {
			*err = -EINVAL;
			bfs_q_free(n);
			return NULL;
		}
		p++;
	} else {
		while(*p && *p != ' ' && *p != '\t' && *p != ')'
		      && *p != '(') {
			if(q - n->value >= BFS_BTREE_MAX_KEY_LEN - 1) {
				*err = -EINVAL;
				bfs_q_free(n);
				return NULL;
			}
			*q++ = *p++;
		}
	}
	len = (int)(q - n->value);
	n->vlen = len;
	*pp = p;
	return n;
}

static struct bfs_qnode *bfs_q_parse_term(const char **pp, int *err, int depth)
{
	const char *p = bfs_q_skip(*pp);
	struct bfs_qnode *n;

	if(*p == '(') {
		if(depth >= 32) {
			/* deep paren nesting would recurse the 4KB kernel
			 * stack; cap it (Haiku queries are small) */
			*err = -EINVAL;
			return NULL;
		}
		p++;
		n = bfs_q_parse_expr(&p, err, depth + 1);
		if(!n) {
			return NULL;
		}
		p = bfs_q_skip(p);
		if(*p != ')') {
			*err = -EINVAL;
			bfs_q_free(n);
			return NULL;
		}
		p++;
		*pp = p;
		return n;
	}
	if(*p == '!') {
		/* '!' only negates a parenthesized term (Haiku) */
		p++;
		p = bfs_q_skip(p);
		if(*p != '(') {
			*err = -EINVAL;
			return NULL;
		}
		if(depth >= 32) {
			*err = -EINVAL;
			return NULL;
		}
		p++;
		n = bfs_q_parse_expr(&p, err, depth + 1);
		if(!n) {
			return NULL;
		}
		p = bfs_q_skip(p);
		if(*p != ')') {
			*err = -EINVAL;
			bfs_q_free(n);
			return NULL;
		}
		p++;
		if((*err = bfs_q_negate(n)) < 0) {
			bfs_q_free(n);
			return NULL;
		}
		*pp = p;
		return n;
	}
	/* an equation */
	n = bfs_q_parse_formula(&p, err);
	if(!n) {
		return NULL;
	}
	*pp = p;
	return n;
}

static struct bfs_qnode *bfs_q_parse_and(const char **pp, int *err, int depth)
{
	struct bfs_qnode *n, *r;

	n = bfs_q_parse_term(pp, err, depth);
	if(!n) {
		return NULL;
	}
	for(;;) {
		const char *p = bfs_q_skip(*pp);

		if(p[0] == '&' && p[1] == '&') {
			struct bfs_qnode *a;

			p += 2;
			*pp = p;
			if(!(r = bfs_q_parse_term(pp, err, depth))) {
				bfs_q_free(n);
				return NULL;
			}
			if(!(a = bfs_q_new(BQ_AND))) {
				*err = -ENOMEM;
				bfs_q_free(n);
				bfs_q_free(r);
				return NULL;
			}
			a->l = n;
			a->r = r;
			n = a;
		} else {
			break;
		}
	}
	return n;
}

static struct bfs_qnode *bfs_q_parse_expr(const char **pp, int *err, int depth)
{
	struct bfs_qnode *n, *r;

	n = bfs_q_parse_and(pp, err, depth);
	if(!n) {
		return NULL;
	}
	for(;;) {
		const char *p = bfs_q_skip(*pp);

		if(p[0] == '|' && p[1] == '|') {
			struct bfs_qnode *a;

			p += 2;
			*pp = p;
			if(!(r = bfs_q_parse_and(pp, err, depth))) {
				bfs_q_free(n);
				return NULL;
			}
			if(!(a = bfs_q_new(BQ_OR))) {
				*err = -ENOMEM;
				bfs_q_free(n);
				bfs_q_free(r);
				return NULL;
			}
			a->l = n;
			a->r = r;
			n = a;
		} else {
			break;
		}
	}
	return n;
}

/* ---- the public entry point ------------------------------------ */

static int bfs_q_eval(struct superblock *sb, struct bfs_qnode *n,
		      struct bfs_qset *out)
{
	struct bfs_qset s1, s2;
	int res;

	if(n->type == BQ_FORMULA) {
		return bfs_q_eval_formula(sb, n, out);
	}
	if((res = bfs_q_eval(sb, n->l, &s1)) < 0) {
		return res;
	}
	if((res = bfs_q_eval(sb, n->r, &s2)) < 0) {
		bfs_qset_free(&s1);
		return res;
	}
	if(n->type == BQ_AND) {
		bfs_qset_and(&s1, &s2);
		bfs_qset_free(&s2);
		*out = s1;
		return 0;
	}
	/* BQ_OR: build a fresh merged set (the result can outgrow s1) */
	return bfs_qset_or(&s1, &s2, out);
}

/*
 * Evaluate a query string against the volume 'sb'. Fills 'inos'
 * (capacity 'cap', inode numbers, sorted) and returns the TOTAL number
 * of matches (which may exceed 'cap').
 */
int bfs_query(struct superblock *sb, const char *q, __u32 *inos, __u32 cap)
{
	const char *p = q;
	struct bfs_qnode *tree;
	struct bfs_qset set;
	int err, res;
	__u32 n;

	/* the value of an empty query is the empty set */
	if(!q || !*q || !bfs_q_skip(q)[0]) {
		return 0;
	}
	tree = bfs_q_parse_expr(&p, &err, 0);
	if(!tree) {
		return err;
	}
	p = bfs_q_skip(p);
	if(*p) {
		bfs_q_free(tree);
		return -EINVAL;	/* trailing garbage */
	}
	if((res = bfs_q_eval(sb, tree, &set)) < 0) {
		bfs_q_free(tree);
		return res;
	}
	bfs_q_free(tree);

	n = (set.count < cap) ? set.count : cap;
	{
		struct bfs_qchunk *c = set.head;
		__u32 done = 0;

		while(c && done < n) {
			__u32 k = c->count;

			if(done + k > n) {
				k = n - done;
			}
			memcpy_b(&inos[done], c->inos, k * sizeof(__u32));
			done += k;
			c = c->next;
		}
	}
	res = (int)set.count;
	bfs_qset_free(&set);
	return res;
}
