# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""The Foundation's strings — F1's acceptance.

docs/design/foundation-plan.md §5 (F1) and §9. The probe is
`/System/Shared/tests/foundation_string`, built from TWO translation units, and
its checks are chosen to cover the two representations clang produces for a
literal:

  * `tiny`        — a literal of FEWER THAN 9 ASCII characters is a TAGGED
                    POINTER (clang packs the characters into the pointer); it is
                    only readable because the Foundation registers a class at
                    the tag the runtime dispatches through. That registration is
                    the whole reason F1 was blocked, so it is asserted directly;
  * `owned`       — a long literal is a real object, and equals an owned string;
  * `mixed`       — a tagged string and an owned one are the SAME value (equal
                    and equal-hashing), which is what makes them interchangeable;
  * `utf8`        — `-length` counts UTF-16 CODE UNITS (the byte count has its
                    own door), with `-characterAtIndex:` indexing units;
  * `mutable`     — `NSMutableString` mutation, and `-copy` returning a snapshot;
  * `description` — the root class's names its class, an override wins, and a
                    string describes itself;
  * `cross-tu`    — a constant string defined in the other translation unit;
  * `class-format-arguments` — the class-side `+stringWithFormat:arguments:` must
                    consume a COPY of the caller's `va_list` (C99 7.15.1.4), so
                    TWO renders from one caller list have to agree. That is the
                    contract the F4 crash was an instance of, and the check the
                    probe was missing when the crash was recorded as unexplained.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_string"
