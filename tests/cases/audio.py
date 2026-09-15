"""The sound card is attached, the guest drives it, and the host backend is quiet.

What it proves: QEMU's intel-hda controller is present, the guest's HDA driver
binds it and exposes the OSS device, a program can write samples to it, and the
audio backend reports no errors.

This is the ears-free form of "audio works": the run is silent by default
(FNX_TEST_AUDIODEV=none), so it is safe on a headless box or in CI.  The same
run becomes audible with FNX_TEST_AUDIODEV=pipewire.
"""

import re
import time

from harness import BaseCase

CARD = r"((?:intel-hda|hda|ac97|es1370|sb16): [0-9a-f]{4}:[0-9a-f]{4}.*)"
TONE = r"TONE-OK (\d+) samples"
BACKEND_ERR = r"audio: |Could not|Assertion"


class Case(BaseCase):
    title = "the guest plays a tone through its OSS device, with no backend errors"
    tier = "fast"
    timeout = 300

    def run(self, ctx):
        ctx.require_guest_file("/System/Tools/tone")
        session = ctx.boot()
        ready = session.shell_ready(150)
        log = session.log_text()

        card = self.find(log, CARD)
        self.check("card-probed", bool(card),
                   card or "no audio driver claimed a device")
        self.check("oss-device", "dsp" in (card or ""),
                   "the driver exposed an OSS device to the guest")

        # A bare CR gets the serial console onto a clean line (the console the
        # desktop session leaves free).  The settle is for the tty's own line
        # discipline, not for the guest to become ready - that was the
        # KESTREL-READY wait above.
        session.serial("")
        time.sleep(1.0)
        session.serial("/System/Tools/tone")
        played = session.wait_for(TONE, 60)

        samples = self.find(session.log_text(), TONE, int, 0)
        self.check("tone-played", played and samples > 0,
                   "%d samples were written to the device" % samples)

        errors = len(re.findall(BACKEND_ERR, session.log_text()))
        self.check("no-backend-errors", errors == 0,
                   "%d host audio backend error line(s)" % errors)
