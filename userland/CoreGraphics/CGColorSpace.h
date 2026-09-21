/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * CGColorSpace.h — the colour space, as far as C2 needs one.
 *
 * A BITMAP CONTEXT CANNOT BE CREATED WITHOUT ONE, which is the whole reason this
 * header exists in the C2 milestone: `CGBitmapContextCreate` takes a colour space and
 * the pixels' meaning depends on it. What is here is the device spaces — the two that
 * have no profile and no colour management because device RGB *is* the assumption C2
 * draws under.
 *
 * THE REST IS C4 (docs/design/coregraphics-plan.md §7), and it is arriving in pieces rather
 * than as stubs: `CGColorSpaceCreateLab` is here, because a Lab space is what gives the engine
 * something to CONVERT and therefore something to check. Still absent, and still deliberate:
 * the ICC-based and indexed spaces (an ICC space needs a data provider, an indexed one a
 * table), the calibrated spaces, the extended-range and HDR predicates, the colour table and
 * the property-list serialization. A stub that returned a plausible object for
 * `CGColorSpaceCreateICCBased` would be worse than an absent symbol, because the pixels would
 * look right and be wrong.
 *
 * AND THE ENGINE IS BOUND NOW (lcms2, vendored at 2.19.1 by tools/lcms2-build.sh, linked by
 * this library): a space built from PARAMETERS carries the engine's profile, and
 * `cg_colorspace_engine_profile` is the private seam the colour half asks through. THE FIELD
 * IS ALSO THE KIND — a profile means the components have to be INTERPRETED, no profile means
 * they are what you blend — which is what closes the gap recorded against C4.1, where the
 * guard dispatched on the colour model alone and so would have copied a calibrated-RGB colour
 * as if it were a device one.
 */
#ifndef CORE_GRAPHICS_CGCOLORSPACE_H
#define CORE_GRAPHICS_CGCOLORSPACE_H

#include <CoreGraphics/CGBase.h>
/* A DATA PROVIDER IS WHERE AN ICC PROFILE'S BYTES ARRIVE, and this header names the type, so it
 * includes the header that declares it — the same include direction as CGContext.h taking
 * CGColor.h: the higher-level object knows the lower one, never the reverse. That is what keeps
 * CGDataProvider.h free of any knowledge of colour. */
#include <CoreGraphics/CGDataProvider.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CGColorSpace *CGColorSpaceRef;

/* THE CASE VALUES ARE THIS TREE'S, as everywhere a `…Model` enum appears: Apple
 * publishes the case names and no numbers, and nothing here depends on which number
 * is which. The ORDER is Apple's documented order, so a reader comparing the two
 * lists finds them in the same sequence. */
typedef enum {
	kCGColorSpaceModelUnknown = -1,
	kCGColorSpaceModelMonochrome = 0,
	kCGColorSpaceModelRGB = 1,
	kCGColorSpaceModelCMYK = 2,
	kCGColorSpaceModelLab = 3,
	kCGColorSpaceModelDeviceN = 4,
	kCGColorSpaceModelIndexed = 5,
	kCGColorSpaceModelPattern = 6,
	kCGColorSpaceModelXYZ = 7
} CGColorSpaceModel;

/* THE DEVICE SPACES. Each is a shared singleton: a device space has no parameters, so two
 * callers asking for device RGB get the same object and the refcount is what keeps it alive.
 *
 * CMYK IS HERE FOR THE COLOUR IT MAKES POSSIBLE RATHER THAN FOR A DRAWING PATH: a CMYK colour
 * can be created and inspected — its four components and its alpha are just numbers, and the
 * model says what they mean — while the CONTEXT still REFUSES to draw with it, because four
 * inks are not three lights and THERE IS NO DEVICE-CMYK PROFILE for the engine to convert
 * through. It is now the ONLY refused colour that can be built, which is exactly the role it
 * was added for: without it, the only refusal a probe could reach was a NULL.
 *
 * AND Lab IS THE OPPOSITE CASE, which is why it arrives with the engine rather than after it.
 * A Lab space HAS a profile — lcms2 builds one from the white point alone — so a Lab colour is
 * drawn by CONVERTING it, not by refusing it. The two spaces together are what let the colour
 * probe show both sides of the line this design draws. */
CGColorSpaceRef CGColorSpaceCreateDeviceRGB(void);
CGColorSpaceRef CGColorSpaceCreateDeviceGray(void);
CGColorSpaceRef CGColorSpaceCreateDeviceCMYK(void);
/* A NULL `whitePoint` is D50, which is Apple's documented default and exactly what the engine's
 * own Lab4 profile uses — so the default is not an approximation of Apple's, it is the same
 * thing. The black point and the range are accepted and have no effect, which is Apple's
 * documented behaviour for a Lab space as well. */
CGColorSpaceRef CGColorSpaceCreateLab(const CGFloat *whitePoint, const CGFloat *blackPoint,
				      const CGFloat *range);

