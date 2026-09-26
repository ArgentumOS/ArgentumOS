/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSBundle.h — the vocabulary around a bundle whose EXECUTABLE architecture a caller can ask about.
 *
 * THE CLASS IS NOT HERE, AND THE REASON IS SPECIFIC RATHER THAN GENERAL: this system's bundles are AGFS
 * directories with a libconfig manifest (the toolkit and the window manager read them that way), so there is no
 * NSBundle to hang -executableArchitectures off. What ships is the vocabulary a conforming caller compiles
 * against: the five architecture codes and the two notification names.
 *
 * AND THE ARCHITECTURE CODES CARRY A WARNING WORTH READING: Apple's five values ARE the Mach-O cputype numbers,
 * because a program compares them against a Mach-O header. THIS SYSTEM'S EXECUTABLES ARE ELF, so a caller that
 * compares one of these against a header it read itself is comparing against nothing - the names are here so the
 * calls compile, the values are ours (§11.6.1 D2), and the comparison they were designed for has no counterpart
 * here. That is stated rather than left to be discovered.
 */

#ifndef FOUNDATION_NSBUNDLE_H
#define FOUNDATION_NSBUNDLE_H

#import <Foundation/NSObject.h>
#import <Foundation/NSNotification.h>

NS_ASSUME_NONNULL_BEGIN

/* Five codes, distinct from each other; the 64-bit marker Apple puts in its high word is NOT reproduced, because
 * nothing here reads the headers these would be compared against. */
typedef enum {
	NSBundleExecutableArchitectureI386 = 1,
	NSBundleExecutableArchitecturePPC = 2,
	NSBundleExecutableArchitectureX86_64 = 3,
	NSBundleExecutableArchitecturePPC64 = 4,
	NSBundleExecutableArchitectureARM64 = 5
} NSBundleExecutableArchitecture;

/* The notification a bundle posts when its code loads, and the userInfo key it carries (value == name, this
 * library's convention for these constants - see NSNotification.h). NOTHING IN THIS SYSTEM POSTS IT: there is no
 * NSBundle to load code, so an observer waits forever, which is why the names ship with that said. */
extern NSNotificationName const NSBundleDidLoadNotification;
extern NSString *const NSLoadedClasses;

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSBUNDLE_H */
