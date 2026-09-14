# Argentum OS

It began as Fiwix, a small (< 50KLoc) Unix-like kernel for i386 systems. I forked it, renamed the kernel to FNX, and ported it to run exclusively on AMD64/UEFI systems. I built it up with drivers for everything useful QEMU can emulate. 

That went so well, I decided to use it as a chance to address every gripe and grievance I've ever had with an OS. I would take features I liked from other OSes and implement them locally from scratch, my way. I would create my own design language, my own applications, my own command line utilities, I would own the entire stack, from soup to nuts.

Argentum OS is the result. It's the combination of every good idea I have seen or thought of for an OS. It has a macOS like API and global menubar, BeOS-like filesystem tricks, Linux-like developer tooling, and many of my own ideas, like `libconfig`, a custom library which implements *all* configuration data for *everything* in the system, in plain text files with a common format.

It is a hobby OS, which means I'm working on it because I want to; it also means it's probably full of little bugs and annoyances, and is nowhere near ready for use in anger on real hardware. If you want to play with it, by all means, but I recommend doing it in QEMU where it's safe.

Warning: This project makes use of agentic coding. If that is a deal-breaker for you, then stay well clear of it.

All original code is © 2026 Kyle J Cardoza, and is free software released under the MIT license. See LICENSE. The FNX kernel is derived from [Fiwix](https://www.fiwix.org), the 32-bit kernel created by Jordi Sanfeliu, and is free software under the MIT License. 

Credits: <https://www.fiwix.org>.
