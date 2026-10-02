/*
 * FNStringFormat.h — THE FORMAT ENGINE'S SINK, INTERNAL (§63.85).
 *
 * WHY THIS HEADER EXISTS: the format walk in NSString.m is ONE ARITHMETIC — the spec grammar, the length
 * modifiers and the va_arg TYPES live in exactly one place — and the ATTRIBUTED formatter
 * (NSAttributedString's -initWithFormat: family) must use that same walk rather than grow a second, divergent
 * copy of it. So the engine emits through a SINK, and this header is the seam.
 *
 * ⚠ THE PLAIN PATH IS UNCHANGED TO THE MESSAGE SEND: with `recorder` nil the sink calls the very same
 * -appendUTF8String: and -appendString: the engine called before, so the refactor that introduced this seam is
 * verified by the probe that ALREADY EXISTS rather than by a new one.
 *
 * ⚠ AND `pos` IS THE BYTE OFFSET IN THE FORMAT THE TEXT CAME FROM, and `object` is non-nil only for a `%@`
 * conversion. THOSE TWO VALUES ARE WHY THE SEAM EXISTS: the attributed sink needs the format's attributes at the
 * position an emission came from, and it needs the argument OBJECT itself to honour
 * NSAttributedStringFormattingInsertArgumentAttributesWithoutMerging. Neither was available before, and the
 * attributed formatter cannot be written faithfully without both.
 */
#ifndef FOUNDATION_FNSTRINGFORMAT_H
#define FOUNDATION_FNSTRINGFORMAT_H

#include <stdarg.h>

#import <Foundation/NSObjCRuntime.h>

@class NSString;
@class NSMutableString;

NS_ASSUME_NONNULL_BEGIN

/* ⚠ AND IT IS A PROTOCOL, WHICH THE FIRST DRAFT ARGUED AGAINST ON THE GROUNDS THAT "a protocol in a header is a
 * promise to a reader of the public surface" — a good rule about a PUBLIC header and the wrong rule about this
 * one. WITHOUT IT the recorder's methods are invisible across translation units and the build grows two
 * `-Wobjc-method-access` warnings while still working by accident: a message the compiler cannot see has its
 * return type default to `id`, which is exactly right until the day it is not. */
@protocol FNFormatSinkRecording <NSObject>
- (void)fnFormatEmittedUTF8String:(const char *)utf8 at:(NSUInteger)pos;
/* `object` is nullable AND HAS TO BE: the engine emits non-`%@` text with object nil, and the attributed sink
 * distinguishes "a %@ argument" from "everything else" by exactly that. */
- (void)fnFormatEmittedText:(NSString *)text at:(NSUInteger)pos object:(nullable id)object;
@end

/* ⚠⚠ AND THE TWO ANNOTATIONS HERE ARE NOT A FORMALITY: F6's gate refused this file for opening no
 * nullability region, and writing it out surfaced TWO REAL FACTS. **EACH FIELD IS NIL ON EXACTLY ONE OF THE TWO
 * PATHS** — `out` is set only for the plain sink and `recorder` only for the attributed one — so both are
 * `_Nullable`, and an implicit nonnull on either would have been a promise the design breaks the moment the other
 * sink is used. THE RULE FOUND SOMETHING THE DRAFT HAD NOT. */
typedef struct {
	NSMutableString * _Nullable out;      /* the plain sink: what the engine appended to before */
	id<FNFormatSinkRecording> _Nullable recorder;   /* nil, or an object conforming to the protocol */
} fn_format_sink;

void fn_string_append_format_sink(fn_format_sink *sink, NSString *format, va_list args);

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_FNSTRINGFORMAT_H */
