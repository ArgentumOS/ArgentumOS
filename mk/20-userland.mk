m0clang: userland64 $(COMPILER_RT_BUILTINS) tools/musl-clang64.sh tools/musl-clang64-static.sh
	$(MUSL64_CLANG) userland/tests/hello.c -o $(M0CLANG_DIR)/clang-hello-dl
	$(MUSL64_CLANG_STATIC) userland/tests/hello.c -o $(M0CLANG_DIR)/clang-hello-static
	@echo "m0clang: clang-built hellos staged under System/Shared/tests"

dash64: $(DASH64_BIN)
$(DASH64_BIN): $(MUSL64_LIBC) third_party/dash-fsh.patch
	cd third_party/dash && git apply $(CURDIR)/third_party/dash-fsh.patch && \
		./autogen.sh && \
		CC="$(MUSL64_CC)" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(CURDIR)/$(DASH64_BIN) && \
		git checkout -- .

toybox64: $(TOYBOX64_BIN)
$(TOYBOX64_BIN): $(MUSL64_LIBC) tools/mktoybox.sh $(MUSL64_CC) $(FNXLIB_CONFIG)
	TOYBOX_CC="$(MUSL64_CC)" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $(TOYBOX64_BIN)

# --- the static recovery set (docs/shared-libraries-plan.md §2.4/§3) ---
# Static-by-choice binaries that must run when /System/Libraries is
# corrupt or missing: a static dash (RECOVERY_PROGRAM, exec'd by the
# kernel) + a static toybox (the repair tools; its account applets need
# libconfig, hence the static libconfig.a). Both are built with
# MUSL64_CC_STATIC after the dynamic world (serial make order: dash64 /
# toybox64 above run first, so the in-tree third_party builds are not
# clobbered out from under the dynamic outputs).
RECOVERY64      = .build/recovery64
DASH64_RECOVERY = $(RECOVERY64)/dash-static
TOYBOX64_RECOVERY = $(RECOVERY64)/toybox-static

$(FNXLIB)/libconfig.a: $(FNXLIB_CONFIG) userland/libconfig.c \
	userland/libconfig_plist.c userland/libconfig_internal.h \
	userland/plist.c userland/libconfig.h
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/libconfig.c -o $(FNXLIB)/libconfig.o
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/libconfig_plist.c -o $(FNXLIB)/libconfig-plist.o
	$(MUSL64_CC) -Iinclude -Iuserland -c userland/plist.c -o $(FNXLIB)/libconfig-core.o
	ar rcs $@ $(FNXLIB)/libconfig.o $(FNXLIB)/libconfig-plist.o $(FNXLIB)/libconfig-core.o

$(DASH64_RECOVERY): $(MUSL64_LIBC) third_party/dash-fsh.patch
	@mkdir -p $(RECOVERY64)
	cd third_party/dash && git apply $(CURDIR)/third_party/dash-fsh.patch && \
		./autogen.sh && \
		CC="$(MUSL64_CC_STATIC)" ./configure --host=x86_64-linux --disable-fnmatch --disable-glob && \
		$(MAKE) && strip src/dash && cp src/dash $(CURDIR)/$(DASH64_RECOVERY) && \
		git checkout -- .

$(TOYBOX64_RECOVERY): $(MUSL64_LIBC) tools/mktoybox.sh $(MUSL64_CC_STATIC) $(FNXLIB)/libconfig.a
	@mkdir -p $(RECOVERY64)
	# TOYBOX_STAGE points the static build at its own scratch root so the
	# recovery run cannot clobber .build/toybox-root (the dynamic staging
	# userland64 copies as System/Tools/toybox) with a static toybox.
	TOYBOX_CC="$(MUSL64_CC_STATIC)" TOYBOX_STAGE="$(CURDIR)/$(RECOVERY64)/toybox-stage" ./tools/mktoybox.sh
	cp third_party/toybox/toybox $@

.PHONY: recovery64
recovery64: $(DASH64_RECOVERY) $(TOYBOX64_RECOVERY)
	@echo "recovery64: static recovery set built ($(DASH64_RECOVERY), $(TOYBOX64_RECOVERY))"


# --- Xfb: FNX's native X server (fork of Xvfb; the pristine upstream is
# not vendored - regenerate it on demand, see userland/xfb/README.md).
# xfb64 is phony and always delegates: the inner per-file rules own the
# incremental rebuild (a file-prerequisite here would go stale forever,
# since .build/x11/xfb/Xfb has no source deps at this level).
XFB_SRC = userland/xfb
XFB_OUT = .build/x11/xfb
XFB_BIN = $(XFB_OUT)/Xfb

.PHONY: xfb64
xfb64: $(FNXLIB_CONFIG)
	# Xfb links dynamic (M2): its third-party deps (pixman, xkbfile,
	# Xfont2, Xau) are shared .so in /System/Libraries; the server's own
	# archives stay in the binary. libsha1.a + the server archives are
	# the only static pieces left (single-consumer FNX code).
	$(MAKE) -C $(XFB_SRC) OUT="$(CURDIR)/$(XFB_OUT)" \
		CC="$(MUSL64_CC)" -j8


# ---------------------------------------------------------------------------
# userland64: stage the native x86_64 root in the FNX hierarchy
# (docs/fsh-proposal.md). The root contains exactly the five top-level
# entries: Applications/, Shared/, System/, Users/, Volumes/. Tools live in
# System/Tools, machine config in System/Configuration, device nodes come
# from devfs at /System/Devices (boot-time kernel mount), and the
# virtual-fs mount points (/System/Processes, devpts under Devices) exist
# as directories.
# ---------------------------------------------------------------------------
# toybox installs applets into PREFIX/{bin,sbin,usr/...} per toy flags;
# stage into a scratch root and merge every applet dir into System/Tools.
TOYBOX64_STAGE = .build/toybox-root

# The Foundation (docs/design/foundation-plan.md F0): the root class, as a shared
# library beside libconfig. Declared HERE, not in mk/00-base.mk, because a make
# target's name and prerequisites are expanded when the rule is READ - and
# $(OBJC_STAMP) only exists once mk/10-toolchain.mk has been included.
# The string family (F1) and the tagged-string class (F2 of the build, F1 of the
# plan). The compile flags are per file and each for a measured reason:
#   -fno-objc-arc   NSObject.m and NSTinyString.m IMPLEMENT -retain/-release,
#                   which ARC forbids (they are the MRR files);
#   -Wno-objc-missing-super-calls  every ARC -dealloc: clang emits the super
#                   chain itself, so the warning is unactionable noise;
#   -Wno-incomplete-implementation  NSString is ABSTRACT: its primitives are
#                   implemented by the concrete subclasses.
FOUNDATION_SRCS = $(FOUNDATION_SRC)/NSObject.m $(FOUNDATION_SRC)/NSString.m \
	$(FOUNDATION_SRC)/NSTinyString.m $(FOUNDATION_SRC)/NSNumber.m \
	$(FOUNDATION_SRC)/NSData.m $(FOUNDATION_SRC)/NSDate.m \
	$(FOUNDATION_SRC)/NSArray.m $(FOUNDATION_SRC)/NSDictionary.m \
	$(FOUNDATION_SRC)/NSError.m $(FOUNDATION_SRC)/NSException.m \
	$(FOUNDATION_SRC)/NSURLError.m \
	$(FOUNDATION_SRC)/NSCharacterSet.m $(FOUNDATION_SRC)/NSIndexSet.m \
	$(FOUNDATION_SRC)/NSIndexPath.m \
	$(FOUNDATION_SRC)/NSLocale.m \
	$(FOUNDATION_SRC)/NSMethodSignature.m \
	$(FOUNDATION_SRC)/NSInvocation.m \
	$(FOUNDATION_SRC)/NSInvocation_amd64.S \
	$(FOUNDATION_SRC)/NSInvocation.h $(FOUNDATION_SRC)/NSMethodSignature.h \
	$(FOUNDATION_SRC)/NSEnumerator.m $(FOUNDATION_SRC)/NSDirectoryEnumerator.m \
	$(FOUNDATION_SRC)/NSPropertyListSerialization.m \
	$(FOUNDATION_SRC)/NSTimeZone.m $(FOUNDATION_SRC)/NSDateComponents.m \
	$(FOUNDATION_SRC)/NSCalendar.m \
	$(FOUNDATION_SRC)/NSURL.m \
	$(FOUNDATION_SRC)/NSKeyValueCoding.m \
	$(FOUNDATION_SRC)/NSSortDescriptor.m \
	$(FOUNDATION_SRC)/NSPredicate.m \
	$(FOUNDATION_SRC)/NSPredicateFormat.m \
	$(FOUNDATION_SRC)/NSPredicate.h \
	$(FOUNDATION_SRC)/NSDataCodec.m \
	$(FOUNDATION_SRC)/NSData.h \
		$(FOUNDATION_SRC)/NSCalendar.h \
	$(FOUNDATION_SRC)/NSFormatter.m \
	$(FOUNDATION_SRC)/NSDateFormatter.m \
	$(FOUNDATION_SRC)/NSNumberFormatter.m \
	$(FOUNDATION_SRC)/NSSet.m \
	$(FOUNDATION_SRC)/NSValue.m \
	$(FOUNDATION_SRC)/NSNull.m \
	$(FOUNDATION_SRC)/NSCountedSet.m \
	$(FOUNDATION_SRC)/NSOrderedSet.m \
	$(FOUNDATION_SRC)/NSKeyValueObserving.m \
	$(FOUNDATION_SRC)/NSExpression.m \
	$(FOUNDATION_SRC)/NSComparisonPredicate.m \
	$(FOUNDATION_SRC)/NSCoder.m \
	$(FOUNDATION_SRC)/NSKeyedArchiver.m \
	$(FOUNDATION_SRC)/NSProcessInfo.m \
	$(FOUNDATION_SRC)/NSFileManager.m \
	$(FOUNDATION_SRC)/NSFileAccessIntent.m \
	$(FOUNDATION_SRC)/NSFileCoordinator.m \
	$(FOUNDATION_SRC)/NSFileVersion.m \
	$(FOUNDATION_SRC)/NSFileProviderService.m \
	$(FOUNDATION_SRC)/NSXMLNode.m \
	$(FOUNDATION_SRC)/NSXMLDocument.m \
	$(FOUNDATION_SRC)/NSXMLDTD.m \
	$(FOUNDATION_SRC)/NSXMLParser.m \
	$(FOUNDATION_SRC)/NSFileSecurity.m \
	$(FOUNDATION_SRC)/NSFileWrapper.m \
	$(FOUNDATION_SRC)/NSURL.h \
	$(FOUNDATION_SRC)/NSURLComponents.m \
	$(FOUNDATION_SRC)/NSRegularExpression.m \
	$(FOUNDATION_SRC)/NSLock.m \
	$(FOUNDATION_SRC)/NSThread.m \
	$(FOUNDATION_SRC)/NSRunLoop.m \
	$(FOUNDATION_SRC)/NSOperation.m \
	$(FOUNDATION_SRC)/NSGeometry.m \
	$(FOUNDATION_SRC)/NSProgress.m \
	$(FOUNDATION_SRC)/NSURLRequest.m \
	$(FOUNDATION_SRC)/NSURLResponse.m \
	$(FOUNDATION_SRC)/NSCachedURLResponse.m \
	$(FOUNDATION_SRC)/NSURLProtocol.m \
	$(FOUNDATION_SRC)/NSHTTPURLResponse.m
