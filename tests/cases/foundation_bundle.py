# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""NSBundle — the definition of a bundle: a directory containing an Info.plist, whose manifest is a
real property list read through NSPropertyListSerialization.

The probe is `/System/Shared/tests/foundation_bundle`, ONE unit importing only
`<Foundation/Foundation.h>` (which is how NSBundle is proved to have reached the umbrella). IT BUILDS ITS OWN
FIXTURES - a Contents/ bundle, a flat bundle and a directory with no manifest - so no test-only manifest
format exists in the image.

  * `bundle-rejects-a-directory-with-no-manifest` — a directory without Info.plist answers nil;
  * `bundle-accepts-both-layouts`                — Contents/ and flat bundles both open;
  * `bundle-reads-its-plist-manifest`            — Apple's keys come back from a real plist;
  * `bundle-finds-its-executable`                — Contents/MacOS vs the bundle root;
  * `bundle-resource-lookup`                     — a hit, a miss answered nil, and resourcePath;
  * `bundle-localizations-come-from-lproj-directories` — en and fr from *.lproj;
  * `main-bundle-exists-even-for-a-plain-tool`   — the executable's directory, per Apple;
  * `bundle-by-identifier-searches-the-opened-ones` — the registry, and nil for an unknown one;
  * `bundle-urls-follow-their-paths`                — bundleURL/resourceURL/executableURL mirror their paths;
  * `resource-urls-mirror-the-path-lookups`         — URLForResource and URLsForResources over the paths;
  * `resource-url-in-bundle-with-url`               — the class door over a bundle URL, nil for a non-bundle;
  * `bundle-from-url-and-init-with-url`             — bundleWithURL: and initWithURL:;
  * `standard-bundle-directories-are-existence-gated` — PlugIns/Frameworks present, SharedSupport nil;
  * `auxiliary-executable-path-and-url`             — an auxiliary executable found, a miss nil, URL mirrors;
  * `class-resource-door-searches-the-main-bundle`  — the class resource doors over the main bundle;
  * `localization-aware-resource-lookup`            — a .lproj hit, a fallback, and the plural door;
  * `localized-string-over-explicit-localizations`  — the explicit-localization string door, hit and fallback;
  * `preferred-localizations-match-preferences`     — a match, and the unchanged no-match answer;
  * `development-localization-and-localized-info`   — CFBundleDevelopmentRegion and localizedInfoDictionary;
  * `class-lookup-and-bundle-for-class`             — classNamed: and bundleForClass:;
  * `loading-doors-report-their-error`              — loadAndReturnError:/preflightAndReturnError: and NSError.
"""

import re

from harness import BaseCase

PROBE = "/System/Shared/tests/foundation_bundle"
CHECKS = ("bundle-rejects-a-directory-with-no-manifest", "bundle-accepts-both-layouts",
          "bundle-reads-its-plist-manifest", "bundle-finds-its-executable", "bundle-resource-lookup",
          "bundle-localizations-come-from-lproj-directories", "main-bundle-exists-even-for-a-plain-tool",
          "bundle-by-identifier-searches-the-opened-ones", "bundle-urls-follow-their-paths",
          "resource-urls-mirror-the-path-lookups", "resource-url-in-bundle-with-url",
          "bundle-from-url-and-init-with-url", "standard-bundle-directories-are-existence-gated",
          "auxiliary-executable-path-and-url", "class-resource-door-searches-the-main-bundle",
          "localization-aware-resource-lookup", "localized-string-over-explicit-localizations",
          "preferred-localizations-match-preferences", "development-localization-and-localized-info",
          "bundle-load-refuses-a-payload-that-is-not-code", "bundle-load-brings-in-real-code",
          "bundle-principal-class-comes-from-the-manifest", "bundle-loaded-class-is-usable",
          "class-lookup-and-bundle-for-class", "bundle-load-posts-its-notification-with-the-classes",
          "loading-doors-report-their-error", "bundle-unload-answers-no-when-nothing-was-loaded")


class Case(BaseCase):
    title = "NSBundle: what a bundle is, and what it can find inside itself"
    tier = "fast"
    # Measured to answer the SAME with a reused guest: it only runs a probe and reads its output.
    shared_session = True
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("foundation_progress")
        session = ctx.boot()
        ready = session.shell_ready(150)
        self.check("shell-ready", ready,
                   "the serial console has a shell" if ready
                   else "no shell; guest tail: " + session.tail())
        if not ready:
            return

        mark = len(session.log_text())
        session.run("%s; echo FOUNDATION-BUNDLE-STATUS=$?" % PROBE)
        out = session.output_since(mark)
        for line in out.splitlines():
            if line.startswith("FOUNDATION-BUNDLE "):
                self.note(line)

        done = "FOUNDATION-BUNDLE DONE" in out
        self.check("probe-ran", done,
                   "the probe reached its end marker" if done
                   else "no FOUNDATION-BUNDLE DONE; output tail: " + out.strip()[-400:])
        if not done:
            return

        missing = [c for c in CHECKS
                   if not re.search(r"^FOUNDATION-BUNDLE %s ok$" % re.escape(c), out, re.M)]
        self.check("every-check-passed", not missing,
                   ("all %d checks reported ok" % len(CHECKS)) if not missing
                   else "%d of %d ok; missing: %s"
                        % (len(CHECKS) - len(missing), len(CHECKS), ", ".join(missing)))

        fails = [l for l in out.splitlines()
                 if l.endswith("FAIL") or " FAIL " in l]
        self.check("no-fail-lines", not fails,
                   "no check reported FAIL" if not fails else "; ".join(fails))

        tally = re.search(r"FOUNDATION-BUNDLE RESULT ok=(\d+) fail=(\d+)", out)
        self.check("result-line",
                   bool(tally) and tally.group(1) == str(len(CHECKS))
                   and tally.group(2) == "0",
                   "the probe's own tally: %s"
                   % (tally.group(0) if tally else "missing"))

        self.check("exit-status", "FOUNDATION-BUNDLE-STATUS=0" in out,
                   "the probe exited 0 (a non-zero status means a failed check)")
