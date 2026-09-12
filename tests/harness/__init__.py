"""Shared machinery for the FNX test harness.

A case (tests/cases/*.py) boots the assembled OS under QEMU - or runs a host
command - and then asserts on what the guest logs, draws and answers.  See
tests/README.md for the contract, and mk/50-tests.mk for the entry points:

    make test           fast tier
    make test-all       fast + slow
    make test T=<case>  one case (globs allowed)
    make test-list      what exists, with tiers
"""

from .case import BaseCase, Check, Context, Skip
from .image import Shot, hex_rgb, luma_of, parse_ppm
from .monitor import Monitor
from .paths import (ARTIFACTS, ROOT, PrereqError, build, check_prereqs,
                    default_audiodev, default_mem, guest_file_in_image,
                    image_freshness, rel, repo, root_image)
from .qemu import Session, launch

__all__ = [
    "ARTIFACTS", "ROOT", "BaseCase", "Check", "Context", "Monitor",
    "PrereqError", "Session", "Shot", "Skip", "build", "check_prereqs",
    "default_audiodev", "default_mem", "guest_file_in_image",
    "hex_rgb", "image_freshness", "launch", "luma_of", "parse_ppm", "rel",
    "repo", "root_image",
]
