"""Boot the assembled OS, and make sure QEMU is dead afterwards.

`halt -f` reaches "Safe to Power Off" but QEMU does not always exit, so the
session owns a hard kill and the runner always calls it: a leaked guest holds
a write lock on .build/esp.img and the next case fails with "Failed to get
write lock", which reads like a harness bug rather than a leak.

The console is a pipe this process owns, so a case sends a guest command by
calling `session.serial("...")` instead of the shell heredocs the old
.build/*_run.sh scripts used.
"""

import os
import re
import signal
import subprocess
import time

from . import paths
from .image import Shot
from .monitor import Monitor


class Session:
    def __init__(self, proc, log_path, sock_path, work_dir):
        self.proc = proc
        self.log_path = log_path
        self.sock_path = sock_path
        self.work_dir = work_dir
        self._monitor = None
        self._shots = 0

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

    def serial(self, line):
        """Send one line to the guest's serial console."""
        self.write(line + "\r")

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
    return Session(proc, log_path, sock_path, work_dir)