CHECKS = (
          "tiny", "owned", "mixed", "utf8", "mutable", "description", "characters-family", "characters-family-ownership", "cross-tu", "string-api-complete", "string-format", "string-format-tagged-object", "class-format-arguments", "string-compare", "string-transform", "string-convert", "string-path", "string-path-doors", "string-encoding", "string-smallest-encoding", "string-fastest-encoding", "string-default-cstring-encoding", "string-available-encodings", "string-encoding-introspection", "string-file-contents-doors", "string-url-contents-doors", "string-write-refuses-unstored-encoding", "string-init-format-locale", "string-init-format-locale-arguments", "string-localized-string-with-format", "string-mutable", "characterset-api-complete", "locale-basics", "locale-turkic-upper", "locale-turkic-lower", "locale-turkic-neutral", "locale-literal-high-byte", "locale-turkic-compare", "locale-boundaries", "locale-current", "locale-api-complete", "charset-bitmap-and-planes", "charset-illegal",
          "charset-symbols", "charset-titled", "charset-non-base", "charset-decomposable",
          "charset-whitespace-family", "charset-letter-family",
          "charset-punct-and-control", "locale-display-names",
          "locale-keys-are-their-own-names",
          "option-set-members-are-distinct-bits", "transforms-and-keys-are-their-own-names",
          "line-crlf-is-one-terminator", "line-after-crlf-starts-past-both-units",
          "line-nel-terminates", "line-starts-after-nel", "line-ls-and-ps-terminate",
          "line-range-includes-the-terminator", "line-range-is-the-line-containing-the-range",
          "line-enumerator-drops-the-terminator", "line-enumerator-keeps-a-blank-line",
          "line-enumerator-has-no-trailing-empty-line",
          "line-enumerator-of-an-empty-string-yields-nothing", "line-enumerator-honours-stop",
          "line-invalid-range-raises",
          "set-search-no-options-door-survives", "set-search-backwards-answers-the-last",
          "set-search-anchored-forward", "set-search-anchored-does-not-scan",
          "set-search-anchored-backward-at-the-end",
          "set-search-anchored-backward-elsewhere-is-not-a-match",
          "set-search-range-limits-the-scan", "set-search-not-found-answers-notfound",
          "set-search-does-not-normalize", "set-search-nil-set-raises",
          "set-search-invalid-range-raises",
          "common-prefix-stops-where-they-part", "common-prefix-is-the-receiver-characters",
          "common-prefix-of-equals-is-the-whole-string", "common-prefix-of-strangers-is-empty",
          "common-prefix-nil-argument-is-empty",
          "percent-encodes-what-the-set-excludes", "percent-leaves-the-allowed-characters-alone",
          "percent-encodes-by-utf8-bytes", "percent-ignores-a-non-ascii-set-member",
          "percent-encoding-with-no-set-is-nil", "percent-decodes",
          "percent-decodes-lowercase-hex", "percent-round-trips",
          "percent-decode-nil-on-a-bad-digit", "percent-decode-nil-on-a-truncated-tail",
          "percent-decode-nil-on-non-utf8", "percent-decode-nil-on-an-overlong-form",
          "percent-decode-leaves-plain-text-alone",
          "composed-sequence-of-a-plain-character", "composed-sequence-from-the-mark-walks-back-to-the-base",
          "composed-sequence-from-the-base-covers-its-marks", "composed-sequence-stops-at-a-bmp-set-boundary",
          "composed-sequence-splits-crlf", "composed-sequence-splits-a-flag-pair",
          "composed-sequence-at-the-end-is-empty",
          "composed-sequences-range-grows-back-to-the-base",
          "composed-sequences-range-grows-forward-over-the-marks",
          "composed-sequences-empty-range-is-the-sequence-at-its-location",
          "composed-sequence-index-past-the-end-raises", "composed-sequences-range-past-the-end-raises",
          # §63.24: Unicode normalization — Apple's four forms, measured on meaning rather than on lengths.
          "normalization-canonical-composes-and-decomposes",
          "normalization-compatibility-folds-what-canonical-keeps",
          "string-transform-and-folding",
          # §63.25: the locale-aware case doors — one live door and three deprecated spellings.
          "case-capitalization-is-locale-aware",
          # §63.26: the localised search doors — case AND diacritics, and where the two doors differ.
          "localized-search-folds-case-and-diacritics",
          # §63.27: the validated-format pair — a format checked against the specifiers the caller names.
          "validated-format-allows-only-the-listed-specifiers",
          "validated-format-instance-doors",
          # §63.28: creation from a C string with an encoding — the mirror of -cStringUsingEncoding:.
          "cstring-with-encoding-refuses-what-it-cannot-store",
          "cstring-init-doors",
          # §63.29: the two URL forms — a file URL round trip, and a scheme with nothing behind it refused.
          "url-doors-round-trip-a-file-url-and-refuse-an-unreachable-scheme",
          # §63.30: the C-string/characters copy doors, and encoding introspection over the UTF-8 storage.
          "cstring-doors-convert",
          "cstring-doors-copy-byte-for-byte",
          "cstring-doors-refuse-what-cannot-fit",
          "cstring-without-encoding-round-trips",
          "getcharacters-copies-every-unit",
          "can-be-converted-follows-the-storage",
          "maximum-length-follows-the-storage",
          # the 2026-09-30 locale slice: NSLocale reads ICU's data (its own probe checks live in
          # foundation_string.m, in the locale area next to locale-basics and locale-display-names).
          "locale-direction-follows-the-script",
          "locale-windows-locale-code-round-trips",
          "locale-localized-string-names-a-language",
          "locale-localized-string-names-a-country",
          "locale-localized-string-names-a-currency",
          "locale-localized-string-names-a-calendar",
          "locale-separators-come-from-the-locale",
          "locale-quotation-delimiters-are-the-locales",
          "locale-currency-code-and-symbol-are-the-locales",
          "locale-calendar-and-collation-come-from-the-locale",
          "locale-exemplar-set-is-the-locales-alphabet",
          "locale-measurement-system-is-the-locales",
          "locale-iso-catalogues-are-populated",
          "locale-sources-answer",
          "locale-subtags-are-the-identifiers-own",
          "locale-data-keys-stay-nil-through-the-two-narrow-doors",
          "locale-variant-display-stays-open",
                    # §63.46: the paragraph pair, and the engine ALIGNED to Apple's three-character rule.
          "paragraph-door-three-out-parameters",
          "paragraph-blank-line-is-an-empty-paragraph",
          "nel-and-ls-end-a-line-but-not-a-paragraph",
          "paragraphs-via-enumeration-match-the-door",
          # §63.47: the borrowed-buffer family — ownership, the block spelling, and the two getters.
          "borrowed-bytes-are-copied-so-freeing-the-buffer-is-safe",
          "no-copy-deallocator-runs-once-with-the-length",
          "getbytes-size-form-and-refusal",
          "getcstring-range-form-converts-the-range",
          # §63.48: the two filesystem doors (a real symlink, an unresolvable path) and the Finder compare.
          "symlink-resolution-follows-a-real-link",
          "unresolvable-path-resolves-to-itself",
          "localized-standard-compare-folds-case-and-reports-its-numeric-gap",
          "completepathintostring-completes-and-filters",
          "linguistic-tags-pair-agree",
          "linguistic-sentence-range-is-the-taggers-own",
)


class Case(BaseCase):
    title = "the Foundation's strings: tagged literals, owned strings, UTF-8"
    tier = "fast"
    # Its filesystem reads are read-only FIXTURES in the image; the host-clean list is a different
    # question (a HOST build has no /System), so it can share a guest like the rest.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_string")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-STRING-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-STRING "):
                self.note(line)

        done = "FOUNDATION-STRING DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-STRING DONE; output tail: "
                        + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-STRING %s ok$" % re.escape(c),
                                    out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS),
                           ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-STRING RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-STRING-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
