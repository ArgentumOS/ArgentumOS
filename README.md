# Argentum

Argentum is a **desktop-first operating system**
for AMD64/UEFI systems. It is written as a hobby, one layer at a time, and
every layer is its own design — kernel, filesystem hierarchy and
filesystem, config format, GUI stack, application model. Nothing is
adopted for convenience; just enough POSIX surface is kept to run
software that is *mindfully ported*.

This repository builds all of it — one tree, one compiler (clang);
`make run-uefi` boots the OS under QEMU.

## Goals

Argentum asks how far a small, comprehensible system can go when
every layer is chosen rather than inherited — and what a personal
computer looks like when it is designed for its own sake, with no
legacy to carry. Concretely:

- **Own every layer.** One coherent story from the bootloader to the
  desktop, instead of a pile of adopted conventions held together by
  compatibility.
- **One of each.** One config format (`.conf`), one permissions model
  (POSIX ACLs), one encoding (UTF-8), one locale, one compiler — each
  chosen once and used everywhere.
- **Small and honest.** No garbage collector, no hidden machinery; a
  system one person can hold, with decisions — and failures — written
  down as it goes.

## Where it stands

Today the OS boots from UEFI on an AGFS root into a graphical session:
the Xfb display server on the framebuffer, a demo desktop, and a serial
console alongside.

The rest of the desktop is mid-development. The Argentum UIKit has its
view tree, its first controls and its theme system, exercised by a live
widget zoo; a window manager (Kestrel) manages windows and drags them.
Input is real hardware rather than emulation of it: a USB HID keyboard
and mouse are decoded by their own drivers into native input devices
(`/System/Devices/mouse`, `/System/Devices/kbd`), so the pointer and keys
a session sees are exactly what those devices report.

Argentum is a hobby OS project: it may have serious bugs and broken features. **Use at your own risk.**

## More

- `docs/README.md` — everything: design plans, evaluations, references
- `docs/reference/building.md` — requirements, build steps, QEMU harness
- `docs/reference/os-profile.md` — the full profile and philosophy

Argentum is derived from [Fiwix](https://www.fiwix.org), the 32-bit
kernel created by Jordi Sanfeliu, and is free software under the MIT
License — see LICENSE. Credits: <https://www.fiwix.org>.