FOUNDATION_HDRS = $(FOUNDATION_SRC)/NSObjCRuntime.h $(FOUNDATION_SRC)/NSObject.h \
	$(FOUNDATION_SRC)/NSByteOrder.h \
	$(FOUNDATION_SRC)/NSUUID.h \
	$(FOUNDATION_SRC)/NSDateInterval.h \
	$(FOUNDATION_SRC)/NSValueTransformer.h \
	$(FOUNDATION_SRC)/NSAffineTransform.h \
	$(FOUNDATION_SRC)/NSAutoreleasePool.h \
	$(FOUNDATION_SRC)/NSProxy.h \
	$(FOUNDATION_SRC)/NSUndoManager.h \
	$(FOUNDATION_SRC)/NSJSONSerialization.h \
	$(FOUNDATION_SRC)/NSGeometry.h \
	$(FOUNDATION_SRC)/NSString.h \
	$(FOUNDATION_SRC)/NSTinyString.h $(FOUNDATION_SRC)/NSNumber.h \
	$(FOUNDATION_SRC)/NSData.h $(FOUNDATION_SRC)/NSDate.h \
	$(FOUNDATION_SRC)/NSCoding.h $(FOUNDATION_SRC)/NSCoder.h \
	$(FOUNDATION_SRC)/NSKeyedArchiver.h \
	$(FOUNDATION_SRC)/NSProcessInfo.h \
	$(FOUNDATION_SRC)/NSFileManager.h \
	$(FOUNDATION_SRC)/NSFileCoordinator.h \
	$(FOUNDATION_SRC)/NSFilePresenter.h \
	$(FOUNDATION_SRC)/NSFileVersion.h \
	$(FOUNDATION_SRC)/NSFileProviderService.h \
	$(FOUNDATION_SRC)/NSXMLNode.h \
	$(FOUNDATION_SRC)/NSXMLDocument.h \
	$(FOUNDATION_SRC)/NSXMLDTD.h \
	$(FOUNDATION_SRC)/NSXMLParser.h \
	$(FOUNDATION_SRC)/NSFileSecurity.h \
	$(FOUNDATION_SRC)/NSFileWrapper.h \
	$(FOUNDATION_SRC)/NSURLComponents.h \
	$(FOUNDATION_SRC)/NSRegularExpression.h \
	$(FOUNDATION_SRC)/NSLock.h \
	$(FOUNDATION_SRC)/NSThread.h \
	$(FOUNDATION_SRC)/NSTimer.h \
	$(FOUNDATION_SRC)/NSRunLoop.h \
	$(FOUNDATION_SRC)/NSOperation.h \
	$(FOUNDATION_SRC)/NSOperationQueue.h \
	$(FOUNDATION_SRC)/NSProgress.h \
	$(FOUNDATION_SRC)/NSFastEnumeration.h $(FOUNDATION_SRC)/NSArray.h \
	$(FOUNDATION_SRC)/NSDictionary.h $(FOUNDATION_SRC)/NSError.h \
	$(FOUNDATION_SRC)/NSException.h $(FOUNDATION_SRC)/NSCharacterSet.h \
	$(FOUNDATION_SRC)/NSIndexSet.h $(FOUNDATION_SRC)/NSIndexPath.h \
	$(FOUNDATION_SRC)/NSLocale.h \
	$(FOUNDATION_SRC)/NSMethodSignature.h \
	$(FOUNDATION_SRC)/NSInvocation.h \
	$(FOUNDATION_SRC)/NSFormatter.h \
	$(FOUNDATION_SRC)/NSDateFormatter.h \
	$(FOUNDATION_SRC)/NSNumberFormatter.h \
	$(FOUNDATION_SRC)/NSSet.h \
	$(FOUNDATION_SRC)/NSValue.h \
	$(FOUNDATION_SRC)/NSNull.h \
	$(FOUNDATION_SRC)/NSCountedSet.h \
	$(FOUNDATION_SRC)/NSOrderedSet.h \
	$(FOUNDATION_SRC)/NSMutableOrderedSet.h \
	$(FOUNDATION_SRC)/NSKeyValueObserving.h \
	$(FOUNDATION_SRC)/NSEnumerator.h \
	$(FOUNDATION_SRC)/NSDirectoryEnumerator.h \
	$(FOUNDATION_SRC)/NSPropertyListSerialization.h \
	$(FOUNDATION_SRC)/NSDateComponents.h \
	$(FOUNDATION_SRC)/NSTimeZone.h \
	$(FOUNDATION_SRC)/NSCalendar.h \
	$(FOUNDATION_SRC)/NSURL.h \
	$(FOUNDATION_SRC)/NSKeyValueCoding.h \
	$(FOUNDATION_SRC)/NSSortDescriptor.h \
	$(FOUNDATION_SRC)/NSPredicate.h \
	$(FOUNDATION_SRC)/NSURLRequest.h \
	$(FOUNDATION_SRC)/NSURLResponse.h \
	$(FOUNDATION_SRC)/NSCachedURLResponse.h \
	$(FOUNDATION_SRC)/NSURLProtocol.h \
	$(FOUNDATION_SRC)/FNCURLURLProtocol.h \
	$(FOUNDATION_SRC)/NSURLSessionConfiguration.h \
	$(FOUNDATION_SRC)/NSURLSessionTask.h \
	$(FOUNDATION_SRC)/NSURLSession.h \
	$(FOUNDATION_SRC)/NSHTTPURLResponse.h \
	$(FOUNDATION_SRC)/Foundation.h
# -Iinclude: the plist CORE (include/plist.h) is shared with libconfig, which
# consumes it from C — see the one-core-two-skins decision in the plan.
# F6 (nullability): a header that carries ANY annotation must carry them ALL —
# clang's -Wnullability-completeness is what says so, and as an ERROR it is the
# sweep's own gate: a file with some annotations and not others fails the build,
# while an untouched file (no annotations at all) stays silent, which is what lets
# the sweep land one slice at a time.
FOUNDATION_CFLAGS = -fPIC -Iinclude -Wno-objc-missing-super-calls -Wno-incomplete-implementation \
	-Werror=nullability-completeness
# WHY THE LIBRARY HAS NO -fobjc-arc, stated as the POLICY it is (user, 2026-09-19): ARC is for
# everything that USES the Foundation; the Foundation's own ownership is a free choice as long as it
# works, and it is MANUAL here. This replaces a note claiming clang refuses -fobjc-arc on this
# platform, which the build contradicts: EVERY PROBE IS COMPILED -fobjc-arc, one file per command so
# the flag applies, and foundation_core's arc-pool check - whose semantics depend on ARC being real -
# PASSES on the guest. The old note was a mis-scoped measurement (F13.21) that stood for several
# milestones with a sweep-style conclusion drawn from it. WHAT IT MEANT TO SAY: the LIBRARY's
# ownership is manual, not that ARC is unavailable here.

