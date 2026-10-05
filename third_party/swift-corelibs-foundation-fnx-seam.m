/*
 * FNX's seam for the vendored CoreFoundation.
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * WHY THIS FILE EXISTS, AND WHY IT IS OURS RATHER THAN A FIFTH PATCH. swift-corelibs-foundation's
 * CoreFoundation is built by upstream WITH the Swift half of the repository linked in, and two symbols
 * this tree needs live over there. Rather than patch upstream a third and fourth time, the definitions
 * live here, in one file that says what it is for and that no upstream file has to be edited to add to.
 * tools/corefoundation-build.sh compiles it into libcorefoundation beside the 86 upstream objects.
 *
 * WHEN THIS FILE CAN GO AWAY: when a symbol below stops being undefined, the link fails with a
 * DUPLICATE symbol and names it — so this file cannot silently rot into redundancy, and it cannot
 * silently stop being needed either. That is deliberate: a seam that fails loudly in both directions is
 * the only kind worth having.
 *
 * file: 2026-10, the M1 work (docs/design/foundation-cf-core-plan.md), after measuring the whole
 * undefined set down to these two by building 86/86 objects and linking.
 */
#include "CFInternal.h"		/* _CFThreadRef, Boolean, CF_EXPORT, CF_CROSS_PLATFORM_EXPORT */
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*
 * _CFGetCurrentDirectory  —  upstream implements this IN SWIFT, so there is no C definition to reach:
 * Sources/Foundation/FileManager.swift defines it with @_cdecl("_CFGetCurrentDirectory"), commented
 * "Used in CFURL.c (use FileManager API so that platform specifics like long path prefix strip can be
 * handled in a single place)". CFURL.c calls it, so the C-only build must supply it.
 *
 * THE CONTRACT IS TAKEN FROM THAT SWIFT DEFINITION, not invented: return false for a NULL buffer or a
 * size of zero or less; the required size INCLUDES the terminating NUL; and return false if the path
 * does not fit. FileManager.currentDirectoryPath is getcwd(3) for this system, which has no long-path
 * prefixes to strip — the "single place" the comment is about is this function either way.
 */
CF_EXPORT Boolean _CFGetCurrentDirectory(char *path, int maxlen) {
	char *cwd;
	size_t required;
	Boolean ok = false;

	if (path == NULL || maxlen <= 0) {
		return false;
	}
	cwd = getcwd(NULL, 0);		/* the size is the library's to choose */
	if (cwd == NULL) {
		return false;
	}
	required = strlen(cwd) + 1;	/* + the NUL, as the Swift version counts it */
	if (required <= (size_t)maxlen) {
		memcpy(path, cwd, required);
		ok = true;
	}
	free(cwd);
	return ok;
}

/*
 * _CFThreadSetName  —  upstream DOES define this, at CFPlatform.c:1791, and the definition is not
 * negotiable with a flag: MEASURED, that object neither defines nor references the symbol (`nm
 * .build/cfobj/CFPlatform.o | grep _CFThreadSetName` is empty) while CFStream.c calls it (CFStream.c:1704,
 * "com.apple.CFStream.LegacyThread"), which is exactly the undefined reference the link reported.
 *
 * SO THIS IS NOT A COPY OF UPSTREAM'S BODY, and it must not be: upstream's TARGET_OS_MAC branch calls
 * Darwin's ONE-argument pthread_setname_np(name). musl's takes TWO arguments — (pthread_t, const char *)
 * — so the Darwin branch would not compile here anyway. Passing the thread through is what musl's form is
 * for, and returning EINVAL for a thread it cannot name keeps upstream's own shape (its MAC branch
 * returns EINVAL for a thread that is not the caller).
 */
CF_CROSS_PLATFORM_EXPORT int _CFThreadSetName(_CFThreadRef thread, const char *name) {
	if (thread == ((_CFThreadRef)0)) {
		return EINVAL;
	}
	return pthread_setname_np((pthread_t)thread, name);
}
