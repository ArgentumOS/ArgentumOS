"""Where the harness's inputs live, and whether it can run at all.

Nothing under tests/ hardcodes a repository path, a screen size or a RAM size:
a collaborator runs this from a fresh clone, and their checkout is not at
somebody else's path.  Everything here is derived from this file's location or
from FNX_TEST_* environment variables.

Environment knobs
-----------------
FNX_TEST_MEM        guest RAM for cases that do not ask for a size (default 256M)
FNX_TEST_ROOTIMG    a different root image (default .build/rootagfs.img)
FNX_TEST_AUDIODEV   QEMU host audio backend (default "none" - silent but real)
FNX_TEST_MACHINE    the QEMU machine type (default "pc,usb=off")
FNX_TEST_TIER       default tier for the runner (fast|slow|all)
FNX_TEST_ALLOW_OTHER_QEMU  1 to boot while another QEMU is running
"""

import os
import subprocess

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
ARTIFACTS = os.path.join(ROOT, ".build", "tests")

# What a boot needs.
ESP_IMG = os.path.join(ROOT, ".build", "esp.img")
OVMF_FD = os.path.join(ROOT, ".build", "ovmf", "OVMF.fd")
QEMU_SH = os.path.join(ROOT, "tools", "qemu.sh")

# Staged userland (the FSH linter reads it, not the source tree).
STAGED_ROOT = os.path.join(ROOT, ".build", "rootfs64")

# Directories whose newest file decides whether the packed image is stale.
SOURCE_DIRS = ("kernel", "mm", "fs", "drivers", "net", "lib", "include",
               "userland", "mk", "tools")
SOURCE_SUFFIXES = (".c", ".h", ".cpp", ".hpp", ".S", ".s", ".mk", ".py",
                   ".sh", ".ld", ".conf")


class PrereqError(RuntimeError):
    """The harness cannot run here; the message names the fix."""


def repo(*parts):
    return os.path.join(ROOT, *parts)


def build(*parts):
    return os.path.join(ROOT, ".build", *parts)


def rel(path):
    try:
        return os.path.relpath(path, ROOT)
    except ValueError:
        return path


def root_image():
    """The root image every case boots (FNX_TEST_ROOTIMG picks a variant)."""
    return os.environ.get("FNX_TEST_ROOTIMG") or build("rootagfs.img")


def default_mem():
    return os.environ.get("FNX_TEST_MEM", "256M")


def default_audiodev():
    """CI-safe by default.

    The sound card is always attached (it is the shipped configuration), but
    its output is discarded, so a run neither needs a host audio server nor
    makes noise.  FNX_TEST_AUDIODEV=pipewire (or pa/alsa) makes it audible.
    """
    return os.environ.get("FNX_TEST_AUDIODEV", "none")


def default_machine():
    return os.environ.get("FNX_TEST_MACHINE", "pc,usb=off")


def qemu_version():
    """The version line of the QEMU `tools/qemu.sh` selects, or raise."""
    if not os.path.exists(QEMU_SH):
        raise PrereqError("tools/qemu.sh is missing from this checkout")
    try:
        out = subprocess.run([QEMU_SH, "--version"], capture_output=True,
                             text=True, timeout=120)
    except (OSError, subprocess.SubprocessError) as exc:
        raise PrereqError("cannot run tools/qemu.sh: %s" % exc)
    if out.returncode != 0:
        detail = (out.stderr or out.stdout or "").strip().splitlines()
        raise PrereqError(
            "no usable QEMU (%s)\n"
            "  install one (Debian: apt install qemu-system-x86), or set\n"
            "  FNX_QEMU_TOOLS to a tools prefix that has a qemu-system-x86_64"
            % (detail[0] if detail else "no output"))
    return (out.stdout or "qemu (version unknown)").strip().splitlines()[0]


def other_qemus():
    """PIDs of QEMU processes that are not ours.

    This reads /proc/<pid>/comm (the kernel's process name) rather than
    matching command lines, because a `pgrep -f qemu-system` also matches the
    shell that runs the search - its own command line contains the pattern -
    which is how a harness ends up refusing to run itself.
    """
    found = []
    for entry in os.listdir("/proc"):
        if not entry.isdigit():
            continue
        try:
            with open("/proc/%s/comm" % entry) as fh:
                name = fh.read().strip()
        except OSError:
            continue
        if name.startswith("qemu-system"):
            found.append(int(entry))
    return found


def check_prereqs(need_image=True, need_qemu=True):
    """Raise PrereqError unless the selected cases can run. Returns details."""
    version = qemu_version() if need_qemu else "not needed"
    if need_qemu and other_qemus() and not os.environ.get("FNX_TEST_ALLOW_OTHER_QEMU"):
        raise PrereqError(
            "another QEMU is already running, and it holds the image lock:\n"
            "  make qemu-kill      (or FNX_TEST_ALLOW_OTHER_QEMU=1 to try anyway)")
    missing = []
    if need_qemu and not os.path.exists(ESP_IMG):
        missing.append("  .build/esp.img       -> ./tools/mkesp.sh")
    if need_image and not os.path.exists(root_image()):
        missing.append("  %s -> make rootagfs" % rel(root_image()))
    if need_qemu and not os.path.exists(OVMF_FD):
        missing.append("  .build/ovmf/OVMF.fd  -> make ovmf")
    if missing:
        raise PrereqError("missing harness inputs:\n" + "\n".join(missing))
    return version


def guest_file_in_image(name):
    """Is `name` present in the packed root image?

    AGFS stores directory entries as plain names, not paths, so this searches
    for the basename: pass "/System/Tools/tone" or "tone", either works.  The
    answer to the question the cases ask ("was my probe built into this
    image?") is exact enough, and False when the image itself is absent.
    """
    path = root_image()
    if not os.path.exists(path):
        return False
    try:
        rc = subprocess.run(["grep", "-aqF", os.path.basename(name.rstrip("/")),
                             path], timeout=180).returncode
    except (OSError, subprocess.SubprocessError):
        return False
    return rc == 0


def newest_source():
    """(mtime, path) of the newest file the packed images are built from."""
    newest, newest_path = 0.0, ""
    for d in SOURCE_DIRS:
        base = os.path.join(ROOT, d)
        if not os.path.isdir(base):
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = [x for x in dirnames
                           if x not in (".git", "build", "node_modules",
                                        "__pycache__")]
            for fn in filenames:
                if not fn.endswith(SOURCE_SUFFIXES):
                    continue
                p = os.path.join(dirpath, fn)
                try:
                    m = os.path.getmtime(p)
                except OSError:
                    continue
                if m > newest:
                    newest, newest_path = m, p
    return newest, newest_path


def image_freshness():
    """(fresh, detail) for the image the cases will boot.

    `detail` names the newest source file, so a stale result says what makes it
    stale instead of just "FAIL".
    """
    path = root_image()
    if not os.path.exists(path):
        return True, "no image yet"
    _, newest_path = newest_source()
    if not newest_path:
        return True, "no sources found"
    try:
        if os.path.getmtime(path) >= os.path.getmtime(newest_path):
            return True, rel(newest_path)
    except OSError:
        return True, rel(newest_path)
    return False, rel(newest_path)
