/* argentum/text_utf8.h — shared UTF-8 byte-boundary helpers used by
 * the text-edit views (TextField, TextView). TXT-b
 * (docs/design/argentum-textview.md): TextField's own copies moved
 * here so the two files use one implementation. Operate on a
 * null-terminated string (a char[] or a std::string::c_str()).
 */
#ifndef FNX_ARGENTUM_TEXT_UTF8_H
#define FNX_ARGENTUM_TEXT_UTF8_H

namespace argentum {

static inline bool
utf8IsCont(unsigned char c)
{
	return (c & 0xc0) == 0x80;
}

/* index of the character START before `at` (at must be a boundary) */
static inline unsigned int
prevCharStart(const char *s, unsigned int at)
{
	unsigned int p = at;

	if (p == 0) {
		return 0;
	}
	p--;
	while (p > 0 && utf8IsCont((unsigned char) s[p])) {
		p--;
	}
	return p;
}

/* index just past the character STARTING at `at` */
static inline unsigned int
nextCharEnd(const char *s, unsigned int at)
{
	unsigned int n = at;

	if (s[n] == 0) {
		return n;
	}
	n++;
	while (s[n] && utf8IsCont((unsigned char) s[n])) {
		n++;
	}
	return n;
}

} /* namespace argentum */

#endif /* FNX_ARGENTUM_TEXT_UTF8_H */
