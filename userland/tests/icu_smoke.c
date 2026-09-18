/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * icu_smoke — F13's acceptance: the guest LOADS the staged ICU, and its DATA.
 * docs/design/foundation-plan.md §10.
 *
 * WHY A C PROBE, when every other acceptance in this plan is Objective-C: ICU is a
 * C library and this exercises the DATA path, not the object layer. It answers one
 * question before any class is bound to ICU — does the guest resolve
 * libicuuc/libicui18n/libicudata from /System/Libraries and get REAL DATA out of
 * them?
 *
 * SO EVERY CHECK ASKS FOR A DATA-DEPENDENT ANSWER, and that is the design rule
 * here: nothing below can pass from constants compiled into this file.
 *   * three locales format the SAME number differently — de_DE groups with '.',
 *     en_US with ',', and ar_EG writes it in non-ASCII digits (§10 found the
 *     separators and digits are data);
 *   * a date goes through a locale's own medium-date pattern AND through an
 *     explicit "yyyy-MM-dd" pattern in UTC;
 *   * the German and Swedish collations DISAGREE about 'ö' vs 'o' — that is the
 *     CLDR rule, not ours, and no single answer satisfies both;
 *   * the time-zone ID set is ENUMERATED, because F7 refused "the IANA
 *     identifiers ARE the database" — this is that database arriving.
 */

#include <stdio.h>
#include <string.h>

#include <unicode/utypes.h>
#include <unicode/uversion.h>
#include <unicode/ustring.h>
#include <unicode/uenum.h>
#include <unicode/unum.h>
#include <unicode/udat.h>
#include <unicode/ucol.h>
#include <unicode/ucal.h>

static int okc, failc;

static void check(const char *name, int ok, const char *detail)
{
	if (ok) {
		okc++;
		printf("ICU-SMOKE %s ok\n", name);
	} else {
		failc++;
		printf("ICU-SMOKE %s FAIL %s\n", name, detail ? detail : "");
	}
}

/* UTF-16 out of ICU, UTF-8 onto the wire. `dest` is returned so a caller can put
 * the MEASUREMENT in a check's detail rather than a boolean. */
static const char *utf8(const UChar *s, int32_t length, char *dest, int cap)
{
	UErrorCode status = U_ZERO_ERROR;
	int32_t used = 0;

	dest[0] = 0;
	u_strToUTF8(dest, cap, &used, s, length, &status);
	return dest;
}