/* A SPACE FROM AN ICC PROFILE'S OWN BYTES — and the profile arrives through a CGDataProvider,
 * which is a CORE GRAPHICS type rather than a Core Foundation one. That is what makes this
 * function reachable without resolving the Foundation question plan §6 leaves open, where
 * `CGColorSpaceCreateICCBased`'s CF-typed cousins (`CreateWithName`, `CreateWithICCData`) wait.
 *
 * WHAT IS READ FROM THE PROFILE IS ITS COLOUR SPACE, and nothing else: the components are the
 * profile's own, so the model reported here is the profile's model. A profile whose colour
 * space has no CG model in this library is REFUSED rather than given a plausible one — the
 * numbers would draw something, and something wrong.
 *
 * `nComponents` may be 0 (take the profile's own count) or the profile's count exactly. The
 * `range` is accepted and has no effect, and a non-NULL `alternate` is REFUSED: both are
 * documented, and the second is refused rather than ignored because a fallback space that is
 * silently dropped changes what a caller thinks they asked for. */
CGColorSpaceRef CGColorSpaceCreateICCBased(size_t nComponents, const CGFloat *range,
					   CGDataProviderRef profile, CGColorSpaceRef alternate);

/* ---------------------------------------------------------------------------------------
 * THE SYSTEM-DEFINED SPACES, BY NAME — AND THE FIRST FOUNDATION OBJECTS IN THIS HEADER.
 *
 * Apple names them with `CFStringRef` constants; this tree names them `NSString *`, because the
 * retracted CoreFoundation plan settled that CoreGraphics here declares its signatures with
 * FOUNDATION types rather than inventing a CF layer. That leaves one mechanical question, and IT
 * WAS MEASURED BEFORE IT WAS WRITTEN: a C compiler cannot see `@interface NSString`, so under
 * `__OBJC__` this header forward-declares the class, and under a C compiler it declares the
 * opaque struct a class is at the ABI level. A C caller passes and receives that pointer and
 * never looks inside it; the one translation unit that does is Objective-C, imports Foundation,
 * and is compiled by the tree's Objective-C wrapper.
 *
 * THE COST IS REAL AND IS PAID WHERE IT IS STATED: `libcoregraphics.so.1` now DEPENDS ON
 * libfoundation, because comparing a name is a message send. There is no honest route around
 * it — the names ARE the API — and the alternative would be inventing C-string-keyed functions
 * Apple does not have.
 * ------------------------------------------------------------------------------------- */
#ifdef __OBJC__
@class NSString;
#else
typedef struct objc_object NSString;
#endif

/* The names this library answers with an EXACT profile — exact in the sense that matters: each
 * space's white point, primaries and transfer function are given to the engine rather than
 * substituted for something this library cannot express. What is absent is absent for that same
 * reason, name by name in CGColorSpaceNames.m, and stays `open` in the ledger. */
extern NSString *const kCGColorSpaceSRGB;
extern NSString *const kCGColorSpaceLinearSRGB;
extern NSString *const kCGColorSpaceDisplayP3;
extern NSString *const kCGColorSpaceLinearDisplayP3;
extern NSString *const kCGColorSpaceAdobeRGB1998;
extern NSString *const kCGColorSpaceROMMRGB;
extern NSString *const kCGColorSpaceGenericLab;
extern NSString *const kCGColorSpaceGenericGrayGamma2_2;

/* A space by name, or NULL with a reason on stderr. THE NAMED SPACES ARE NOT CACHED, WHICH IS A
 * DEVIATION: Apple returns the same object for the same name, and this builds a fresh one per
 * call. THE DEVICE SPACES REMAIN SHARED SINGLETONS, and THE NAMED SPACES ARE CACHED NOW TOO —
 * one object per name — because that day arrived with `CGColorSpaceCopyName` below: a space that
 * can say which name it was made with has to know it. */
CGColorSpaceRef CGColorSpaceCreateWithName(NSString *name);

/* THE NAME THIS SPACE WAS MADE WITH, OR NIL. It is a COPY — the caller owns it — and it closes the
 * loop with the function above: a name that comes out of this one can go straight back in, because
 * the named spaces are one object per name.
 *
 * NIL IS THE HONEST ANSWER FOR EVERY OTHER SPACE. A DEVICE space has no name to give — Apple's
 * index has no `kCGColorSpaceDevice…` row at all, measured — and a space built from profile bytes
 * or from parameters was never asked for by name. Apple's documentation for this function allows
 * exactly that case: "or NULL if the colour space has no name".
 *
 * THE `CG_RETURNS_RETAINED` ATTRIBUTE IS PART OF THE SIGNATURE AND NOT DECORATION: a `Copy`-named
 * function returns +1, and an Objective-C caller under ARC would otherwise treat it as +0 and leak.
 * It EXPANDS AWAY under a C compiler, where it means nothing and clang would warn that it is
 * ignored — which is what CGBase.h's macro is for. */
NSString *CGColorSpaceCopyName(CGColorSpaceRef space) CG_RETURNS_RETAINED;

