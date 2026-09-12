"""Boot the assembled OS, and make sure QEMU is dead afterwards.

`halt -f` reaches "Safe to Power Off" but QEMU does not always exit, so the
session owns a hard kill and the runner always calls it: a leaked guest holds
a write lock on .build/esp.img and the next case fails with "Failed to get
write lock", which reads like a harness bug rather than a leak.

The console is a pipe this process owns, so a case sends a guest command by
calling `session.serial("...")` instead of the shell heredocs the old
.build/*_run.sh scripts used.
"""

import atexit
import os
import re
import signal
import subprocess
import time

from . import paths
from .image import Shot
from .monitor import Monitor

# Guests this process started.  Session.stop() takes a session out of here, so
# what remains at exit is a leak: the harness always kills the process group,
# but a run that dies abruptly (SIGPIPE from a piped stdout, SIGKILL, a timeout)
# never reaches its teardown, and the survivor holds the image lock, which makes
# every later boot fail with "Failed to get write lock".
_LIVE = []


@atexit.register
def _kill_live_guests():
    for proc in list(_LIVE):
        try:
            _signal_group(proc, signal.SIGKILL)
        except Exception:
            pass


class Session:
    def __init__(self, proc, log_path, sock_path, work_dir):
        self.proc = proc
        self.log_path = log_path
        self.sock_path = sock_path
        self.work_dir = work_dir
        self._monitor = None
        self._shots = 0
        self._tags = 0

    # --- the guest's console ------------------------------------------
    def log_text(self):
        """Everything the guest has printed so far (CRs stripped)."""
        try:
            with open(self.log_path, "r", errors="replace") as fh:
                return fh.read().replace("\r", "")
        except OSError:
            return ""

    def wait_for(self, pattern, secs=60):
        """Wait for a regex to appear in the guest log.

        Cases wait for markers the guest actually prints; sleeping a fixed
        time is what makes a gate flaky on a slower machine.
        """
        rx = re.compile(pattern)
        deadline = time.time() + secs
        while time.time() < deadline:
            if rx.search(self.log_text()):
                return True
            if self.proc.poll() is not None:
                return bool(rx.search(self.log_text()))
            time.sleep(0.25)
        return False

    def count(self, pattern):
        return len(re.findall(pattern, self.log_text()))

    def tail(self, lines=6):
        """The last few lines the guest printed.

        Worth putting in a failed check's detail: "the desktop never came up"
        means something specific in the tail (a locked image, a panic).
        """
        text = [line for line in self.log_text().splitlines() if line.strip()]
        return " | ".join(text[-lines:])

    def serial(self, line):
        """Send one line to the guest's serial console."""
        self.write(line + "\r")

    def run(self, command, marker=None, secs=60):
        """Send a command and wait for a marker it prints.

        The marker must appear *after* the command was sent, which is what
        makes this safe to use per probe: a probe that prints `OK` lines has
        usually printed some before, and matching those would report success
        before the command ever ran.

        With no marker, completion is proven by running `printf` with a
        placeholder: what it *prints* (`FNX3-DONE`) differs from what the tty
        echoes back (`FNX%s-DONE`), so the echo cannot satisfy the search on
        its own.
        """
        before = len(self.log_text())
        self.write(command + "\r")
        if marker is None:
            self._tags += 1
            self.write("printf 'FNX%%s-DONE\\n' %d\r" % self._tags)
            marker = "FNX%d-DONE" % self._tags
        rx = re.compile(marker)
        deadline = time.time() + secs
        while time.time() < deadline:
            if rx.search(self.log_text()[before:]):
                return True
            if self.proc.poll() is not None:
                break
            time.sleep(0.25)
        return False

    def output_since(self, length):
        """The guest console output after a length taken from log_text()."""
        return self.log_text()[length:]

    def shell_ready(self, secs=90):
        """Wait until the serial console has a shell that answers.

        KESTREL-READY is not enough: init starts the desktop and the console
        shell at different times, and a command typed before the shell exists
        is consumed by the tty and lost.  Each attempt uses the tagged form, so
        the tty's echo of the attempt cannot be mistaken for its answer.
        """
        deadline = time.time() + secs
        while time.time() < deadline:
            if self.run(":", secs=10):
                return True
            time.sleep(1.0)
        return False

    def write(self, raw):
        data = raw.encode()
        try:
            self.proc.stdin.write(data)
            self.proc.stdin.flush()
        except (BrokenPipeError, ValueError, OSError):
            pass

    # --- the host's view of the screen --------------------------------
    def monitor(self):
        if self._monitor is None:
            self._monitor = Monitor(self.sock_path)
        return self._monitor

    def shot(self, name="shot"):
        """Screendump into the case's artifact directory and load it."""
        self._shots += 1
        path = os.path.join(self.work_dir, "%s-%d.ppm" % (name, self._shots))
        self.monitor().screendump(path)
        return Shot.from_file(path)

    # --- teardown -----------------------------------------------------
    def halt(self, secs=30):
        """Ask the guest to power off; True when it says it is safe."""
        self.serial("halt -f")
        return self.wait_for(r"Safe to Power Off", secs)

    def alive(self):
        return self.proc.poll() is None

    def stop(self):
        if self._monitor is not None:
            self._monitor.close()
            self._monitor = None
        try:
            if self.proc.stdin:
                self.proc.stdin.close()
        except (OSError, ValueError):
            pass
        if self.proc.poll() is None:
            _signal_group(self.proc, signal.SIGTERM)
            try:
                self.proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                _signal_group(self.proc, signal.SIGKILL)
                try:
                    self.proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    pass
        try:
            _LIVE.remove(self.proc)
        except ValueError:
            pass
        try:
            os.unlink(self.sock_path)
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop()
        return False


