"""What a case is: a name, a tier, a boot, and a list of checks.

A case is one file under tests/cases/.  It subclasses BaseCase, implements
`run(ctx)`, and calls `self.check(name, ok, detail)` for each thing it wants
to prove.  `run` may boot a guest (ctx.boot), run a host command (ctx.host),
or both.

The contract - see tests/README.md for the reasoning:

  * wait for a marker the guest prints, never a fixed sleep;
  * derive geometry from the log, never assume a screen size;
  * assert the colours the configuration produced, not magic triples;
  * leave the machine dead: the Context kills every session it started.
"""

import os
import re
import subprocess

from . import paths
from .qemu import launch


class Skip(Exception):
    """This case cannot run here (a missing artifact, a missing tool).

    A skip is reported, not counted as a failure: a collaborator who has not
    built the image yet should get "run make rootagfs", not a red FAIL.
    """


class Check:
    def __init__(self, name, ok, detail):
        self.name, self.ok, self.detail = name, bool(ok), detail


class Context:
    """What a case is handed: artifacts, boots, and a way to run a command."""

    def __init__(self, case_name):
        self.case_name = case_name
        self.dir = os.path.join(paths.ARTIFACTS, case_name)
        os.makedirs(self.dir, exist_ok=True)
        self.sessions = []

    # --- booting ------------------------------------------------------
    def boot(self, name="guest", **kw):
        session = launch(self.dir, name=name, **kw)
        self.sessions.append(session)
        return session

    def boot_to_desktop(self, name="guest", secs=120, **kw):
        """Boot and wait for a working session.

        The marker matters: a boot banner line also contains the string "# ",
        which is why the old gates' prompt-based sync was unreliable.
        """
        session = self.boot(name=name, **kw)
        if not session.wait_for(r"KESTREL-READY", secs):
            session.stop()
        return session

    # --- artifacts ----------------------------------------------------
    def artifact(self, name):
        return os.path.join(self.dir, name)

    # --- prerequisites ------------------------------------------------
    def require_guest_file(self, name):
        """Skip unless `name` is inside the packed root image."""
        if not paths.guest_file_in_image(name):
            raise Skip("`%s` is not in %s - build it first (make rootagfs)"
                       % (name, paths.rel(paths.root_image())))

    def require_exists(self, path, hint=""):
        if not os.path.exists(path):
            raise Skip("`%s` is missing%s"
                       % (paths.rel(path), (" - " + hint) if hint else ""))

    # --- host commands ------------------------------------------------
    def host(self, argv, secs=900, cwd=None):
        """Run a host command.  Returns (returncode, output)."""
        try:
            proc = subprocess.run(argv, capture_output=True, text=True,
                                  timeout=secs, cwd=cwd or paths.ROOT)
        except subprocess.TimeoutExpired:
            return 124, "timed out after %ds: %s" % (secs, " ".join(argv))
        except OSError as exc:
            return 127, "cannot run %s: %s" % (" ".join(argv), exc)
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")

    def cleanup(self):
        for session in self.sessions:
            try:
                session.stop()
            except Exception:
                pass
        self.sessions = []


class BaseCase:
    """One test.  Subclass it in tests/cases/<name>.py.

    The class attribute `tier` decides when it runs: "fast" is what `make test`
    runs (a few minutes), "slow" needs `make test-all` or `make test-tier-slow`.
    `needs_boot` is False for a case that only runs host commands, so the
    runner can skip the QEMU/image prerequisite checks entirely.
    """

    title = ""
    tier = "fast"
    timeout = 300
    needs_boot = True

    def __init__(self, name):
        self.name = name
        self.checks = []
        self.notes = []

    # --- reporting ----------------------------------------------------
    def check(self, name, ok, detail=""):
        self.checks.append(Check(name, ok, detail))
        print(("PASS " if ok else "FAIL ") + self.name + "/" + name
              + (": " + detail if detail else ""))
        return bool(ok)

    def note(self, message):
        self.notes.append(message)
        print("  " + message)

    # --- what a case implements ---------------------------------------
    def run(self, ctx):
        raise NotImplementedError

    def passed(self):
        return bool(self.checks) and all(c.ok for c in self.checks)

    # --- small helpers cases keep re-implementing ---------------------
    @staticmethod
    def find(text, pattern, cast=str, default=None):
        """First capture group of `pattern` in `text`, cast, or `default`."""
        match = re.search(pattern, text)
        if not match:
            return default
        try:
            return cast(match.group(1))
        except (TypeError, ValueError):
            return default

    @staticmethod
    def find_all(text, pattern, cast=str):
        out = []
        for match in re.finditer(pattern, text):
            try:
                out.append(cast(match.group(1)))
            except (TypeError, ValueError):
                pass
        return out

    def luma_of(self, rgb):
        return (rgb[0] * 299 + rgb[1] * 587 + rgb[2] * 114) // 1000