int main(void)
{
	UErrorCode status = U_ZERO_ERROR;
	UVersionInfo version;
	char versionText[U_MAX_VERSION_STRING_LENGTH];
	char text[128];

	/* The version is a libicuuc SYMBOL rather than data, and it is here to name WHICH
	 * ICU answered every data question below — the runtime's own rendering of it, not
	 * this file's idea of it. */
	u_getVersion(version);
	u_versionToString(version, versionText);
	check("icu-version", version[0] == 76, versionText);

	{
		/* ONE NUMBER, THREE LOCALES. The grouping separator and the digits come
		 * from libicudata, so the three answers cannot be the same constant. */
		static const int64_t number = 1234567;
		struct { const char *locale; const char *expect; int nonAscii; } cases[3] = {
			{ "de_DE", "1.234.567", 0 },
			{ "en_US", "1,234,567", 0 },
			{ "ar_EG", "",          1 },	/* the digits themselves differ */
		};
		int i;

		for (i = 0; i < 3; i++) {
			UNumberFormat *fmt;
			UChar buf[64];
			int32_t n;
			char name[32];
			int good;

			status = U_ZERO_ERROR;
			fmt = unum_open(UNUM_DECIMAL, NULL, 0, cases[i].locale, NULL, &status);
			if (U_FAILURE(status) || fmt == NULL) {
				snprintf(name, sizeof name, "icu-num-%.2s", cases[i].locale);
				check(name, 0, u_errorName(status));
				continue;
			}
			n = unum_formatInt64(fmt, number, buf, 64, NULL, &status);
			utf8(buf, n, text, sizeof text);
			unum_close(fmt);

			/* ASCII cases are compared exactly; the non-ASCII case is asserted on
			 * the property that matters (the digits are NOT ASCII), because
			 * hard-coding an Arabic-Indic numeral here would be a table in the
			 * probe — the very thing this project binds ICU to avoid. */
			if (cases[i].nonAscii) {
				int j, seen = 0;

				for (j = 0; text[j] != 0; j++) {
					if ((unsigned char)text[j] >= 0x80) {
						seen = 1;
						break;
					}
				}
				good = seen;
			} else {
				good = strcmp(text, cases[i].expect) == 0;
			}
			/* The check name is the SHORT locale code (de/en/ar): the locale in full is
			 * the INPUT, and the name is the handle the case file matches on. */
			snprintf(name, sizeof name, "icu-num-%.2s", cases[i].locale);
			check(name, good, text);
		}
	}

	{
		/* A DATE, two ways. 2021-03-04T00:00:00Z = 1614816000 s. */
		const UDate when = 1614816000000.0;
		UChar usize[] = { 'y', 'y', 'y', 'y', '-', 'M', 'M', '-', 'd', 'd' };
		UChar utc[] = { 'U', 'T', 'C' };
		UDateFormat *df;
		UChar buf[64];
		int32_t n;

		status = U_ZERO_ERROR;
		/* UDAT_PATTERN, and this cost a round trip to learn: udat_open's first branch is
		 * `if (timeStyle != UDAT_PATTERN)`, which builds a STYLE formatter and IGNORES the
		 * pattern argument entirely (measured: UDAT_NONE printed "20210304 12:00 AM" for a
		 * "yyyy-MM-dd" pattern). Passing UDAT_PATTERN is what selects the branch that
		 * makes a SimpleDateFormat from the pattern. */
		df = udat_open(UDAT_PATTERN, UDAT_NONE, "en_US", utc, 3, usize, 10, &status);
		if (U_FAILURE(status) || df == NULL) {
			check("icu-dat-pattern", 0, u_errorName(status));
		} else {
			n = udat_format(df, when, buf, 64, NULL, &status);
			utf8(buf, n, text, sizeof text);
			udat_close(df);
			check("icu-dat-pattern", strcmp(text, "2021-03-04") == 0, text);
		}

		/* The locale's OWN medium date: the pattern itself is CLDR data. The check
		 * asserts the parts it must contain rather than one exact rendering, so it
		 * measures the data without freezing a punctuation convention. */
		status = U_ZERO_ERROR;
		df = udat_open(UDAT_NONE, UDAT_MEDIUM, "en_US", utc, 3, NULL, 0, &status);
		if (U_FAILURE(status) || df == NULL) {
			check("icu-dat-locale", 0, u_errorName(status));
		} else {
			n = udat_format(df, when, buf, 64, NULL, &status);
			utf8(buf, n, text, sizeof text);
			udat_close(df);
			check("icu-dat-locale",
			      strstr(text, "Mar") != NULL && strstr(text, "2021") != NULL,
			      text);
		}
	}

	{
		/* THE COLLATION RULES DISAGREE BETWEEN LOCALES: German groups 'ö' with
		 * 'o' at primary strength, Swedish keeps it a letter of its own after 'z'.
		 * One rule cannot produce both answers, so this measures the DATA. */
		UChar oe[] = { 0x00F6 };	/* ö */
		UChar o[] = { 0x006F };		/* o */
		UCollator *de;
		UCollator *sv;
		UCollationResult rde;
		UCollationResult rsv;

		status = U_ZERO_ERROR;
		de = ucol_open("de_DE", &status);
		sv = ucol_open("sv_SE", &status);
		if (U_FAILURE(status) || de == NULL || sv == NULL) {
			check("icu-coll-de", 0, u_errorName(status));
			check("icu-coll-sv", 0, u_errorName(status));
		} else {
			ucol_setStrength(de, UCOL_PRIMARY);
			ucol_setStrength(sv, UCOL_PRIMARY);
			rde = ucol_strcoll(de, oe, 1, o, 1);
			rsv = ucol_strcoll(sv, oe, 1, o, 1);
			snprintf(text, sizeof text, "de=%d sv=%d", (int)rde, (int)rsv);
			check("icu-coll-de", rde == UCOL_EQUAL, text);
			check("icu-coll-sv", rsv != UCOL_EQUAL, text);
			ucol_close(de);
			ucol_close(sv);
		}
	}

	{
		/* THE TIME-ZONE DATABASE, which F7 refused by name as "the IANA
		 * identifiers ARE the database". Here it is: enumerated, counted, and one
		 * example id printed so the detail carries the measurement. */
		UEnumeration *zones;

		status = U_ZERO_ERROR;
		zones = ucal_openTimeZoneIDEnumeration(UCAL_ZONE_TYPE_ANY, NULL, NULL, &status);
		if (U_FAILURE(status) || zones == NULL) {
			check("icu-tz-ids", 0, u_errorName(status));
		} else {
			int32_t count = uenum_count(zones, &status);
			int32_t len = 0;
			const char *first = uenum_next(zones, &len, &status);

			snprintf(text, sizeof text, "count=%d first=%s", (int)count,
				 first != NULL ? first : "(none)");
			check("icu-tz-ids", count > 100, text);
			uenum_close(zones);
		}
	}

	printf("ICU-SMOKE RESULT ok=%d fail=%d\n", okc, failc);
	printf("ICU-SMOKE DONE\n");
	return failc ? 1 : 0;
}
