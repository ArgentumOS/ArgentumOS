# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
"""Run a probe on the HOST instead of booting a guest (mk/60-host.mk).

A CASE DOES NOT CHANGE.  A case's run(ctx) boots, sends a probe path to a shell,
and then asserts on the OUTPUT TEXT and the exit status - so a host run only has
to produce those two things, and every assertion the case already makes applies
unchanged.  No case file knows this module exists.

WHAT THIS IS NOT.  It never touches the kernel, Xfb, /dev, the FSH or the guest
filesystem, and it links the host's glibc and ICU rather than the guest's musl.
A host run says "the library does this"; it cannot say "the OS does this".  The
guest gates stay the verification of record.

ONLY CASES IN HOST_CLEAN RUN, and each is listed only once it has been SHOWN to
pass here.  A case whose probe reads the guest's filesystem or an FSH path is
deliberately absent: it would fail for a reason that is not a bug.
"""

import os
import re
import subprocess

from . import paths

# The path the cases send to the shell, and where those probes really are for a
# host run.  A prefix rewrite, so a case that passes arguments still works.
GUEST_PROBE_DIR = "/System/Shared/tests/"
HOST_BIN_DIR = os.path.join(paths.ROOT, ".build", "host", "bin")

# Add a case only after building it (make host-foundation HOST_PROBES=...) and
# watching it pass here.  The guest run of foundation_core answers 50 checks; the
# host currently answers 47 - arc-pool (a RUNTIME difference, not the library's)
# and the two undo checks - which is why the fragment's header records that this
# is an iteration loop and not yet a substitute.
HOST_CLEAN = {
    "foundation_codecs",
    "foundation_decimal",
    "foundation_coder",
    "foundation_core",
    "foundation_dateformatter",
    "foundation_error",
    "foundation_expression",
    "foundation_kvc",
    "foundation_kvo",
    "foundation_numberformatter",
    "foundation_orderedset",
    "foundation_predicate",
    "foundation_processinfo",
    "foundation_progress",
    "foundation_regex",
    "foundation_runloop",
    "foundation_set",
    "foundation_sort",
    "foundation_thread",
    "foundation_urlcomponents",
}


class HostSession:
    """The part of a guest Session a case actually uses, over the host."""

    def __init__(self, case_name):
        self.case_name = case_name
        self._text = ""

    # --- the guest-session surface the cases use ----------------------
    def shell_ready(self, secs=90):
        # There is nothing to wait for: require_guest_file has already proved the
        # binary is there, and a process either runs or reports why.
        return True

    def run(self, command, marker=None, secs=60):
        argv = ["/bin/sh", "-c", self.rewrite(command)]
        try:
            proc = subprocess.run(argv, capture_output=True, text=True,
                                  timeout=secs, cwd=paths.ROOT)
        except subprocess.TimeoutExpired:
            self._text += "timed out after %ds: %s\n" % (secs, command)
            return 124
        except OSError as exc:
            self._text += "cannot run %s: %s\n" % (command, exc)
            return 127
        self._text += proc.stdout or ""
        self._text += proc.stderr or ""
        return proc.returncode

    def output_since(self, length):
        return self._text[length:]

    def log_text(self):
        return self._text

    def tail(self, lines=6):
        return "\n".join(self._text.splitlines()[-lines:])

    def count(self, pattern):
        return len(re.findall(pattern, self._text))

    def wait_for(self, pattern, secs=60, poll=0.25):
        return re.search(pattern, self._text) is not None

    # --- there is no machine to stop ----------------------------------
    def stop(self):
        pass

    def alive(self):
        return False

    def halt(self, secs=30):
        pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        return False

    # --- the one thing that differs from a guest ----------------------
    @staticmethod
    def rewrite(command):
        """Point a guest probe path at its host-built binary."""
        return command.replace(GUEST_PROBE_DIR, HOST_BIN_DIR + "/")


def binary_path(probe):
    return os.path.join(HOST_BIN_DIR, probe)
