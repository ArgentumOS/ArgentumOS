# Foundation — the new library's build, in its own fragment from day one.
#
# Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
# SPDX-License-Identifier: MIT
#
# WHY A NEW FRAGMENT RATHER THAN MORE LINES IN mk/20-userland.mk. The old Foundation's rule lived there and
# grew one line per ledger entry over hundreds of commits until locating a target in that file stopped being
# reliable — twice in one session, two different instruments gave two different answers about which target
# owned the recipes around mk/20-userland.mk's probe block, and both answers were wrong. A build that cannot
# be read is a build that cannot be fixed, so the new library starts in a file small enough to read whole.
#
# THE ONE FLAG WORTH A COMMENT IS -fconstant-cfstrings. NSObject.m's description doors build a CFString
# with CFSTR, and this project has already paid for that trap once: without the flag, CFSTR references the
# CF constant string class, which in swift-corelibs-foundation is SWIFT's, and the link asks for
# $s10Foundation19_NSCFConstantStringCN. Every file in this library that spells CFSTR needs it.

FOUNDATION_SRC     = userland/Foundation
FOUNDATION_MSRCS   = $(notdir $(wildcard $(FOUNDATION_SRC)/*.m))
FOUNDATION_OBJS    = $(addprefix .build/foundation-,$(FOUNDATION_MSRCS:.m=.o))
FOUNDATION_LIB     = $(FNXLIB)/libfoundation.so

# -Iuserland so that `#import <Foundation/...>` resolves, which is the spelling every consumer will use.
# -I.build/cf-shim and $(COREFOUNDATION_SRC)/include for CF's headers, as the CF probes already do.
# -DDEPLOYMENT_RUNTIME_SWIFT=0 IS NOT OPTIONAL AND WAS THE WHOLE CFSTR SAGA. CoreFoundation's headers read
# that macro to choose between their Objective-C and their Swift forms -- the Swift arm's CFSTR names a
# Swift class and emits a five-word struct, the other uses clang's built-in and emits Apple's four-word
# __CFConstantString. The CF package passes it IN ITS OWN BUILD SCRIPT; this build never did, so every file
# here was compiled against Swift-mode headers, and every CFSTR asked the linker for
# $s10Foundation19_NSCFConstantStringCN. One flag, one flag list, one very long search.
FOUNDATION_CFLAGS  = -DDEPLOYMENT_RUNTIME_SWIFT=0 -fPIC -fblocks -fconstant-cfstrings -Iuserland -I.build/cf-shim \
                     -I$(COREFOUNDATION_SRC)/include -I$(LIBDISPATCH_PREFIX)/include

# THE LINKER'S ALIAS, AND IT LANDS WHERE .set DID NOT. Every constant-string struct clang emits carries
# &__CFConstantStringClassReference as its isa, and a message send reads that FIELD as a Class -- so the
# symbol's ADDRESS must BE the class. CoreFoundation defines it (now WEAK, so a strong definition wins), and
# Foundation cannot alias it from C because .set only emits a symbol the translation unit references.
# --defsym is the linker's own alias and needs nobody to reference it first. Measured before: the same trick
# for _CF_CONSTANT_STRING_SWIFT_CLASS produced a DYNAMIC symbol at the class's address.
FOUNDATION_DEFSYM = -Wl,--defsym,__CFConstantStringClassReference=._OBJC_CLASS_NSConstantString

FOUNDATION_CF_LIBS = $(FOUNDATION_DEFSYM) -L$(COREFOUNDATION_PREFIX)/lib -lcorefoundation \
                     -L$(LIBDISPATCH_PREFIX)/lib -ldispatch -lBlocksRuntime \
                     -L$(OBJC_PREFIX)/lib -lobjc \
                     -Wl,-rpath-link,$(COREFOUNDATION_PREFIX)/lib \
                     -Wl,-rpath-link,$(LIBDISPATCH_PREFIX)/lib \
                     -Wl,-rpath-link,$(OBJC_PREFIX)/lib

# ONE OBJECT PER CLASS, and the pattern rule is the whole build: there is no generated source, no
# configure, and (deliberately) no third build system — the same measurement the CF package's build.sh
# records for itself.
.build/foundation-%.o: $(FOUNDATION_SRC)/%.m
	@mkdir -p $(dir $@)
	$(MUSL64_OBJC) -c $(FOUNDATION_CFLAGS) $< -o $@

# THE SONAME IS NOT DECORATION: without it `-lfoundation` records NEEDED=libfoundation.so, so a guest that
# has the library staged as libfoundation.so.1 cannot find it — measured, as a relocation failure naming the
# directory it looked in and then every class symbol as missing. With the soname set, the link records the
# versioned name and the staged file is the one the loader asks for.
$(FOUNDATION_LIB): $(FOUNDATION_OBJS)
	@mkdir -p $(FNXLIB)
	$(MUSL64_OBJC) -shared -Wl,-soname,libfoundation.so.1 $(FOUNDATION_OBJS) $(FOUNDATION_CF_LIBS) -o $@
	@echo "foundation: $(words $(FOUNDATION_MSRCS)) class file(s) -> $(FOUNDATION_LIB)"

# STAGING, AND THE PREREQUISITE IS THE WHOLE POINT OF THESE TWO RULES. mk/20-userland.mk:2073 copies
# $(COREFOUNDATION_PREFIX)/lib/libcorefoundation.so.* into the image WITHOUT naming the file as a
# dependency, so a rebuilt CoreFoundation reaches the prefix and never reaches the guest. That is stale by
# construction, and it cost three guest runs to notice: runs 181 and 182 reported, accurately, that
# CFRetain did not reach this library — because in the library THAT IMAGE carried, it did not. A copy rule
# that names its input cannot lie that way, so these two do, and they live here rather than in the tangle.
CF_STAGED_LIB = $(ROOTFS64)/System/Libraries/libcorefoundation.so.1.1.0
FN_STAGED_LIB = $(ROOTFS64)/System/Libraries/libfoundation.so.1

$(CF_STAGED_LIB): $(COREFOUNDATION_PREFIX)/lib/libcorefoundation.so.1.1.0
	@mkdir -p "$(ROOTFS64)/System/Libraries"
	cp -a $(COREFOUNDATION_PREFIX)/lib/libcorefoundation.so.* "$(ROOTFS64)/System/Libraries/"

$(FN_STAGED_LIB): $(FOUNDATION_LIB)
	@mkdir -p "$(ROOTFS64)/System/Libraries"
	cp -a $(FOUNDATION_LIB) "$(ROOTFS64)/System/Libraries/libfoundation.so.1"

# THE FIRST ACCEPTANCE, and it is staged with its library: a probe that cannot find libfoundation at RUN
# time would fail for a reason that has nothing to do with what it asserts.
FOUNDATION_OBJECT_PROBE = foundation_object

$(ROOTFS64)/System/Shared/tests/$(FOUNDATION_OBJECT_PROBE): userland/tests/$(FOUNDATION_OBJECT_PROBE).m $(FOUNDATION_LIB) $(FN_STAGED_LIB) $(CF_STAGED_LIB)
	@mkdir -p "$(ROOTFS64)/System/Shared/tests"
	$(MUSL64_OBJC) userland/tests/$(FOUNDATION_OBJECT_PROBE).m $(FOUNDATION_CFLAGS) \
		-L$(FNXLIB) -lfoundation $(FOUNDATION_CF_LIBS) \
		-Wl,-rpath-link,$(FNXLIB) \
		-o "$@"

.PHONY: foundation2 cf-staged
foundation2: $(ROOTFS64)/System/Shared/tests/$(FOUNDATION_OBJECT_PROBE) $(FN_STAGED_LIB) $(CF_STAGED_LIB)

# A NAME FOR THE STAGING ALONE, so a change to CoreFoundation can be verified without relinking a probe.
cf-staged:  $(CF_STAGED_LIB)