def _signal_group(proc, sig):
    try:
        os.killpg(os.getpgid(proc.pid), sig)
    except OSError:
        try:
            proc.send_signal(sig)
        except OSError:
            pass


def launch(work_dir, name="guest", mem=None, audiodev=None, machine=None,
           extra=(), attach_sound=True):
    """Start a guest and return its Session (already running, not yet booted).

    The boot flags mirror the `make run-uefi` harness so a case exercises the
    same configuration a user does, with two differences: no display, and an
    audio backend that is silent unless FNX_TEST_AUDIODEV asks otherwise.
    """
    mem = mem or paths.default_mem()
    audiodev = audiodev or paths.default_audiodev()
    machine = machine or paths.default_machine()

    # A foreign QEMU holds a write lock on the images, and the failure it
    # produces ("Failed to get write lock") reads like a harness bug.  The
    # runner checks this up front; this catches a leaked guest from an earlier
    # interrupted run too, at the moment the next boot would fail.
    others = paths.other_qemus()
    if others and not os.environ.get("FNX_TEST_ALLOW_OTHER_QEMU"):
        raise paths.PrereqError(
            "another QEMU is already running (pid %s) and holds the image lock\n"
            "  make qemu-kill      (or FNX_TEST_ALLOW_OTHER_QEMU=1 to try anyway)"
            % ", ".join(str(p) for p in others))

    os.makedirs(work_dir, exist_ok=True)
    log_path = os.path.join(work_dir, "%s.log" % name)
    sock_path = os.path.join(work_dir, "%s.sock" % name)
    for p in (log_path, sock_path):
        try:
            os.unlink(p)
        except OSError:
            pass

    argv = [
        paths.QEMU_SH,
        "-nographic", "-no-reboot", "-display", "none",
        "-monitor", "unix:%s,server=on,wait=off" % sock_path,
        "-serial", "stdio",
        "-m", mem,
        "-machine", machine,
        "-drive", "file=%s,format=raw,if=ide,index=0" % paths.ESP_IMG,
        "-drive", "file=%s,format=raw,if=none,id=disk" % paths.root_image(),
        "-device", "ich9-ahci,id=ahci",
        "-device", "ide-hd,drive=disk,bus=ahci.0",
    ]
    if attach_sound:
        argv += ["-audiodev", "%s,id=snd" % audiodev,
                 "-device", "intel-hda",
                 "-device", "hda-output,audiodev=snd"]
    argv += list(extra)

    env = dict(os.environ)
    env.setdefault("FNX_QEMU_BIOS", "ovmf")

    log_fd = os.open(log_path, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644)
    try:
        proc = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=log_fd,
                                stderr=log_fd, env=env, start_new_session=True)
    finally:
        os.close(log_fd)
    _LIVE.append(proc)
    return Session(proc, log_path, sock_path, work_dir)