/* THE ICC PROFILE AS FOUNDATION DATA: `NSData *` where Apple's name says `CFData`, and the
 * counterpart of `CGDataProviderCreateWithCFData`. It is a BRIDGE — the data becomes a provider
 * and goes to `CGColorSpaceCreateICCBased`, which is where parsing, validation and taking the
 * model out of the profile all live — so this form and the file form cannot reach different
 * conclusions about what a profile means. The data may be released as soon as this returns. */
CGColorSpaceRef CGColorSpaceCreateWithICCData(NSData *data);

/* THE PROFILE'S BYTES BACK OUT, AS FOUNDATION DATA — and WHAT COMES OUT IS A RE-SERIALISATION
 * RATHER THAN THE BYTES THAT WENT IN. A space keeps the PARSED profile: that is what lets drawing
 * through it be fast and the provider be released the moment the space exists, so these are the
 * bytes the engine WRITES. They describe the same space, which is the property that matters and
 * the one the probe checks, by converting through a space rebuilt from them.
 *
 * A SPACE WITH NO STORED PROFILE ANSWERS NIL, which is Apple's own contract for a space without ICC
 * data and is the case for every DEVICE space here. Apple's device spaces do carry profiles; this
 * library's do not, and its conversion path synthesises the one it needs when it needs it rather
 * than keeping it. */
NSData *CGColorSpaceCopyICCData(CGColorSpaceRef space) CG_RETURNS_RETAINED;

/* THE CALIBRATED SPACES, AND THE ONE PLACE APPLE'S PARAMETERS DO NOT TRANSFER DIRECTLY. Apple
 * gives a MATRIX taking RGB to XYZ; the engine wants three PRIMARIES in xy. Those are the same
 * fact in two spellings — a column of an RGB-to-XYZ matrix IS that primary's XYZ — so the step
 * between them is arithmetic rather than an approximation, and sRGB's own matrix builds this
 * library's device RGB described the long way round.
 *
 * A NULL `matrix` MEANS THIS LIBRARY'S DEVICE RGB, which is sRGB and is what the conversion path
 * already states: "the default RGB colour space" has to mean something specific, and device RGB
 * has a definition here. A NULL `whitePoint` means D65, for the same reason — a matrix without
 * its white point is half a space.
 *
 * THE BLACK POINT IS ACCEPTED AND HAS NO EFFECT. That is as much Apple's description of it as
 * this library's limitation: Apple documents it as mattering only under the ABSOLUTE
 * COLORIMETRIC intent, and an ICC matrix/TRC profile has no field for one. */
CGColorSpaceRef CGColorSpaceCreateCalibratedGray(const CGFloat *whitePoint,
						 const CGFloat *blackPoint, CGFloat gamma);
CGColorSpaceRef CGColorSpaceCreateCalibratedRGB(const CGFloat *whitePoint,
						const CGFloat *blackPoint,
						const CGFloat gamma[3], const CGFloat matrix[9]);

CGColorSpaceRef CGColorSpaceRetain(CGColorSpaceRef space);
void CGColorSpaceRelease(CGColorSpaceRef space);

CGColorSpaceModel CGColorSpaceGetModel(CGColorSpaceRef space);
size_t CGColorSpaceGetNumberOfComponents(CGColorSpaceRef space);

/* THE PREDICATES, AND WHICH OF THEM ARE COMPUTED.
 *
 * `SupportsOutput` asks whether this library can DRAW a colour in the space, and it is answered
 * from the SAME fact the context's setters refuse from: a space with a profile can be converted
 * and therefore drawn, while device CMYK has neither and answers NO.
 *
 * `IsWideGamutRGB` compares the space's PRIMARIES against sRGB's triangle, because wide gamut
 * means the space includes colours sRGB does not and a primary is such a colour. A space that IS
 * sRGB — which device RGB is, by this library's own definition — has its primaries ON that
 * triangle and answers NO, and a profile whose primaries cannot be read (anything that is not a
 * matrix shaper) answers NO as well, since a gamut nobody can read is a gamut nobody can compare.
 *
 * THE OTHER FOUR ANSWER NO BECAUSE NO SUCH SPACE EXISTS HERE YET, not because they are stubs:
 * an extended-range space has components outside 0..1, and an HDR, PQ or HLG space is built on
 * one of those unbounded transfer curves. Everything this library builds is bounded and 0..1, so
 * each of the four is a statement about the library — and the day one of them changes, it is the
 * function's own comment that changes with it. */
bool CGColorSpaceSupportsOutput(CGColorSpaceRef space);
bool CGColorSpaceIsWideGamutRGB(CGColorSpaceRef space);
bool CGColorSpaceUsesExtendedRange(CGColorSpaceRef space);
bool CGColorSpaceIsHDR(CGColorSpaceRef space);
bool CGColorSpaceIsHLGBased(CGColorSpaceRef space);
bool CGColorSpaceIsPQBased(CGColorSpaceRef space);

#ifdef __cplusplus
}
#endif

#endif /* CORE_GRAPHICS_CGCOLORSPACE_H */