# FOUNDATION COMPILE RULES, GENERATED. One rule per source from the directory listing, so a new .m file needs no
# hand-written rule; and EACH OBJECT DEPENDS ON ITS .m, so editing a source rebuilds exactly its own object instead
# of leaving a stale library that make never notices.
# THE PER-FILE FLAGS WERE READ OFF the block this replaces: -fno-objc-arc for the MRC files (NSObject.m,
# NSTinyString.m, NSDateInterval.m), the ICU prefix for the 5 files including <unicode/...>, the X11 prefix for
# NSDataCodec.m (the only file including <zlib.h>, hence -lz on the link), -Wno-objc-root-class for NSProxy.m.
# AND A CORRECTION TO THIS BLOCK'S OWN COMMENT: $(FOUNDATION_CFLAGS) selects NO ARC AT ALL - the whole
# library is MRC - so the three-entry MRC list below is REDUNDANT. It is kept because it was read off
# the block this replaced, and because a redundant flag costs nothing; the sentence it replaces
# ("$(FOUNDATION_CFLAGS) already selects ARC") was simply wrong.
FN_FOUNDATION_SRCS  = $(notdir $(wildcard $(FOUNDATION_SRC)/*.m))
FN_FOUNDATION_NOARC = NSObject.m NSTinyString.m NSDateInterval.m
# W7 slice 2c: the libcurl BRIDGE is the one file that includes <curl/curl.h>, so curl's prefix is on
# ITS include path - the same per-file rule the ICU and X11 lists follow. The LINK needs the new
# libraries, because the bridge lives IN the library (below).
FN_FOUNDATION_CURL   = FNCURLURLProtocol.m
# W7 §58: the STREAM TASK is the SECOND file with a third-party header on its include path, because
# -startSecureConnection wraps its descriptor in a TLS session: <openssl/ssl.h>, from the libressl the
# library ALREADY links. (curl is the reason libssl is on the link line, and the reason this file needs
# no new library either - it needs the HEADERS, which is what a per-file include list is for.)
FN_FOUNDATION_SSL    = NSURLSessionStreamTask.m FNWebSocketHandshake.m NSURLSessionWebSocketTask.m
# THREE ENTRIES NOW, AND THE THIRD EARNED ITS PLACE THE HARD WAY: NSURLSessionWebSocketTask.m masks its frames with
# RAND_bytes (<openssl/rand.h>), and being absent from this list is what made the library build fail with
# "'openssl/rand.h' file not found" - the very trap the note below describes, met by the file that needed it.
# FNWebSocketHandshake.m is here because §4.2.2's accept digest is a SHA-1; NSURLSessionStreamTask.m for the TLS
# upgrade. THE RULE: a file that needs one of libressl's headers must be listed here, or it fails on the GUEST
# ONLY - the host has those headers on its default include path.
# NSCharacterSet.m JOINED THIS TABLE IN §15.5: its four ICU-backed rule sets read the general
# category and the decomposition type, so <unicode/uchar.h> is on its include path. The link needed
# nothing new - libfoundation has needed libicui18n/libicuuc/libicudata since F13.6.
# THE ICU-HEADER LIST IS PER FILE, and a file that needs it and is not here fails on the GUEST ONLY
# (the host has ICU's headers on its default include path). NSDecimalNumber.m asks ICU for the locale's
# decimal separator, so it belongs in this list - which the guest build is what proved.
FN_FOUNDATION_ICU   = NSCalendar.m NSDateFormatter.m NSNumberFormatter.m NSPredicate.m NSTimeZone.m \
                      NSCharacterSet.m NSLocale.m NSDecimalNumber.m NSListFormatter.m \
                      NSISO8601DateFormatter.m NSDateIntervalFormatter.m NSByteCountFormatter.m \
                      NSRelativeDateTimeFormatter.m NSDateComponentsFormatter.m NSMeasurementFormatter.m \
                      NSSecureUnarchiveFromDataTransformer.m NSPointerFunctions.m \
                      NSPointerArray.m FNPointerTable.m NSHashTable.m NSMapTable.m \
                      NSPurgeableData.m NSCache.m
FN_FOUNDATION_X11   = NSDataCodec.m
FN_FOUNDATION_ROOT  = NSProxy.m
FN_FOUNDATION_OBJS  = $(addprefix .build/foundation-,$(FN_FOUNDATION_SRCS:.m=.o)) .build/foundation-ninvoke-asm.o

define FN_FOUNDATION_rule
.build/foundation-$(1:.m=.o): $(FOUNDATION_SRC)/$(1)
	$$(MUSL64_OBJC) -c $$(FOUNDATION_CFLAGS) \
		$(if $(filter $(1),$(FN_FOUNDATION_ROOT)),-Wno-objc-root-class) \
		$(if $(filter $(1),$(FN_FOUNDATION_NOARC)),-fno-objc-arc) \
		$(if $(filter $(1),$(FN_FOUNDATION_ICU)),-I$(ICUPREFIX)/include) \
		$(if $(filter $(1),$(FN_FOUNDATION_X11)),-I$(X11PREFIX)/include) \
		$(if $(filter $(1),$(FN_FOUNDATION_CURL)),-I$(CURL_PREFIX)/include) \
		$(if $(filter $(1),$(FN_FOUNDATION_SSL)),-I$(LIBRESSL_PREFIX)/include) \
		-Iuserland $$< -o $$@
endef
$(foreach f,$(FN_FOUNDATION_SRCS),$(eval $(call FN_FOUNDATION_rule,$(f))))
.build/foundation-ninvoke-asm.o: $(FOUNDATION_SRC)/NSInvocation_amd64.S
	$(MUSL64_CC) -c -fPIC $< -o $@
$(FOUNDATION_LIB): $(FOUNDATION_SRCS) $(FOUNDATION_HDRS) $(OBJC_STAMP) $(FN_FOUNDATION_OBJS)
	@mkdir -p $(FNXLIB)
	$(MUSL64_CC) -c -fPIC -Iinclude userland/plist.c -o .build/plist.o
	# F7: the calendar family. NSCalendar.m and NSTimeZone.m are ARC; the
	# components bag owns nothing but its fields.
	#
	# F13.7a: NSTimeZone.m NOW INCLUDES ICU (<unicode/ucal.h>, <unicode/uenum.h>), because the class
	# reads the zone database instead of refusing it — so the ICU prefix is on ITS include path.
	# NSCalendar.m and NSDateComponents.m do not include ICU: their arithmetic stays on libc's
	# struct tm and they reach the database only through NSTimeZone.
	# F13.7b: NSCalendar.m NOW INCLUDES ICU (<unicode/ucal.h>), because the class reads every
	# calendar out of it instead of refusing the ones whose tables it lacked. So the ICU prefix is
	# on ITS include path too; the link needed nothing new (F13.6 already made libfoundation need
	# libicui18n/libicuuc/libicudata).
	# F8: the URL value type.
	# F9: the key-value coding family — a category on NSObject, so nothing here
	# owns its storage; the lookup goes through the runtime's ivar table.
	# F10: the sorting family. NSSortDescriptor.m resolves its key through KVC and
	# calls a comparison selector through its OWN return type (a scalar, not `id`).
	# F11a: the predicate object model — an abstract base, two private leaves, the tree
	# node, and the two collection filters. The block leaf is why this file stores a block.
	#
	# F13.7d: NSPredicate.m NOW INCLUDES ICU (<unicode/ucol.h>, for the `[d]` collation) and
	# <regex.h> (musl's POSIX engine, which is inside libc — so MATCHES adds no link and no
	# artifact). The ICU include path is therefore on THIS rule; the link needed nothing new,
	# because F13.6 already made libfoundation need libicui18n/libicuuc/libicudata.
	# F11b: the format grammar. A category on NSPredicate, so the parser lives beside the object
	# model without either file owning the other.
	# F12: the compression binding. NSDataCodec.m is the ONLY file that includes <zlib.h>, so the X11
	# prefix is on ITS include path — and on the LINK line below, because libfoundation now needs
	# libz.so.1. That library is already staged into the guest for the X11 stack, so this adds a
	# dependency and no new artifact (docs/design/foundation-plan.md, F12).
	# F13.6: the value-to-text family. NSFormatter.m is the abstract base and needs nothing extra;
	# NSDateFormatter.m is the file that includes <unicode/udat.h> and <unicode/udatpg.h>, so the
	# ICU prefix is on ITS include path — and on the LINK line below, because libfoundation now
	# needs libicui18n/libicuuc/libicudata. Those libraries and their data package are already
	# staged into the guest (docs/design/foundation-plan.md §10, F13), so this adds a dependency
	# and no new artifact — the same shape as F12's libz.
	# F13.7c: the number formatter. It includes <unicode/unum.h>, so the ICU prefix is on ITS
	# include path; the LINK needs nothing new, because F13.6 already made libfoundation need
	# libicui18n/libicuuc/libicudata.
	# F13.7e: the shared calendar-keyword bridge (NSCalendar.m/.h). It is its OWN translation unit
	# because TWO classes call it — NSCalendar and NSDateFormatter — and it includes only
	# <Foundation/...> headers, so it needs no ICU include path of its own.
	# F13.8: the unordered collection. It includes <Foundation/...> headers only — no ICU, no zlib —
	# because a set is RULES rather than data, which is why it was a gap in the plan's refusal table
	# rather than an entry in it.
	# W2h: the 128-bit identifier. <Foundation/...> headers only — the entropy comes from
	# getentropy, so no ICU include path is needed.
	# W2h: the autorelease pool boundary. It needs the RUNTIME's pool primitives, from
	# W2h: JSON. Foundation headers plus string.h and math.h; no ICU, no zlib.
	# W2h: the undo manager's core. Foundation headers plus the runtime, for the selector send.
	# W2h: the second root class. It needs the runtime's own allocation and disposal.
	# -Wno-protocol IS DELIBERATE AND NOT NOISE: a proxy FORWARDS -isKindOfClass: and -isMemberOfClass:
	# rather than implementing them - that is Apple's documented behaviour and the reason -isProxy
	# exists - so the compiler correctly observes that this class does not satisfy the whole NSObject
	# protocol itself. The same shape, and the same justification, as the probe rules' 
	# -Wno-incomplete-implementation beside them.
	# <objc/objc-arc.h>, which is already on the include path.
	# W2h: the affine transform. <Foundation/...> headers and libm, for sin/cos.
	# W2h: the value transformer. <Foundation/...> headers plus the runtime, for NSClassFromString.
	# W2h: a span of time. <Foundation/...> headers only - it is dates and arithmetic, no ICU.
	# F13.8c: the box for everything that is not an object, and the object that stands for nothing.
	# Same shape as the set: <Foundation/...> headers only, no ICU and no zlib.
	# F13.8d: the counted set. A SUBCLASS of NSMutableSet, so its initialisers have to reach the
	# counts — see the file's header for why the array form may not go through the superclass's.
	# F13.8e: the ordered set and its mutable half, in ONE translation unit (they share no
	# superclass relation, so there is no NSMutableSet-style reason to split them).
	# F13.9: the observer registry. <Foundation/...> headers only, like the collections.
	# F13.10: the expression tree. It reads collections and key paths, so it includes the NSSet
	# header; no ICU and no zlib.
	# F13.11: the expression-shaped comparison. It shares the comparison rule with the grammar's
	# leaf through FNCompareValues, so this file states no rule of its own.
	# F13.12: the coder family. NSKeyedArchiver.m includes the plist serialisation and the runtime
	# (it looks a class up BY NAME), so it is the one foundation source with those two dependencies.
	# F13.13: the process service. It reads /proc and the C library's environ, so it is the one
	# source here that includes <unistd.h> and <stdio.h>.
	# F13.14: the file system service. It is the one source here that walks directories and calls
	# open/read/write itself, because a recursive copy has no syscall to lean on.
	# F13.15: the structured URL and RFC 3986 §5.2's resolution. NSURL.h is the bridge NSURL's
	# relative door reaches the algorithm through.
	# F13.16: regular expressions, on the engine musl already ships inside libc. It includes
	# <regex.h> and nothing else new.
	# F13.17: the locking classes and NSThread, over the pthreads musl already ships. These are the
	# only two sources here that include <pthread.h>.
	# F13.18: NSTimer and NSRunLoop in one unit, because they are one design — a timer names a date
	# and the loop is what waits for dates. Its wait is select(2), not nanosleep(2): F13.17 measured
	# that this kernel returns from nanosleep early.
	# F13.19: the operation and the queue that schedules it. The second source here that uses the
	# thread family — NSThread for the workers, NSCondition for the drain.
	# F13.20: the progress tree. It includes <pthread.h> for its per-thread current stack, and
	# nothing else new.
	# W7 slice 2c: THE BRIDGE MAKES THE LIBRARY DEPEND ON LIBCURL, and libcurl on LibreSSL.
	# The order is the dependency order (-lcurl -lssl -lcrypto), as curl_smoke records, and no RPATH
	# is needed: the guest loader resolves libcurl.so.4 out of /System/Libraries, where slice 2b stages
	# it. Precedent rather than a new kind of dependency: the library already needs libz (one codec)
	# and ICU (F13.6). And the comment sits ABOVE the command, not inside it, because a line inside a
	# backslash-continued recipe is part of that command: a '#' there does not comment out a makefile
	# line, it comments out the rest of the SHELL command — measured here as a link that silently lost
	# every flag after it.
	$(MUSL64_OBJC) -shared -Wl,-soname,libfoundation.so.1 \
		$(FN_FOUNDATION_OBJS) \
		.build/plist.o -L$(X11PREFIX)/lib -lz -L$(ICUPREFIX)/lib -licui18n -licuuc -licudata \
		-L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto -o $@
	ln -sf libfoundation.so.1 $(FNXLIB)/libfoundation.so
userland64: toolchain-gate $(MUSL64_LIBC) $(DASH64_BIN) $(TOYBOX64_BIN) $(LLVM_CXX_STAMP) $(OBJC_STAMP) foundation-gate $(FOUNDATION_LIB) $(CG_LIB) $(LVGL64) $(XFB_BIN) $(FNXLIB_CONFIG) $(DASH64_RECOVERY) $(TOYBOX64_RECOVERY)
	rm -rf $(ROOTFS64)
	@mkdir -p $(ROOTFS64)
	# third-party X11 + toolchain tests live under System/Shared
	@mkdir -p "$(ROOTFS64)/System/Shared/X11/bin" "$(ROOTFS64)/System/Shared/tests"
	# --- the FSH skeleton (spaced names verbatim, Q7). No root /tmp: the
	# FSH maps /tmp to /System/Temporary Files (staged below); fshlint
	# bans the /tmp string in System/Tools; fs_repair_tmpdir() recreates
	# /System/Temporary Files at mount if a kill-replay left it non-dir. ---
	# Application Support: behaviour material (scripts, app data) per the
	# config policy carve-out - the same domain key and scope tree as
	# Configuration/, a different payload kind (config-design.md §0).
	@mkdir -p "$(ROOTFS64)/System/Application Support" \
		"$(ROOTFS64)/Shared/Application Support" \
		"$(ROOTFS64)/System/User Template/Application Support" \
	@mkdir -p "$(ROOTFS64)/Applications" "$(ROOTFS64)/Volumes"
	@mkdir -p "$(ROOTFS64)/Shared/Configuration" "$(ROOTFS64)/Shared/Libraries" \
		"$(ROOTFS64)/Shared/Fonts" "$(ROOTFS64)/Shared/Images" \
		"$(ROOTFS64)/Shared/Sounds" "$(ROOTFS64)/Shared/Videos" \
		"$(ROOTFS64)/Shared/Documentation" "$(ROOTFS64)/Shared/Themes"
	@mkdir -p "$(ROOTFS64)/System/Tools" "$(ROOTFS64)/System/Libraries" \
		"$(ROOTFS64)/System/Configuration" "$(ROOTFS64)/System/Devices" \
		"$(ROOTFS64)/System/Devices/pts" "$(ROOTFS64)/System/Processes" \
		"$(ROOTFS64)/System/ESP" "$(ROOTFS64)/System/Documentation/HTML/FNX" \
		"$(ROOTFS64)/System/Documentation/PDF/FNX" \
		"$(ROOTFS64)/System/Source Code" "$(ROOTFS64)/System/Shared/Fonts" \
		"$(ROOTFS64)/System/Shared/Images/Icons" \
		"$(ROOTFS64)/System/Shared/Images/Wallpaper" \
		"$(ROOTFS64)/System/Shared/Sounds" "$(ROOTFS64)/System/Shared/Videos" \
		"$(ROOTFS64)/System/Shared/X11/xkb" \
		"$(ROOTFS64)/System/Temporary Files" \
		"$(ROOTFS64)/System/Variable Data/X11/xkb/compiled" \
		"$(ROOTFS64)/System/User Template/Configuration" \
		"$(ROOTFS64)/System/User Template/Applications" \
		"$(ROOTFS64)/System/User Template/Documents" \
		"$(ROOTFS64)/System/User Template/Desktop" \
		"$(ROOTFS64)/System/User Template/Music" \
		"$(ROOTFS64)/System/User Template/Pictures" \
		"$(ROOTFS64)/System/User Template/Videos" \
		"$(ROOTFS64)/System/User Template/Shared/Libraries" \
		"$(ROOTFS64)/System/User Template/Shared/Fonts" \
		"$(ROOTFS64)/System/User Template/Shared/Images" \
		"$(ROOTFS64)/System/User Template/Shared/Sounds" \
		"$(ROOTFS64)/System/User Template/Shared/Videos" \
		"$(ROOTFS64)/System/User Template/Shared/Documentation" \
		"$(ROOTFS64)/System/User Template/Temporary Files" \
		"$(ROOTFS64)/System/User Template/Variable Data"
	# --- tools (executables) ---
	# toybox is built + installed by mktoybox.sh (fresh config + the
	# libconfig link); userland64 only flattens the staged applet dirs.
	@for d in bin sbin usr/bin usr/sbin; do \
		if [ -d "$(TOYBOX64_STAGE)/$$d" ]; then \
			cp -a $(TOYBOX64_STAGE)/$$d/. "$(ROOTFS64)/System/Tools/"; \
		fi; \
	done
	# toybox install links applets with PREFIX-relative targets that break
	# once bin/sbin/usr/bin are flattened into one Tools dir: point every
	# symlink at the toybox binary sitting next to it.
	@cd "$(ROOTFS64)/System/Tools" && for l in *; do \
		if [ -L "$$l" ]; then ln -sfn toybox "$$l"; fi; \
	done
	# suid root: the kernel honors S_ISUID at exec, and toybox drops to
	# the real uid for every applet except the account tools
	# (TOYFLAG_STAYROOT/ROOTONLY) — that is what lets non-root `su`
	# authenticate and switch users (M4).
	@chmod 4755 "$(ROOTFS64)/System/Tools/toybox"
	@chmod 0755 "$(ROOTFS64)/System/Tools/config" \
		"$(ROOTFS64)/System/Tools/init" 2>/dev/null || true
	# init reads the machine configuration THROUGH LIBCONFIG (P3c-b): one reader
	# for the domains it needs, and the reason a domain written as a plist can no
	# longer strand the boot mounts. -lconfig is the staged shared library, like
	# every other userland tool's.
	$(MUSL64_CC) -Iuserland -L$(FNXLIB) userland/tools/init.c -lconfig \
		-o "$(ROOTFS64)/System/Tools/init"
	$(MUSL64_CXX) userland/tests/cpp_smoke.cpp -o "$(ROOTFS64)/System/Shared/tests/cpp_smoke"
	# objc_smoke: the Objective-C runtime (docs/design/objc-toolchain-plan.md
	# P2/P3). TWO translation units on purpose: the class is implemented in
	# objc_smoke_support.m and its CATEGORY in objc_smoke.m, because cross-TU
	# class registration is the case that was misdiagnosed during P1 - it stays
	# in the acceptance now. The support unit is MRR (a root class cannot be
	# ARC), the other is ARC; they link into one binary.
	# -Wno-objc-root-class: SmokeObject IS a root class, deliberately.
	$(MUSL64_OBJC) -c -Wno-objc-root-class -Iuserland/tests \
		userland/tests/objc_smoke_support.m -o .build/objc-smoke-support.o
	$(MUSL64_OBJC) -c -Wno-objc-root-class -fobjc-arc -Iuserland/tests \
		userland/tests/objc_smoke.m -o .build/objc-smoke-main.o
	$(MUSL64_OBJC) .build/objc-smoke-support.o .build/objc-smoke-main.o \
		-o "$(ROOTFS64)/System/Shared/tests/objc_smoke"
	# sterlingc K1, the guest half (docs/design/sterling-plan.md §4). The four
	# host legs (make sterlingc-check) prove the emitted TEXT - the golden diff,
	# the corpus, the rejects, and that it compiles. None of them RUNS anything,
	# and "do not plan past K1 until it passes" is about the chain, so the
	# emitted class is linked with a hand-written driver here and executed on a
	# guest boot:
	#
	#     MyClass.ag -> sterlingc -> MyClass.h/.m -> clang -> libobjc2 -> Foundation
	#
	# The compiler itself runs on the HOST (it is a host tool; the guest only
	# runs what it produced), so this rule invokes it - and `--build` is first
	# because a stale .build/sterlingc would silently emit yesterday's output.
	#
	# THE INCLUDE BRIDGE IS LOAD-BEARING. §2 emits `#import <Foundation/Foundation.h>`,
	# Cocoa's capitalisation, and this tree's directory is `userland/Foundation`.
	# On a case-sensitive filesystem that import cannot resolve without a bridge
	# - the same one tools/sterlingc-compile.sh builds for the host, and clang's
	# -Wnonportable-include-path warning is how you can tell it is what resolved
	# it. The bridge is built beside the emitted headers so the generated include
	# search is self-contained and cannot be satisfied by a stale one elsewhere.
	tools/sterlingc.sh --build
	rm -rf .build/sterlingc/guest
	mkdir -p .build/sterlingc/guest/include
	ln -sfn "$(CURDIR)/userland/Foundation" .build/sterlingc/guest/include/Foundation
	.build/sterlingc/sterlingc -o .build/sterlingc/guest
	$(MUSL64_OBJC) -c -fobjc-arc \
		-I.build/sterlingc/guest -I.build/sterlingc/guest/include -Iuserland \
		userland/tests/sterlingc_k1.m -o .build/sterlingc-k1-main.o
	$(MUSL64_OBJC) -c -fobjc-arc \
		-I.build/sterlingc/guest -I.build/sterlingc/guest/include -Iuserland \
		.build/sterlingc/guest/MyClass.m -o .build/sterlingc-k1-myclass.o
	$(MUSL64_OBJC) .build/sterlingc-k1-main.o .build/sterlingc-k1-myclass.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/sterlingc_k1"
	# foundation_core: F0 acceptance (docs/design/foundation-plan.md). Two units
	# AND two ownership regimes: the subclass and the MRR lifetime exercises in
	# the support unit, the checks in the ARC unit. The ARC flag is EXPLICIT -
	# the wrapper never adds it - and without it clang emits no release at all and
	# the pool check fails (measured).
	# THE FORWARDING FIXTURES DECLARE METHODS THEY MUST NOT IMPLEMENT: FastForwarder's -marker and
	# SlowForwarder's -value/-setValue: are the CLAIM this probe tests (the runtime has to forward
	# them to the backing object), so -Wincomplete-implementation is not noise here — it is the
	# compiler correctly observing that the design is incomplete ON PURPOSE. The same shape, and the
	# same justification, as NSString's abstract primitives in FOUNDATION_CFLAGS above.
	$(MUSL64_OBJC) -c -Wno-objc-root-class -Wno-incomplete-implementation -fno-objc-arc \
		-Iuserland -Iuserland/tests \
		userland/tests/foundation_core_support.m -o .build/probe-foundation_core_support.o
	$(MUSL64_OBJC) -c -Wno-objc-root-class -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_core.m -o .build/probe-foundation_core.o
	$(MUSL64_OBJC) .build/probe-foundation_core_support.o .build/probe-foundation_core.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_core"
	# foundation_string: F1 acceptance (docs/design/foundation-plan.md). Two
	# units again, and the support unit is where the OTHER constant-string
	# cases live (a 4-character literal is a TAGGED pointer, a 20-character one
	# is an object - both paths have to work).
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_string_support.m -o .build/probe-foundation_string_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_string.m -o .build/probe-foundation_string.o
	$(MUSL64_OBJC) .build/probe-foundation_string_support.o .build/probe-foundation_string.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_string"
	# foundation_value: F2 acceptance. The support unit imports ONLY the
	# umbrella header, so a complete <Foundation/Foundation.h> is part of the
	# acceptance too.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_value_support.m -o .build/probe-foundation_value_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_value.m -o .build/probe-foundation_value.o
	$(MUSL64_OBJC) .build/probe-foundation_value_support.o .build/probe-foundation_value.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_value"
	# foundation_collection: F3 acceptance. The support unit builds a NESTED
	# collection, which is the cheapest check that collections are ordinary
	# objects; the main unit exercises clang's for-in lowering.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_collection_support.m -o .build/probe-foundation_collection_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_collection.m -o .build/probe-foundation_collection.o
	$(MUSL64_OBJC) .build/probe-foundation_collection_support.o .build/probe-foundation_collection.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_collection"
	# foundation_error: F4 acceptance. The support unit builds values from
	# another unit; the main unit exercises @try/@catch, so the runtime's throw
	# path is part of the check.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_error_support.m -o .build/probe-foundation_error_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_error.m -o .build/probe-foundation_error.o
	$(MUSL64_OBJC) .build/probe-foundation_error_support.o .build/probe-foundation_error.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_error"
	# foundation_calendar: F7 acceptance. The same two-unit shape, and the support
	# unit imports ONLY the umbrella — which is how the three new headers are
	# proved to have reached <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_calendar_support.m -o .build/probe-foundation_calendar_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_calendar.m -o .build/probe-foundation_calendar.o
	$(MUSL64_OBJC) .build/probe-foundation_calendar_support.o .build/probe-foundation_calendar.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_calendar"
	# foundation_url: F8 acceptance. Two units again, and the support unit imports
	# ONLY the umbrella — so this is also the proof that NSURL reached
	# <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_url_support.m -o .build/probe-foundation_url_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_url.m -o .build/probe-foundation_url.o
	$(MUSL64_OBJC) .build/probe-foundation_url_support.o .build/probe-foundation_url.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_url"
	# foundation_kvc: F9 acceptance. Two units again — and here the split is the
	# CLAIM under test: the support unit defines the OBJECTS and imports only the
	# umbrella, so a lookup that crossed translation units by name can only be the
	# runtime's.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_kvc_support.m -o .build/probe-foundation_kvc_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_kvc.m -o .build/probe-foundation_kvc.o
	$(MUSL64_OBJC) .build/probe-foundation_kvc_support.o .build/probe-foundation_kvc.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_kvc"
	# foundation_sort: F10 acceptance. Two units, and the split carries the claim again:
	# the support unit builds the OBJECTS and a descriptor from its own side, so a sort
	# whose key was resolved by name can only have gone through KVC.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_sort_support.m -o .build/probe-foundation_sort_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_sort.m -o .build/probe-foundation_sort.o
	$(MUSL64_OBJC) .build/probe-foundation_sort_support.o .build/probe-foundation_sort.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_sort"
	# foundation_predicate: F11a acceptance. Two units again: the support unit builds a
	# predicate (and counts a block's calls from its own side), so a predicate that crossed a
	# translation unit can only have gone through the object model.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_predicate_support.m -o .build/probe-foundation_predicate_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_predicate.m -o .build/probe-foundation_predicate.o
	$(MUSL64_OBJC) .build/probe-foundation_predicate_support.o .build/probe-foundation_predicate.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_predicate"
	# foundation_codecs: F12 acceptance. Two units, and the SUPPORT unit builds the BYTES — so the
	# codec is exercised on data it did not create.
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_codecs_support.m -o .build/probe-foundation_codecs_support.o
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_codecs.m -o .build/probe-foundation_codecs.o
	$(MUSL64_OBJC) .build/probe-foundation_codecs_support.o .build/probe-foundation_codecs.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_codecs"
	# icu_smoke: F13's acceptance for the ICU bring-up (docs/design/foundation-plan.md §10). The
	# SOURCE is C, because ICU is a C library and this exercises the DATA path rather than the
	# object layer — but the LINK goes through the C++ driver: ICU's libraries are C++ underneath
	# (libicui18n NEEDs libc++.so.1), and musl-clang++64.sh self-bootstraps the C++ runtime's
	# link pieces (-L.build/llvm-cxx/lib -lc++ -lc++abi -lunwind), which the C driver does not add.
	# Measured: with $(MUSL64_CC) the link fails on __cxa_* and std::__1::mutex from libicui18n.
	# Every check asks for an answer that comes from libicudata — three locales' number
	# formatting, a locale's own date pattern, the German vs Swedish collation rules, and the
	# time-zone id set — so a pass cannot come from constants in the probe.
	$(MUSL64_CXX) -I$(ICUPREFIX)/include userland/tests/icu_smoke.c \
		-L$(ICUPREFIX)/lib -licui18n -licuuc -licudata \
		-o "$(ROOTFS64)/System/Shared/tests/icu_smoke"
	# foundation_dateformatter: F13.6 acceptance - the first un-refused DATA family, exercised
	# through FOUNDATION's own API rather than ICU's directly. ONE unit, deliberately: the other
	# Foundation probes are two-unit because their claim is a cross-translation-unit boundary,
	# while this family's claim is data, so the probe includes only <Foundation/Foundation.h>
	# (which also proves the umbrella exports the new headers). It links the Foundation library,
	# which is where ICU is now bound.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_dateformatter.m -o .build/probe-foundation_dateformatter.o
	$(MUSL64_OBJC) .build/probe-foundation_dateformatter.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_dateformatter"
	# foundation_set: F13.8 acceptance - the first family the boundary never justified. ONE unit (the
	# claim is VALUE SEMANTICS, not a cross-TU boundary), only <Foundation/Foundation.h>, linking the
	# Foundation library.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_set.m -o .build/probe-foundation_set.o
	$(MUSL64_OBJC) .build/probe-foundation_set.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_set"
	# foundation_nsvalue: F13.8c acceptance. ONE unit, only <Foundation/Foundation.h>. Named nsvalue
	# and NOT value, because foundation_value is F2/F8's probe for NSNumber/NSData/NSDate.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_nsvalue.m -o .build/probe-foundation_nsvalue.o
	$(MUSL64_OBJC) .build/probe-foundation_nsvalue.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_nsvalue"
	# foundation_orderedset: F13.8e acceptance. ONE unit, only <Foundation/Foundation.h>. A separate
	# probe from foundation_set because NSOrderedSet is NOT an NSSet subclass: order is its value.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_orderedset.m -o .build/probe-foundation_orderedset.o
	$(MUSL64_OBJC) .build/probe-foundation_orderedset.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_orderedset"
	# foundation_formatters: W11 acceptance. ONE unit, only <Foundation/Foundation.h> (which also
	# proves the umbrella exports all six new formatter headers). The claim is DATA coming back
	# through Foundation's own API — CLDR list/interval/relative patterns and the ISO 8601 grammar —
	# plus NSByteCountFormatter's arithmetic, which is the ONE thing in this family that is ours.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_formatters.m -o .build/probe-foundation_formatters.o
	$(MUSL64_OBJC) .build/probe-foundation_formatters.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_formatters"
	# foundation_kvo: F13.9 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_kvo.m -o .build/probe-foundation_kvo.o
	$(MUSL64_OBJC) .build/probe-foundation_kvo.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_kvo"
	# foundation_expression: F13.10 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_expression.m -o .build/probe-foundation_expression.o
	$(MUSL64_OBJC) .build/probe-foundation_expression.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_expression"
	# foundation_coder: F13.12 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_coder.m -o .build/probe-foundation_coder.o
	$(MUSL64_OBJC) .build/probe-foundation_coder.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_coder"
	# foundation_pointers: W13a acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_pointers.m -o .build/probe-foundation_pointers.o
	$(MUSL64_OBJC) .build/probe-foundation_pointers.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_pointers"
	# foundation_processinfo: F13.13 acceptance. ONE unit, only <Foundation/Foundation.h> plus
	# <unistd.h> for the getpid cross-check.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_processinfo.m -o .build/probe-foundation_processinfo.o
	$(MUSL64_OBJC) .build/probe-foundation_processinfo.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_processinfo"
	# foundation_filemanager: F13.14 acceptance. ONE unit, only <Foundation/Foundation.h> plus
	# <unistd.h> for the symlink(2) its link check makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filemanager.m -o .build/probe-foundation_filemanager.o
	$(MUSL64_OBJC) .build/probe-foundation_filemanager.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filemanager"
	# foundation_directoryenumerator: W8 slice 1 acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus <unistd.h> for the symlink(2) its fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_directoryenumerator.m -o .build/probe-foundation_directoryenumerator.o
	$(MUSL64_OBJC) .build/probe-foundation_directoryenumerator.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_directoryenumerator"
	# foundation_filemanagerdelegate: W8 slice 2 acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus <unistd.h> for the symlink(2) and link(2) its fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filemanagerdelegate.m -o .build/probe-foundation_filemanagerdelegate.o
	$(MUSL64_OBJC) .build/probe-foundation_filemanagerdelegate.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filemanagerdelegate"
	# foundation_filewrapper: W8 slice 4 acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filewrapper.m -o .build/probe-foundation_filewrapper.o
	$(MUSL64_OBJC) .build/probe-foundation_filewrapper.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filewrapper"
	# foundation_url_ownership: W8p slice 6h (foundation-plan.md §60). NSURL's part ownership, with a
	# deliberate reuse pile so an UNOWNED part is visibly wrong and an over-released one crashes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_url_ownership.m -o .build/probe-foundation_url_ownership.o
	$(MUSL64_OBJC) .build/probe-foundation_url_ownership.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_url_ownership"
	# foundation_substratekeys: W8p slice 6g (foundation-plan.md §60). The MEASUREMENT INSTRUMENT for the
	# eighteen keys that need a substrate fact: it prints what the file system does.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_substratekeys.m -o .build/probe-foundation_substratekeys.o
	$(MUSL64_OBJC) .build/probe-foundation_substratekeys.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_substratekeys"
	# foundation_keymasses: W8p slice 6f acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - it asks about the machine it is on.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_keymasses.m -o .build/probe-foundation_keymasses.o
	$(MUSL64_OBJC) .build/probe-foundation_keymasses.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_keymasses"
	# foundation_mountedvolumes: W8p slice 6e acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - this system publishes its mounts in /proc/mounts.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_mountedvolumes.m -o .build/probe-foundation_mountedvolumes.o
	$(MUSL64_OBJC) .build/probe-foundation_mountedvolumes.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_mountedvolumes"
	# foundation_urlresourcevalues: W8 slice 6a acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlresourcevalues.m -o .build/probe-foundation_urlresourcevalues.o
	$(MUSL64_OBJC) .build/probe-foundation_urlresourcevalues.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlresourcevalues"
	# foundation_xmldtdparse: XML slice XML-e acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - a subset is a string, and the file that is never opened is named.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_xmldtdparse.m -o .build/probe-foundation_xmldtdparse.o
	$(MUSL64_OBJC) .build/probe-foundation_xmldtdparse.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_xmldtdparse"
	# foundation_xmldtd: XML slice XML-d acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its file fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_xmldtd.m -o .build/probe-foundation_xmldtd.o
	$(MUSL64_OBJC) .build/probe-foundation_xmldtd.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_xmldtd"
	# foundation_xmldocument: XML slice XML-c acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its file fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_xmldocument.m -o .build/probe-foundation_xmldocument.o
	$(MUSL64_OBJC) .build/probe-foundation_xmldocument.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_xmldocument"
	# foundation_xmltree: XML slice XML-b acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - a tree is built by hand, so there is no fixture.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_xmltree.m -o .build/probe-foundation_xmltree.o
	$(MUSL64_OBJC) .build/probe-foundation_xmltree.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_xmltree"
	# foundation_xmlparser: W8 slice XML-a acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its file fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_xmlparser.m -o .build/probe-foundation_xmlparser.o
	$(MUSL64_OBJC) .build/probe-foundation_xmlparser.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_xmlparser"
	# foundation_fileproviderservice: W8 slice 9 acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - no fixture and no tree.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_fileproviderservice.m -o .build/probe-foundation_fileproviderservice.o
	$(MUSL64_OBJC) .build/probe-foundation_fileproviderservice.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_fileproviderservice"
	# foundation_fileversion: W8 slice 8a acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_fileversion.m -o .build/probe-foundation_fileversion.o
	$(MUSL64_OBJC) .build/probe-foundation_fileversion.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_fileversion"
	# foundation_filepresenter: W8 slice 7c acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its fixture and its deferred presenter need.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filepresenter.m -o .build/probe-foundation_filepresenter.o
	$(MUSL64_OBJC) .build/probe-foundation_filepresenter.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filepresenter"
	# foundation_filecoordinator: W8 slice 7b acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> plus the POSIX calls its fixture makes.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filecoordinator.m -o .build/probe-foundation_filecoordinator.o
	$(MUSL64_OBJC) .build/probe-foundation_filecoordinator.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filecoordinator"
	# foundation_fileaccessintent: W8 slice 7a acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - the vocabulary needs no fixture and touches no file system.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_fileaccessintent.m -o .build/probe-foundation_fileaccessintent.o
	$(MUSL64_OBJC) .build/probe-foundation_fileaccessintent.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_fileaccessintent"
	# foundation_filesecurity: W8 slice 5 acceptance (foundation-plan.md §60). ONE unit, only
	# <Foundation/Foundation.h> - and it asserts the ABSENCE of the bridged accessors, so the D13
	# boundary is machine-checked rather than merely written down.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filesecurity.m -o .build/probe-foundation_filesecurity.o
	$(MUSL64_OBJC) .build/probe-foundation_filesecurity.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filesecurity"
	# foundation_urlcomponents: F13.15 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlcomponents.m -o .build/probe-foundation_urlcomponents.o
	$(MUSL64_OBJC) .build/probe-foundation_urlcomponents.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlcomponents"
	# foundation_decimalnumber: W3b acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_decimalnumber.m -o .build/probe-foundation_decimalnumber.o
	$(MUSL64_OBJC) .build/probe-foundation_decimalnumber.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_decimalnumber"
	# foundation_decimal: W3 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_decimal.m -o .build/probe-foundation_decimal.o
	$(MUSL64_OBJC) .build/probe-foundation_decimal.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_decimal"
	# foundation_notification: W4 acceptance. ONE unit, only <Foundation/Foundation.h> - which makes
	# it the check that the umbrella carries the family too.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_notification.m -o .build/probe-foundation_notification.o
	$(MUSL64_OBJC) .build/probe-foundation_notification.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_notification"
	# foundation_regex: F13.16 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_regex.m -o .build/probe-foundation_regex.o
	$(MUSL64_OBJC) .build/probe-foundation_regex.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_regex"
	# foundation_thread: F13.17 acceptance. ONE unit, only <Foundation/Foundation.h> plus
	# <sys/time.h> for the elapsed-time measurements.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_thread.m -o .build/probe-foundation_thread.o
	$(MUSL64_OBJC) .build/probe-foundation_thread.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_thread"
	# foundation_runloop: F13.18 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_runloop.m -o .build/probe-foundation_runloop.o
	$(MUSL64_OBJC) .build/probe-foundation_runloop.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_runloop"
	# foundation_operation: F13.19 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_operation.m -o .build/probe-foundation_operation.o
	$(MUSL64_OBJC) .build/probe-foundation_operation.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_operation"
	# foundation_progress: F13.20 acceptance. ONE unit, only <Foundation/Foundation.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_progress.m -o .build/probe-foundation_progress.o
	$(MUSL64_OBJC) .build/probe-foundation_progress.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_progress"
	# foundation_numberformatter: F13.7c acceptance - the second un-refused DATA family, and the
	# same shape as the date one: ONE unit (the claim is data), only <Foundation/Foundation.h>, and
	# it links the Foundation library, where ICU is bound.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_numberformatter.m -o .build/probe-foundation_numberformatter.o
	$(MUSL64_OBJC) .build/probe-foundation_numberformatter.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_numberformatter"
	# foundation_defaults: W5 acceptance. ONE unit, only <Foundation/Foundation.h> plus
	# <pwd.h>/<unistd.h>/<stdlib.h> for the scratch root and the user name. IT IS LAUNCHED WITH
	# `-ProbeArgument from-argv` by its case (tests/cases/foundation_defaults.py): the argument domain is
	# the one part of the search list that is a fact about the LAUNCHING process, so a check that could not
	# see the launcher's arguments would be asserting nothing.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_defaults.m -o .build/probe-foundation_defaults.o
	$(MUSL64_OBJC) .build/probe-foundation_defaults.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_defaults"
	# foundation_port: W6b acceptance. ONE unit, only <Foundation/Foundation.h> plus the socket headers -
	# a port IS a socket and the probe asks the KERNEL what it bound (getsockname/accept/fcntl), so this
	# one needs <sys/socket.h> and friends where the other Foundation probes need <unistd.h>.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_port.m -o .build/probe-foundation_port.o
	$(MUSL64_OBJC) .build/probe-foundation_port.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_port"
	# foundation_filehandle: W6c acceptance. ONE unit, only <Foundation/Foundation.h> plus the POSIX
	# headers - a file handle wraps a DESCRIPTOR and the probe asks the kernel what became of it (fcntl(2)
	# for the two ownership rules, a zero-byte read for end of file).
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_filehandle.m -o .build/probe-foundation_filehandle.o
	$(MUSL64_OBJC) .build/probe-foundation_filehandle.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_filehandle"
	# foundation_task: W6d acceptance. ONE unit - and the child it launches is ITSELF (argv[0] is the
	# staged path), so the child's exit code, output and death signal are the probe's own choices.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_task.m -o .build/probe-foundation_task.o
	$(MUSL64_OBJC) .build/probe-foundation_task.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_task"
	# foundation_stream: W6's streams half, the NSStream HEAD's acceptance. ONE unit, and it builds a
	# SUBSTREAM - the head's value contract (ours, under D2) and its run-loop seam are what a substream
	# inherits, so both are asserted through a real descriptor and a real delegate.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_stream.m -o .build/probe-foundation_stream.o
	$(MUSL64_OBJC) .build/probe-foundation_stream.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_stream"
	# foundation_urlrequest: W7 slice 1's acceptance - the REQUEST/RESPONSE VALUE TYPES. ONE unit, only
	# <Foundation/Foundation.h>, and no socket anywhere: a request is a DESCRIPTION of an exchange and a
	# response is its answer's metadata, so the probe is a value probe (an inventory, the mutability
	# boundary, and RFC 9110's own status phrases).
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlrequest.m -o .build/probe-foundation_urlrequest.o
	$(MUSL64_OBJC) .build/probe-foundation_urlrequest.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlrequest"
	# foundation_urlprotocol: W7 slice 2a's acceptance - THE SEAM AND THE CACHED VALUE. ONE unit, only
	# <Foundation/Foundation.h>, and NO transport anywhere: what is asserted is the plug-in point (the
	# base's documented defaults, the registration order, the request-property table's identity rule) and
	# the cached answer's value contract. The probe DEFINES its own NSURLProtocol subclass, which is the
	# only way to exercise override points that exist to be overridden.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlprotocol.m -o .build/probe-foundation_urlprotocol.o
	$(MUSL64_OBJC) .build/probe-foundation_urlprotocol.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlprotocol"
	# foundation_urlprotocol_curl: W7 slice 2c's FIRST HALF - THE BRIDGE. ONE unit, only
	# <Foundation/Foundation.h>, and no session anywhere: FNCURLURLProtocol is an ordinary
	# NSURLProtocol subclass, so it can be started by hand with a client and watched, which is why
	# the bridge is testable BEFORE the session that will normally drive it. It fetches a file:// URL
	# (deterministic, needs no server) and a missing one (the failure path), and the library it links
	# now carries libcurl itself.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlprotocol_curl.m -o .build/probe-foundation_urlprotocol_curl.o
	$(MUSL64_OBJC) .build/probe-foundation_urlprotocol_curl.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlprotocol_curl"
	# foundation_urlsession_config: W7 slice 2c's SESSION half, first row - the configuration a session is
	# built from. ONE unit, only <Foundation/Foundation.h>, and NO SESSION IS CREATED: it is a value with
	# documented defaults, so the probe is a value probe, the shape slice 1 used.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlsession_config.m -o .build/probe-foundation_urlsession_config.o
	$(MUSL64_OBJC) .build/probe-foundation_urlsession_config.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlsession_config"
	# foundation_urlsession: W7 slice 2c's session half, row 2 - THE SESSION AND THE TASK MODEL. ONE unit,
	# only <Foundation/Foundation.h>, and NOTHING TRANSFERS: identity, the configuration snapshot, the task
	# state machine and the enumeration, which is what the row after this one will make do something.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		-Werror=nullable-to-nonnull-conversion \
		userland/tests/foundation_urlsession.m -o .build/probe-foundation_urlsession.o
	$(MUSL64_OBJC) .build/probe-foundation_urlsession.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlsession"
	# foundation_taskmetrics: the metrics records (W7 slice 6)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_taskmetrics.m -o .build/probe-foundation_taskmetrics.o
	$(MUSL64_OBJC) .build/probe-foundation_taskmetrics.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_taskmetrics"
	# foundation_cachehooks: the bridge's cache hooks, proved by a contact count
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_cachehooks.m -o .build/probe-foundation_cachehooks.o
	$(MUSL64_OBJC) .build/probe-foundation_cachehooks.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib \
		-Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl \
		-L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_cachehooks"
	# foundation_urlcache: the cache and its policy (W7 slice 5)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_urlcache.m -o .build/probe-foundation_urlcache.o
	$(MUSL64_OBJC) .build/probe-foundation_urlcache.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlcache"
	# foundation_authloop: the 401 path end to end, probe as its own server
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_authloop.m -o .build/probe-foundation_authloop.o
	$(MUSL64_OBJC) .build/probe-foundation_authloop.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib \
		-Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl \
		-L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_authloop"
	# foundation_redirect: following a redirect, decided by the delegate, bounded by the hop limit (§54).
	# Its own HTTP server again (a 302, a 307, a decline and a never-ending chain), and it reads §52's record
	# off the delegate, so it links the bridge and curl exactly as the two units beside it do.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_redirect.m -o .build/probe-foundation_redirect.o
	$(MUSL64_OBJC) .build/probe-foundation_redirect.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib \
		-Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl \
		-L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_redirect"
	# foundation_metricsdelivery: what a transfer cost, delivered to the delegate before the ending (§52).
	# The probe is its own HTTP server (the authloop unit's pattern) and reads the PUBLIC record the
	# delegate was handed, so it links the bridge and curl exactly as the authloop probe does.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_metricsdelivery.m -o .build/probe-foundation_metricsdelivery.o
	$(MUSL64_OBJC) .build/probe-foundation_metricsdelivery.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib \
		-Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl \
		-L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_metricsdelivery"
	# foundation_streamtask: the duplex connection as a task (§58) - the minimum, the cap, the
	# timeout-as-a-cancel, the half-close, the refused door, AND the TLS tunnel (§58.1). Its own server for the
	# first eight legs, and its own LIBTLS PEER for the ninth: `openssl s_server` was tried FIRST and stalled
	# the handshake in this guest (the same stall the libressl units' `s_client` shows), so the tunnel's far
	# end is the substrate the tree has already proven there. That peer is why this probe needs -ltls.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests -I$(LIBRESSL_PREFIX)/include \
		userland/tests/foundation_streamtask.m -o .build/probe-foundation_streamtask.o
	$(MUSL64_OBJC) .build/probe-foundation_streamtask.o \
		-L$(FNXLIB) -lfoundation \
		-L$(LIBRESSL_PREFIX)/lib -ltls -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_streamtask"
	# foundation_urlerror: the URL error names, their values, and the shape of the family (§56). No transport
	# and no server: two of its checks read a REAL task's error and the rest are the codes themselves.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_urlerror.m -o .build/probe-foundation_urlerror.o
	$(MUSL64_OBJC) .build/probe-foundation_urlerror.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlerror"
	# foundation_websocket: the WebSocket VALUES (§59 slice 1) - the message and the two enums. No transport,
	# no server, no framing: this is the slice whose subject is what the values ARE.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_websocket.m -o .build/probe-foundation_websocket.o
	$(MUSL64_OBJC) .build/probe-foundation_websocket.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_websocket"
	# foundation_wsframe: RFC 6455's BYTE LAYER (§59 slice 2) - the frame codec, as pure functions. It links the
	# library's internal FNWebSocketFraming (not public API, and exported like every other symbol in this .so).
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_wsframe.m -o .build/probe-foundation_wsframe.o
	$(MUSL64_OBJC) .build/probe-foundation_wsframe.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_wsframe"
	# foundation_wsassemble: the framing's STATE HALF (§59 slice 2b) - frames in, messages out, including the
	# interleaved control frame of §5.4 that §59 left as a measurement. It links both internal units.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_wsassemble.m -o .build/probe-foundation_wsassemble.o
	$(MUSL64_OBJC) .build/probe-foundation_wsassemble.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_wsassemble"
	# foundation_wshandshake: RFC 6455's OPENING HANDSHAKE (§59 slice 3a) - the request and the reply, with the
	# accept digest tested against the RFC's own worked example. No socket, which is why it is a layer.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_wshandshake.m -o .build/probe-foundation_wshandshake.o
	$(MUSL64_OBJC) .build/probe-foundation_wshandshake.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_wshandshake"
	# foundation_websockettask: THE TASK ITSELF (§59 slice 3b) - the layers meeting the substrate over a real
	# connection to a RAW peer, which speaks the upgrade in plain HTTP and RFC 6455 through the same codec. ITS
	# wss: LEG ADDS A TLS PEER, so this probe needs libtls's header path AND its library, exactly as
	# foundation_streamtask does - the same substrate for the same reason.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests -I$(LIBRESSL_PREFIX)/include \
		userland/tests/foundation_websockettask.m -o .build/probe-foundation_websockettask.o
	$(MUSL64_OBJC) .build/probe-foundation_websockettask.o \
		-L$(FNXLIB) -lfoundation \
		-L$(LIBRESSL_PREFIX)/lib -ltls -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_websockettask"
	# foundation_challengedoor: the two delegate doors, driven (W7 slice 4)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_challengedoor.m -o .build/probe-foundation_challengedoor.o
	$(MUSL64_OBJC) .build/probe-foundation_challengedoor.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_challengedoor"
	# foundation_credentialstorage: the store keyed by protection space (W7 slice 4)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_credentialstorage.m -o .build/probe-foundation_credentialstorage.o
	$(MUSL64_OBJC) .build/probe-foundation_credentialstorage.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_credentialstorage"
	# foundation_authenticationchallenge: the challenge's mechanics (W7 slice 4)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_authenticationchallenge.m -o .build/probe-foundation_authenticationchallenge.o
	$(MUSL64_OBJC) .build/probe-foundation_authenticationchallenge.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_authenticationchallenge"
	# foundation_urlcredential: the credential as a value (W7 slice 4)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_urlcredential.m -o .build/probe-foundation_urlcredential.o
	$(MUSL64_OBJC) .build/probe-foundation_urlcredential.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlcredential"
	# foundation_urlprotectionspace: the realm as a value (W7 slice 4)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_urlprotectionspace.m -o .build/probe-foundation_urlprotectionspace.o
	$(MUSL64_OBJC) .build/probe-foundation_urlprotectionspace.o \
		-L$(FNXLIB) -lfoundation \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlprotectionspace"
	# foundation_httpcookiestorage: the store and its two matching rules (W7 slice 3)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_httpcookiestorage.m -o .build/probe-foundation_httpcookiestorage.o
	$(MUSL64_OBJC) .build/probe-foundation_httpcookiestorage.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib \
		-Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl \
		-L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_httpcookiestorage"
	# foundation_httpcookie: the cookie as a value (W7 slice 3)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_httpcookie.m -o .build/probe-foundation_httpcookie.o
	$(MUSL64_OBJC) .build/probe-foundation_httpcookie.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib \
		-Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl \
		-L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_httpcookie"
	# foundation_urlsession_task: W7 slice 2c row 3 - THE EXECUTION (completion-handler path)
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/foundation_urlsession_task.m -o .build/probe-foundation_urlsession_task.o
	$(MUSL64_OBJC) .build/probe-foundation_urlsession_task.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/foundation_urlsession_task"
	# fn_block_mrc: the MRC TWIN of the crashing call - same door, same __block-object capture
	$(MUSL64_OBJC) -c -fno-objc-arc -Iuserland -Iuserland/tests \
		userland/tests/fn_block_mrc.m -o .build/probe-fn_block_mrc.o
	$(MUSL64_OBJC) .build/probe-fn_block_mrc.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/fn_block_mrc"
	# fn_receiver_probe: the receiver ALONE - three rounds could not tell 'never starts' from
	# 'something before the check blocks', and a probe that does one thing can.
	$(MUSL64_OBJC) -c -fobjc-arc -Iuserland -Iuserland/tests \
		userland/tests/fn_receiver_probe.m -o .build/probe-fn_receiver_probe.o
	$(MUSL64_OBJC) .build/probe-fn_receiver_probe.o \
		-L$(FNXLIB) -lfoundation -Wl,-rpath-link,$(CURL_PREFIX)/lib -Wl,-rpath-link,$(LIBRESSL_PREFIX)/lib -L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/fn_receiver_probe"
	# curl_smoke: W7 slice 2b's acceptance - LIBCURL ON THE GUEST. NOT a Foundation probe: this is a
	# third-party library's landing, so the program that judges it has no Foundation in it (the
	# kernel_pipe_dup2 reasoning). It compiles against the VENDORED libcurl out of .build/curl-prefix
	# and needs NO RPATH: the guest loader resolves libcurl.so.4 out of /System/Libraries, which is
	# where the staging block above puts it.
	# -lcurl -lssl -lcrypto IN THAT ORDER: libcurl NEEDs LibreSSL's entry points now that it is bound to
	# it (L2), so leaving them out fails the link with "undefined reference to X509_check_issued" — the
	# same shape as the libtls link line below.
	$(MUSL64_CC) -I$(CURL_PREFIX)/include userland/tests/curl_smoke.c \
		-L$(CURL_PREFIX)/lib -lcurl -L$(LIBRESSL_PREFIX)/lib -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/curl_smoke"
	# kernel_threaded_exec: THE KERNEL BUG'S REPRODUCER (§45 of the Foundation plan), not a Foundation
	# probe - it is plain C with NO Foundation in it, because the point is that the library is absent from
	# the failing program. Mode 1's children are /System/Tools/true; mode 2's are the Foundation probe.
	$(MUSL64_CC) -O2 userland/tests/kernel_threaded_exec.c \
		-o "$(ROOTFS64)/System/Shared/tests/kernel_threaded_exec"
	# kernel_pipe_dup2: the MINIMAL reproducer for the pipe/fork/dup2 wedge (§45) - plain POSIX, so a hang
	# here is a kernel defect in a file nobody can argue with.
	$(MUSL64_CC) -O2 userland/tests/kernel_pipe_dup2.c \
		-o "$(ROOTFS64)/System/Shared/tests/kernel_pipe_dup2"
	# kernel_loopback_tcp: CAN THIS KERNEL'S LOOPBACK CARRY A PAYLOAD? - plain C with NO Foundation,
	# NO SSL and no third-party library, because the question came from a stalled TLS handshake and
	# the answer must not be able to be about the TLS library (docs/design/libressl-plan.md L1).
	$(MUSL64_CC) -O2 userland/tests/kernel_loopback_tcp.c \
		-o "$(ROOTFS64)/System/Shared/tests/kernel_loopback_tcp"
	# kernel_pty_read: ARE A PTY'S TWO READ PATHS WOKEN? - plain C with no Foundation. The MASTER reads
	# through pty_read and the SLAVE through tty_read (two different fsops), and tty_read's VMIN/VTIME arms
	# are reachable ONLY from a tty the probe owns - which is exactly why a pty is used here and the console
	# is not. It is a BEHAVIOUR test first: two §58.1 cures went in unverifiable because nothing could reach
	# them (docs/design/foundation-plan.md §58.1b).
	$(MUSL64_CC) -O2 userland/tests/kernel_pty_read.c \
		-o "$(ROOTFS64)/System/Shared/tests/kernel_pty_read"
	# (The toolkit probes — layout_solve, view_layout, stack_view, scroll_view,
	# collection_view, tab_view, split_view, grid_view, kvc_basic,
	# notification_basic, cell_basic, viewcontroller_basic, window_draw,
	# control_click, text_stack — and the Widget Zoo were removed with the
	# toolkit, 2026-09-17: docs/design/argentum-uikit-plan.md, DEFERRED.)
	# font_twice: TEMPORARY DIAGNOSTIC - asks the guest's own FreeType whether
	# it can open one font file twice (the toolkit keeps a face per size, so a
	# second size is a second FT_New_Face). No toolkit, no fontconfig.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-I$(X11PREFIX)/include/freetype2 -L$(X11PREFIX)/lib \
		userland/tests/font_twice.cpp -lfreetype \
		-o "$(ROOTFS64)/System/Shared/tests/font_twice"
	# plist_test: the C plist CORE's acceptance (include/plist.h + userland/plist.c)
	# — the shared core libconfig will consume from C. Compiled from the same
	# source the library builds, rather than linked out of libfoundation: it is a C
	# probe of a C core, so there is one implementation either way and no Objective-C
	# runtime in the path.
	$(MUSL64_CC) -Iinclude userland/tests/plist_test.c userland/plist.c -lm \
		-o "$(ROOTFS64)/System/Shared/tests/plist_test"
	# config_plist_test: P3b's acceptance (docs/design/plist-config-plan.md) — a
	# .conf reads the SAME in both spellings. It copies each shipped file into a
	# scratch tree, forces a rewrite (the writer emits plists now), and compares
	# every key, every value and the prose, both ways. Linked from the same
	# sources the library builds, like plist_test: one implementation either way.
	$(MUSL64_CC) -Iinclude -Iuserland userland/tests/config_plist_test.c \
		userland/libconfig.c userland/libconfig_plist.c userland/plist.c \
		-o "$(ROOTFS64)/System/Shared/tests/config_plist_test"
	# x_move: raw-Xlib window mover — attributes a window-move wedge
	# between the server (Xfb) and any client, with no toolkit involved.
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/x_move.cpp -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/x_move"
	# x_keys: raw-Xlib key reader — proves a typed key reaches the guest's X
	# server with no toolkit in the path (the keyboard's x_move).
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		-L$(CURDIR)/$(FNXLIB) -L$(X11PREFIX)/lib \
		userland/tests/x_keys.cpp -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/x_keys"
	# oom_probe: S4.3d (eats memory until a page cannot be faulted in,
	# to prove the fault path reports it and sends SIGBUS)
	$(MUSL64_CXX) -Iuserland -I$(X11PREFIX)/include \
		userland/tests/oom_probe.cpp \
		-L$(X11PREFIX)/lib -L$(FNXLIB) -lconfig -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/oom_probe"
	# xclick: generic synthetic-click injector for the S2.4 gates
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xclick.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xclick"
	# xshm_m0: MIT-SHM M0 acceptance — Xlib client (the UIKit transport)
	# paints a window via a SysV segment + XShmPutImage (libXext); logs
	# SHMM0-DONE for the gate.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xshm_m0.c \
		-L$(X11PREFIX)/lib -lXext -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xshm_m0"
	# xshm_geo: the Xfb short-window SHM measurement (S4.2a open item).
	# XShmPutImage into a matrix of geometries - including the toolkit's
	# real one (window 1920x30 with a 25%-larger backing) - reads each
	# window back with XGetImage and prints a verdict per geometry; run
	# from the console shell with DISPLAY=:0 and read the SHMGEO lines.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xshm_geo.c \
		-L$(X11PREFIX)/lib -lXext -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xshm_geo"
	# xwinprobe: the shadow-vs-screen probe (XGetImage over a screen rect,
	# twice) — written for the menubar strip that never repaints; useful
	# for any "the draw landed but the screen never changed" question.
	$(MUSL64_CC) -I$(X11PREFIX)/include userland/tests/xwinprobe.c \
		-L$(X11PREFIX)/lib -lX11 \
		-o "$(ROOTFS64)/System/Shared/tests/xwinprobe"
	# xcomp_probe: de-risking a compositing Kestrel — does Xfb's Composite
	# redirect + hand out a usable pixmap, and what does a screen-sized
	# frame cost. Linked against the xcb bindings deliberately: the Xlib
	# wrappers (libXcomposite/libXdamage/libXrender) are not built here.
	$(MUSL64_CC) -I$(X11PREFIX)/include \
		-I$(X11PREFIX)/include/pixman-1 \
		userland/tests/xcomp_probe.c \
		-L$(X11PREFIX)/lib -lX11 -lX11-xcb -lxcb -lxcb-composite \
		-lpixman-1 -lXrender \
		-o "$(ROOTFS64)/System/Shared/tests/xcomp_probe"
	# xbtn: X11 mouse-leg regression client (window + pointer poll +
	# button print) — the S0.6 mouse gate drives QEMU monitor mouse at
	# it and expects "XBTN: button 1 press/release".
	$(MUSL64_CC) -I$(X11PREFIX)/include -I$(X11PREFIX)/include/X11 \
		-L$(X11PREFIX)/lib \
		userland/tests/xbtn.c -lX11 -o "$(ROOTFS64)/System/Shared/tests/xbtn"
	$(MUSL64_CC) userland/tools/acl.c -o "$(ROOTFS64)/System/Tools/acl"
	$(MUSL64_CC) -Iinclude -Iuserland userland/tools/config.c -L$(CURDIR)/$(FNXLIB) \
		-lconfig -o "$(ROOTFS64)/System/Tools/config"
	$(MUSL64_CC) -Iinclude tools/shm_leak_test.c -o "$(ROOTFS64)/System/Tools/shm_leak_test"
	$(MUSL64_CC) -Iinclude tools/shm_cap_test.c -o "$(ROOTFS64)/System/Tools/shm_cap_test"
	$(MUSL64_CC) tools/config_m3_test.c -o "$(ROOTFS64)/System/Tools/config_m3_test"
	$(MUSL64_CC) userland/tools/pty_test.c -o "$(ROOTFS64)/System/Tools/pty_test"
	# Probes the test harness drives (tests/cases/*): the security battery from
	# the audit rounds, the AGFS metadata/stream probes and the mmap probe.
	# They are committed sources; nothing built them into the image before.
	$(MUSL64_CC) -Iinclude tools/sec_test.c -o "$(ROOTFS64)/System/Shared/tests/sec_test"
	$(MUSL64_CC) userland/tests/test_mmap.c -o "$(ROOTFS64)/System/Shared/tests/test_mmap"
	$(MUSL64_CC) userland/tests/agfsattr.c -o "$(ROOTFS64)/System/Shared/tests/agfsattr"
	$(MUSL64_CC) userland/tests/agfsdir.c -o "$(ROOTFS64)/System/Shared/tests/agfsdir"
	$(MUSL64_CC) userland/tests/agfsxattr.c -o "$(ROOTFS64)/System/Shared/tests/agfsxattr"
	$(MUSL64_CC) userland/tools/agfsquery.c -o "$(ROOTFS64)/System/Tools/agfsquery"
	$(MUSL64_CC) userland/tools/agfsqtest.c -o "$(ROOTFS64)/System/Tools/agfsqtest"
	$(MUSL64_CC) userland/tools/tone.c -o "$(ROOTFS64)/System/Tools/tone" -lm
	$(MUSL64_CC) userland/tools/fbdump.c -o "$(ROOTFS64)/System/Tools/fbdump"
	cp $(DASH64_BIN) "$(ROOTFS64)/System/Tools/sh"
	# --- the static recovery set (docs/shared-libraries-plan.md §2.4/§3):
	# insurance when /System/Libraries is corrupt or missing. The kernel
	# boots these (RECOVERY_PROGRAM) instead of init on a 'recovery'
	# param or a failed NEEDED-closure probe. Static dash + static
	# toybox; the /System/Recovery/bin applet links mirror the dynamic
	# /System/Tools set so repair commands resolve to the static toybox.
	cp $(DASH64_RECOVERY) "$(ROOTFS64)/System/Tools/recovery-sh"
	cp $(TOYBOX64_RECOVERY) "$(ROOTFS64)/System/Tools/recovery-toybox"
	@chmod 0755 "$(ROOTFS64)/System/Tools/recovery-sh" \
		"$(ROOTFS64)/System/Tools/recovery-toybox"
	@mkdir -p "$(ROOTFS64)/System/Recovery/bin"
	@for l in $(CURDIR)/$(ROOTFS64)/System/Tools/*; do \
		if [ -L "$$l" ] && [ "$$(readlink "$$l")" = toybox ]; then \
			ln -sfn /System/Tools/recovery-toybox \
				"$(ROOTFS64)/System/Recovery/bin/$$(basename "$$l")"; \
		fi; \
	done
	# third-party X11 lives under System/Shared/X11 (outside the
	# zero-allow System/Tools lint scope); System/Tools stays first-party
	# + ported toybox only.
	@mkdir -p "$(ROOTFS64)/System/Shared/X11/bin" "$(ROOTFS64)/System/Shared/tests"
	cp $(XFB_BIN) "$(ROOTFS64)/System/Shared/X11/bin/Xfb"
	cp .build/x11-prefix/bin/xkbcomp "$(ROOTFS64)/System/Shared/X11/bin/xkbcomp"
	# xkb data for the runtime XKB compile (Xfb's libxkbfile default is
	# System/Shared/X11/xkb). Host xkeyboard-config is the established
	# source (bundled data mismatches the server's xkbcomp).
	@if [ -d /usr/share/X11/xkb/rules ]; then \
		cp -r /usr/share/X11/xkb/. "$(ROOTFS64)/System/Shared/X11/xkb/"; \
	else \
		echo "WARNING: /usr/share/X11/xkb missing - Xfb keyboard init will fail"; \
	fi
	@cp userland/tests/test_toybox.sh "$(ROOTFS64)/System/Shared/tests/test_toybox.sh" 2>/dev/null || \
		{ mkdir -p "$(ROOTFS64)/System/Shared/tests" && \
		  cp userland/tests/test_toybox.sh "$(ROOTFS64)/System/Shared/tests/test_toybox.sh"; }
	@chmod +x "$(ROOTFS64)/System/Tools/sh" "$(ROOTFS64)/System/Tools/init"
	@mkdir -p "$(ROOTFS64)/System/Shared/scripts/dhcp" && \
		cp userland/scripts/dhcp_script.sh "$(ROOTFS64)/System/Shared/scripts/dhcp/default.script" 2>/dev/null || true
	# --- machine configuration (System/Configuration; Q9 accounts) ---
	# Identity, name resolution and machine identity are record domains
	# shipped in System scope (docs/system-config-files-plan.md M2/M5).
	# No legacy colon/line files exist (passwd, group, shells, hosts).
	@cp userland/configuration/system.passwd.conf "$(ROOTFS64)/System/Configuration/system.passwd.conf"
	@cp userland/configuration/system.group.conf "$(ROOTFS64)/System/Configuration/system.group.conf"
	@cp userland/configuration/system.shells.conf "$(ROOTFS64)/System/Configuration/system.shells.conf"
	@cp userland/configuration/system.hosts.conf "$(ROOTFS64)/System/Configuration/system.hosts.conf"
	@cp userland/configuration/system.network.conf "$(ROOTFS64)/System/Configuration/system.network.conf"
	@cp userland/configuration/system.mounts.conf "$(ROOTFS64)/System/Configuration/system.mounts.conf"
	# --- FSH fonts (text stack): OS fonts in /System/Shared/Fonts; the
	# fontconfig config is the libconfig domain system.fonts.conf (M0,
	# docs/design/fontconfig-config-plan.md) - no XML fonts.conf ships.
	# --- the interim cursor theme (userland/cursors): an Xcursor theme is
	# <dir>/<theme>/cursors/<name>, which is the layout libXcursor searches
	# under XCURSOR_PATH. Kestrel points XCURSOR_PATH at this and defines
	# the cursor on the root window.
	# Idempotent ON PURPOSE: `cp -a src dst` copies src INTO dst when dst
	# is already a directory, so a second make run nested a whole duplicate
	# theme at cursors/cursors/ (~11.7MB, 146 entries) and pushed the tree
	# past the 64MB image. Clear the destination first.
	@mkdir -p "$(ROOTFS64)/System/Shared/Icons/default"
	@rm -rf "$(ROOTFS64)/System/Shared/Icons/default/cursors"
	@cp -a userland/cursors \
		"$(ROOTFS64)/System/Shared/Icons/default/cursors"
	@mkdir -p "$(ROOTFS64)/System/Shared/Fonts"
	@cp userland/fonts/DejaVuSans.ttf userland/fonts/DejaVuSans-Bold.ttf \
		"$(ROOTFS64)/System/Shared/Fonts/"
	@cp userland/configuration/system.fonts.conf \
		"$(ROOTFS64)/System/Configuration/system.fonts.conf"
	# --- shared libc (docs/shared-libraries-plan.md): stage the dynamic
	# linker + libc for the dynamic userland. The interpreter is a
	# hardlink of libc.so (same inode), matching musl's own install, so
	# the loader recognizes libc as itself.
	@cp $(MUSL64_PREFIX)/lib/libc.so "$(ROOTFS64)/System/Libraries/libc.so"
	@ln -f "$(ROOTFS64)/System/Libraries/libc.so" "$(ROOTFS64)/System/Libraries/ld-musl-x86_64.so.1"
	# --- shared X stack (docs/shared-libraries-plan.md §6, M2): the
	# versioned .so files of the X11 dependency prefix. musl's loader
	# resolves each NEEDED soname (libX11.so.6, libxcb.so.1, ...) as an
	# exact filename against the baked /System/Libraries search path; the
	# glob carries the soname symlink + the versioned real file (the bare
	# dev symlink libX11.so is link-time only and skipped). Only the libs
	# today's consumers NEED are staged. libX11-xcb + libxcb-composite are
	# now staged because something DOES link them: xcomp_probe, which
	# de-risks a compositing Kestrel. libXrender is vendored too (the
	# RENDER client the compositor blends through); libXcomposite and
	# libXdamage are still not - the xcb bindings cover Composite.
	@if [ ! -d "$(X11PREFIX)/lib" ]; then \
		echo "X11 prefix missing - run tools/x11-shared-build.sh first"; \
		exit 1; \
	fi
	@for l in libX11.so libxcb.so libXau.so libXdmcp.so libxkbfile.so \
		libpixman-1.so libXfont2.so libfontenc.so libz.so libXext.so \
		libX11-xcb.so libxcb-composite.so libXrender.so \
		libXcursor.so libXfixes.so libXcomposite.so; do \
		cp -a $(X11PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# --- ICU4C (docs/design/foundation-plan.md §10, slice F13): the DATA backend
	# the Foundation's data-driven families bind - the formatters, the time-zone
	# names and DST rules, the non-Gregorian calendars, collation and the [d]
	# fold. Three libraries plus their sonames: the common library, the i18n
	# library, and the DATA PACKAGE, which --with-data-packaging=library makes a
	# real shared library (the Debian arrangement). That is why the guest needs no
	# data path and no ICU_DATA: musl's loader resolves libicudata.so.76 as an
	# ordinary NEEDED entry, exactly like libz. ICU's own NEEDED closure is the
	# C++ stack (libc++/libc++abi/libunwind/libc), staged above.
	@if [ ! -d "$(ICUPREFIX)/lib" ]; then \
		echo "ICU prefix missing - run tools/icu-build.sh first"; \
		exit 1; \
	fi
	@for l in libicuuc.so libicui18n.so libicudata.so; do \
		cp -a $(ICUPREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# FNX's own shared libconfig (first-party, .build/fnxlib): the
	# config tool, toybox account tools and Xfb's configargs all NEEDED it.
	@cp $(FNXLIB_CONFIG) "$(ROOTFS64)/System/Libraries/libconfig.so.1"
	# The Objective-C runtime (docs/design/objc-toolchain-plan.md P3): same
	# rule as libconfig and the C++ stack - the versioned file, whose SONAME
	# ("libobjc.so.4.6") the guest loader resolves. Upstream tries to suppress
	# the soname with a malformed set_property() call, so it has one.
	@cp $(OBJC_PREFIX)/lib/libobjc.so.4.6 "$(ROOTFS64)/System/Libraries/libobjc.so.4.6"
	# The Foundation (docs/design/foundation-plan.md F0): the same staging rule.
	@cp $(FOUNDATION_LIB) "$(ROOTFS64)/System/Libraries/libfoundation.so.1"
	# Its PUBLIC HEADERS, which is what makes an on-guest Objective-C rebuild
	# possible - the gap docs/design/self-hosting-packages.md §6 records for the
	# runtime. Lower-case directory on purpose: <Foundation/...>, never Apple's.
	# THE GUEST'S OWN COPY IS `Headers/Foundation` NOW: with the library's directory renamed, the
	# import spelling `<Foundation/...>` must name the STAGED tree too, or a guest build would be
	# told our own headers do not exist (user's cleanup, 2026-09-20).
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/Foundation"
	@cp $(FOUNDATION_SRC)/*.h "$(ROOTFS64)/System/Shared/Headers/Foundation/"

	# --- CoreGraphics (docs/design/coregraphics-plan.md C1-C3): the drawing library, staged
	# by the SAME rule as Foundation - the versioned file, whose SONAME
	# ("libcoregraphics.so.1") is what the guest loader resolves - AND ITS PUBLIC HEADERS
	# under the directory the import spelling names, so that a guest build saying
	# <CoreGraphics/CGPath.h> finds OUR headers. THIS IS THE HALF THAT WAS MISSING UNTIL NOW:
	# the library built and was in no image, so nothing on the guest could link it.
	@cp $(CG_LIB) "$(ROOTFS64)/System/Libraries/libcoregraphics.so.1"
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/CoreGraphics"
	@cp userland/CoreGraphics/*.h "$(ROOTFS64)/System/Shared/Headers/CoreGraphics/"
	# --- The AppKit (docs/design/coregraphics-plan.md C8): the bridge library and its headers,
	# staged the same way CoreGraphics is, into the directory the `<AppKit/…>` import spelling names.
	# `$(APPKIT_LIB)` IS A PREREQUISITE of this target (mk/00-base.mk builds it), for the reason the
	# CoreGraphics staging line records: a rule that copies a file nothing builds works only on a
	# machine where the file happens to be there.
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/AppKit"
	@cp userland/AppKit/*.h "$(ROOTFS64)/System/Shared/Headers/AppKit/"
	@cp $(APPKIT_LIB) "$(ROOTFS64)/System/Libraries/libappkit.so.1"

	# --- shared C++ stack (dynamic-C++): the versioned libc++/libc++abi/
	# libunwind .so files from the llvm-cxx prefix (built shared since the
	# dynamic-C++ milestone). Same staging rule as the X stack: the glob
	# carries the soname symlink + the versioned real file; the bare dev
	# symlink is link-time only and skipped. cpp_smoke NEEDs these sonames at runtime.
	@if [ ! -d "$(LLVM_CXX_PREFIX)/lib" ]; then \
		echo "llvm-cxx prefix missing - run make llvm-cxx first"; \
		exit 1; \
	fi
	@for l in libc++.so libc++abi.so libunwind.so; do \
		cp -a $(LLVM_CXX_PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# --- shared text stack (the X11/font port, tools/x11-shared-build.sh): fontconfig +
	# HarfBuzz + FreeType + the libpng/expat leaves, built SHARED into the
	# X prefix. Only what a consumer NEEDs today is staged (libharfbuzz-
	# subset/gobject are left out until something links them); the glob
	# carries soname + real file (libpng16.so.* covers libpng16.so.16,
	# not the bare libpng.so dev link).
	@if [ ! -d "$(X11PREFIX)/lib" ]; then \
		echo "X11 prefix missing - run tools/x11-shared-build.sh first"; \
		exit 1; \
	fi
	@for l in libpng16.so libexpat.so libfreetype.so libfontconfig.so \
		libharfbuzz.so; do \
		cp -a $(X11PREFIX)/lib/$${l}.* "$(ROOTFS64)/System/Libraries/"; \
	done
	# --- lcms2 (third_party/lcms2, tag lcms2.19.1, MIT): the colour engine libcoregraphics
	# binds for C4. Same staging rule as the X stack above — the glob carries the soname and
	# the real file, and the bare `liblcms2.so` dev link is link-time only, so it is skipped —
	# and the same GATE: a missing prefix is reported as "run this", rather than being left to
	# surface much later as a link failure in whatever consumes it.
	@if [ ! -d "$(LCMS2_PREFIX)/lib" ]; then \
		echo "lcms2 prefix missing - run tools/lcms2-build.sh first"; \
		exit 1; \
	fi
	@cp -a $(LCMS2_PREFIX)/lib/liblcms2.so.* "$(ROOTFS64)/System/Libraries/"
	# ITS HEADER, for the reason the Foundation headers are staged: an on-guest rebuild of
	# anything that includes <lcms2.h> has to be able to find it.
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/lcms2"
	@cp $(LCMS2_PREFIX)/include/*.h "$(ROOTFS64)/System/Shared/Headers/lcms2/"
	# --- libcurl (third_party/curl, tag curl-8_22_0, curl's own MIT/X-derivative licence): the
	# Foundation's HTTP transport (docs/design/foundation-transport-plan.md, W7 slice 2b). Same
	# staging rule as lcms2 above — the glob carries the soname AND the real file, and the bare
	# `libcurl.so` dev link is link-time only so it is skipped — and the same GATE: a missing
	# prefix names its own fix instead of surfacing later as a link or load failure.
	@if [ ! -d "$(CURL_PREFIX)/lib" ]; then \
		echo "libcurl prefix missing - run tools/curl-build.sh first"; \
		exit 1; \
	fi
	@cp -a $(CURL_PREFIX)/lib/libcurl.so.* "$(ROOTFS64)/System/Libraries/"
	# ITS HEADERS, for the reason the Foundation headers are staged: an on-guest rebuild of anything
	# that includes <curl/curl.h> has to be able to find it.
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/curl"
	@cp $(CURL_PREFIX)/include/curl/*.h "$(ROOTFS64)/System/Shared/Headers/curl/"
	# --- libjpeg-turbo (third_party/libjpeg-turbo, tag 3.2.0, IJG + Modified BSD-3): the JPEG
	# decoder behind CGImageCreateWithJPEGDataProvider (C5.3). Same staging rule as lcms2 and curl
	# above — the glob carries the SONAME and the real file, and the bare `libjpeg.so` dev link is
	# link-time only so it is skipped — and the same GATE: a missing prefix names its own fix rather
	# than surfacing later as a link or load failure.
	#
	# AND THIS ONE IS NOT OPTIONAL THE WAY AN UNUSED LIBRARY WOULD BE. libcoregraphics.so.1 lists
	# `libjpeg.so.62` among its NEEDED entries, so without these bytes EVERY guest program that links
	# the graphics library fails AT LOAD — before its first instruction — rather than at some later
	# JPEG call. That is why the rule exists even though nothing has asked for a JPEG yet.
	@if [ ! -d "$(LIBJPEG_PREFIX)/lib" ]; then \
		echo "libjpeg prefix missing - run tools/libjpeg-build.sh first"; \
		exit 1; \
	fi
	@cp -a $(LIBJPEG_PREFIX)/lib/libjpeg.so.* "$(ROOTFS64)/System/Libraries/"
	# ITS HEADERS, for the reason the others are staged: an on-guest rebuild of anything that
	# includes <jpeglib.h> has to be able to find it — and jpeglib.h is not self-contained, so its
	# three companions (jconfig.h, jmorecfg.h, jerror.h) travel with it, which is what the glob takes.
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/jpeg"
	@cp $(LIBJPEG_PREFIX)/include/*.h "$(ROOTFS64)/System/Shared/Headers/jpeg/"
	# THE CLI TOO (BUILD_CURL_EXE=ON): the L2 trust-store acceptance is a shell script driving an
	# https fetch, and a shell cannot call a library.
	#
	# AND IT GOES IN System/Tools, THE TREE THE FSH LINT GATES — because IT IS PATCHED TO THE FSH rather
	# than excused from it (third_party/curl-fsh.patch, applied by tools/curl-build.sh the way
	# musl-fsh.patch is applied to musl). curl's own null device said `/dev/null` and its help text named
	# `/dev/null` and `/etc/hosts`; this system has neither, so the paths are patched to ours and the tool
	# passes the gate on its merits (measured: 3 gate errors before the patch, 0 after).
	@mkdir -p "$(ROOTFS64)/System/Tools"
	@cp $(CURL_PREFIX)/bin/curl "$(ROOTFS64)/System/Tools/curl"
	# --- LibreSSL (docs/design/libressl-plan.md, pin 4.3.2 via tools/fetch-libressl.sh): THE one
	# system SSL library, and what libcurl binds for https at L2. Same staging rule as lcms2 and
	# libcurl above — the glob carries the soname AND the real file, the bare dev link is skipped —
	# and the same GATE, naming its own fix. THREE libraries, because libtls is the first-party
	# simple API the plan names alongside the OpenSSL-compatible pair, not a second project.
	@if [ ! -d "$(LIBRESSL_PREFIX)/lib" ]; then \
		echo "libressl prefix missing - run tools/fetch-libressl.sh then tools/libressl-build.sh"; \
		exit 1; \
	fi
	@cp -a $(LIBRESSL_PREFIX)/lib/libcrypto.so.* $(LIBRESSL_PREFIX)/lib/libssl.so.* \
		$(LIBRESSL_PREFIX)/lib/libtls.so.* "$(ROOTFS64)/System/Libraries/"
	# THE TOOL, because it is the plan's OWN deliverable and L1's acceptance runs it. /System/Tools
	# is where this tree's binaries live; ocspcheck ships beside openssl(1) as in the pin.
	@mkdir -p "$(ROOTFS64)/System/Tools"
	@cp $(LIBRESSL_PREFIX)/bin/openssl $(LIBRESSL_PREFIX)/bin/ocspcheck "$(ROOTFS64)/System/Tools/"
	# ITS HEADERS, for the reason the Foundation headers are staged: an on-guest rebuild of anything
	# that includes <openssl/ssl.h> or <tls.h> has to be able to find it.
	@mkdir -p "$(ROOTFS64)/System/Shared/Headers/openssl"
	@cp -a $(LIBRESSL_PREFIX)/include/openssl/*.h "$(ROOTFS64)/System/Shared/Headers/openssl/"
	@cp $(LIBRESSL_PREFIX)/include/tls.h "$(ROOTFS64)/System/Shared/Headers/"
	# THE CONFIG, at the path this build compiles in as OPENSSLDIR
	# (`-DOPENSSLDIR=/System/Configuration/SSL`, tools/libressl-build.sh). It is not optional: without
	# it `openssl req` exits 1 with "Unable to load config info" and NO certificate can be made — which
	# is measured in the file's own header, and which is what L1's handshake needs. An INI file, because
	# it is LibreSSL's format read by LibreSSL's parser, not one of our libconfig domains.
	@mkdir -p "$(ROOTFS64)/System/Configuration/SSL"
	@cp userland/configuration/openssl.cnf "$(ROOTFS64)/System/Configuration/SSL/openssl.cnf"
	# libressl_l1: L1's acceptance — a real TLS handshake between two guest processes (s_server and
	# s_client), loopback only. A SHELL SCRIPT rather than a C probe: what L1 owes is two processes
	# talking, not a library call.
	@cp userland/tests/libressl_l1.sh "$(ROOTFS64)/System/Shared/tests/libressl_l1.sh"
	# libressl_tls_pair: L1's SUBSTANCE, separated from one variable. The script above drives
	# `openssl s_server`/`s_client`, which MULTIPLEX (they select on the socket AND stdin), so their
	# stall has two readings. This pair takes the handshake through the FIRST-PARTY libtls API over
	# BLOCKING sockets with no select anywhere in it — the surface a first-party consumer would use.
	# It links the vendored libreSSL, and needs NO RPATH: the loader resolves libtls.so.33 (and its
	# libssl/libcrypto NEEDED entries) out of /System/Libraries.
	# -ltls -lssl -lcrypto IN THAT ORDER: libtls NEEDs libssl's SSL_* entry points, so leaving libssl
	# out fails the link with "undefined reference to SSL_connect" (measured).
	$(MUSL64_CC) -I$(LIBRESSL_PREFIX)/include userland/tests/libressl_tls_pair.c \
		-L$(LIBRESSL_PREFIX)/lib -ltls -lssl -lcrypto \
		-o "$(ROOTFS64)/System/Shared/tests/libressl_tls_pair"
	@cp userland/tests/libressl_tls_pair.sh "$(ROOTFS64)/System/Shared/tests/libressl_tls_pair.sh"
	# libressl_l2: L2's acceptance — the FSH TRUST STORE deciding both ways. It generates a CA and a
	# server certificate on the guest, installs the CA at /System/Configuration/SSL/cert.pem (the file
	# OPENSSLDIR points at and the file this curl was given as CURL_CA_BUNDLE), fetches over https with
	# NO --cacert, and then swaps the store's CA to prove the refusal half.
	@cp userland/tests/libressl_l2.sh "$(ROOTFS64)/System/Shared/tests/libressl_l2.sh"
	# hello_dl: the dynamic-linker smoke test. Staged under
	# System/Shared/tests - System/Tools is dynamic too since M1, but the
	# linter carve-out keeps this one out of the zero-allow scope.
	@mkdir -p "$(ROOTFS64)/System/Shared/tests"
	$(MUSL64_CC) userland/tests/hello_dl.c -o "$(ROOTFS64)/System/Shared/tests/hello_dl"
	# Overridable first-party defaults ship in Shared (plan §5.0); a
	# System copy overrides them (Xfb reads via resolved libconfig reads).
	@cp userland/configuration/system.xfb.conf "$(ROOTFS64)/Shared/Configuration/system.xfb.conf"
	# Argentum display physical size (S1.1, domain system.display): the
	# FNX-owned panel's real mm; 0 = unknown -> 96 dpi (4/3 px/pt)
	# fallback. The S1.1/S1.4 gates override via `config write -s
	# system.display display.width_mm <mm> display.height_mm <mm>`.
	@cp userland/configuration/system.display.conf \
		"$(ROOTFS64)/Shared/Configuration/system.display.conf"
	# --- the Admin home: the User Template, copied (Q9) ---
	rm -rf "$(ROOTFS64)/Users"
	@mkdir -p "$(ROOTFS64)/Users"
	cp -a "$(ROOTFS64)/System/User Template" "$(ROOTFS64)/Users/Admin"
	@echo "userland64: native x86_64 rootfs staged in $(ROOTFS64)"

# Build the native x86_64 ext2 root filesystem image (.build/root.img) from
# the ELF64 userland tree, so the kernel can boot /System/Tools/init straight
# off hdb (no initrd). Attached as a second IDE disk (hdb), it becomes the
# boot root via the kernel cmdline 'root=/dev/hdb rootfstype=ext2'.
