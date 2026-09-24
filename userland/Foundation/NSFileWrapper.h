/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSFileWrapper — a file-system node as an OBJECT (W8 slice 4; docs/design/foundation-plan.md §60).
 * MANUAL OWNERSHIP.
 *
 * IT IS A TREE, AND THAT IS THE WHOLE DESIGN: a wrapper is a regular file with its contents, a
 * directory with a DICTIONARY of children keyed by a unique filename, or a symbolic link with a
 * destination. Reading builds the tree from disk, writing puts it back, and everything in between is a
 * value operation — which is why this class is where a document model keeps its parts.
 *
 * THE DICTIONARY KEY IS NOT ALWAYS THE PREFERRED NAME, and Apple states the rule rather than leaving it
 * to be discovered: `-addFileWrapper:` answers "a UNIQUE filename, which is the same as the passed-in
 * file wrapper's preferred filename UNLESS that name is already in use as a key in the directory's
 * dictionary of children". So a directory may hold two children that both want to be called `photo.jpg`
 * and the second one gets a different key — and `-keyForChildFileWrapper:` is how a caller finds out
 * which. `-preferredFilename`'s own page adds the other half: changing it "causes the existing parent
 * directory file wrappers to remove and re-add the child to accommodate the change", so the key can move
 * under the caller's feet and this class does exactly that.
 *
 * TWO OF APPLE'S RULES BECOME THE OPTION SETS, AND BOTH ARE MEASURED FROM ITS PAGES:
 *
 *   READING. `NSFileWrapperReadingImmediate` is "the option to read files IMMEDIATELY after creating a
 *   file wrapper", which says what the default is — LAZY. So `-initWithURL:options:error:` without it
 *   remembers where the contents came from and reads them when `-regularFileContents` asks, while WITH
 *   it the contents are taken then and there; the pair is observable, because a file changed between the
 *   two reads differently. `NSFileWrapperReadingWithoutMapping` asks that "file mapping for regular file
 *   wrappers" be disallowed — and this class never maps anything (it reads with read(2)), so that option
 *   is satisfied by construction rather than by a branch.
 *
 *   WRITING. `NSFileWrapperWritingAtomic` asks for the atomic form, and
 *   `NSFileWrapperWritingWithNameUpdating` asks that "descendant file wrappers' properties are SET IF
 *   THE WRITING SUCCEEDS" — that is, each wrapper's `-filename` becomes the name it was actually written
 *   under. Without it a wrapper that has never been written has no `-filename` at all.
 *
 * WHAT IS NOT HERE, EACH WITH ITS GROUND:
 *   * `-icon` — an `NSImage`, which is AppKit's and not Foundation's (the same ground §39 used for the
 *     other AppKit-shaped rows);
 *   * `-serializedRepresentation` and `-initWithSerializedRepresentation:` — Apple's serialization format
 *     is UNDOCUMENTED ("the file wrapper's data in the format used by the system"), so a format of our
 *     own would be a claim about compatibility rather than a copy of one; it is its own increment;
 *   * `-initWithCoder:`/`-encodeWithCoder:` — the same format question, reached through the coder family;
 *   * `-writeToFile:atomically:updateFilenames:` — Apple's own page says it "has been DEPRECATED in
 *     favor of" the URL form that DOES ship here, and this library does not carry Apple-deprecated API.
 */

#ifndef FOUNDATION_NSFILEWRAPPER_H
#define FOUNDATION_NSFILEWRAPPER_H

#import <Foundation/NSObject.h>

@class NSData;
@class NSDictionary;
@class NSError;
@class NSMutableDictionary;
@class NSString;
@class NSURL;

NS_ASSUME_NONNULL_BEGIN

/* WHAT A READ MAY ASK FOR, and the two names are Apple's. The first is the one that changes behaviour
 * here (immediate versus lazy contents); the second is satisfied by construction, because this class
 * reads bytes with read(2) and never maps a file. */
typedef enum {
	NSFileWrapperReadingImmediate = 1 << 0,
	NSFileWrapperReadingWithoutMapping = 1 << 1
} NSFileWrapperReadingOptions;

/* WHAT A WRITE MAY ASK FOR. `Atomic` writes through a temporary and renames it into place;
 * `WithNameUpdating` sets each descendant's `-filename` to the name it was written under, which is what
 * makes `-filename` readable at all after a first write. */
typedef enum {
	NSFileWrapperWritingAtomic = 1 << 0,
	NSFileWrapperWritingWithNameUpdating = 1 << 1
} NSFileWrapperWritingOptions;

@interface NSFileWrapper : NSObject
{
	NSUInteger _kind;			/* regular, directory or symbolic link */
	NSMutableDictionary *_fileWrappers;	/* children, keyed by UNIQUE filename (directories only) */
	NSData *_contents;			/* a regular file's bytes */
	NSString *_linkDestination;		/* a symbolic link's target */
	NSDictionary *_attributes;		/* what was read about the node */
	NSString *_filename;			/* the name on disk, set by reading or by writing */
	NSString *_preferredFilename;		/* the name a parent should key this child by */
	NSString *_sourcePath;			/* where a LAZY regular file's contents live */
	NSFileWrapper *_parent;			/* UNRETAINED: who holds this child, for the re-add rule */
	NSUInteger _readOptions;
}

/* ---- CREATING ---------------------------------------------------------------------------------- */

/* THE NODE'S OWN KIND DECIDES WHAT IS BUILT: "a file wrapper instance whose kind is determined by the
 * type of file-system node located by the URL". A path that is not there answers nil and fills in the
 * error, which is why this one has an error channel and `-initWithPath:` does not (Apple's own shape). */
- (nullable instancetype)initWithURL:(NSURL *)url
			     options:(NSFileWrapperReadingOptions)options
			       error:(NSError ** _Nullable)error;
- (nullable instancetype)initWithPath:(NSString *)path;

- (instancetype)initDirectoryWithFileWrappers:(nullable NSDictionary *)childrenByPreferredName;
- (instancetype)initRegularFileWithContents:(NSData *)contents;
- (instancetype)initSymbolicLinkWithDestination:(NSString *)path;
- (instancetype)initSymbolicLinkWithDestinationURL:(NSURL *)url;

/* ---- WHAT IT IS --------------------------------------------------------------------------------- */

- (BOOL)isRegularFile;
- (BOOL)isDirectory;
- (BOOL)isSymbolicLink;

/* ---- ITS CHILDREN, WHICH ONLY A DIRECTORY HAS ---------------------------------------------------- */

/* The dictionary itself, or nil for anything that is not a directory. */
- (nullable NSDictionary *)fileWrappers;

/* THE THREE DOORS THAT ANSWER A KEY, and the key is "a UNIQUE filename... the same as the passed-in
 * wrapper's preferred filename UNLESS that name is already in use". A receiver that is not a directory
 * RAISES, and so does a child with no preferred name to offer — Apple's own two sentences. */
- (NSString *)addFileWrapper:(NSFileWrapper *)child;
- (void)removeFileWrapper:(NSFileWrapper *)child;
- (NSString *)addFileWithPath:(NSString *)path;
- (NSString *)addRegularFileWithContents:(NSData *)contents
		       preferredFilename:(NSString *)preferredFilename;
- (NSString *)addSymbolicLinkWithDestination:(NSString *)path
			   preferredFilename:(NSString *)preferredFilename;

- (nullable NSString *)keyForChildFileWrapper:(NSFileWrapper *)child;

/* ---- A LINK'S TARGET ---------------------------------------------------------------------------- */

- (nullable NSString *)symbolicLinkDestination;
- (nullable NSURL *)symbolicLinkDestinationURL;

/* ---- READING AND WRITING THE TREE ---------------------------------------------------------------- */

/* "Recursively rereads the ENTIRE contents of a file wrapper from the specified location on disk" -
 * which is why this one can answer NO for a wrapper that is not there, and why it replaces what the
 * receiver held rather than merging into it. */
- (BOOL)readFromURL:(NSURL *)url
	    options:(NSFileWrapperReadingOptions)options
	      error:(NSError ** _Nullable)error;

/* "Indicates whether the file wrapper needs to be updated to match a given file-system node" - the
 * question a document asks before paying for a reread. */
- (BOOL)needsToBeUpdatedFromPath:(NSString *)path;
- (BOOL)matchesContentsOfURL:(NSURL *)url;
- (BOOL)updateFromPath:(NSString *)path;

/* TO DISK, RECURSIVELY. `originalContentsURL` is the location this wrapper was read from, and Apple's
 * purpose for it is to avoid rewriting what did not change: a regular file whose bytes are identical to
 * the original's is HARDLINKED into place instead of copied, so the two share an inode. */
- (BOOL)writeToURL:(NSURL *)url
	   options:(NSFileWrapperWritingOptions)options
originalContentsURL:(nullable NSURL *)originalContentsURL
	     error:(NSError ** _Nullable)error;

/* ---- WHAT IT IS CALLED, AND WHAT WAS READ ABOUT IT ---------------------------------------------- */

/* `-filename` is the name on disk and is nil until the wrapper is read from or written to one, which is
 * exactly what `-writeToURL:`'s name-updating option is for. */
- (nullable NSString *)filename;
- (void)setFilename:(nullable NSString *)filename;
- (nullable NSString *)preferredFilename;
- (void)setPreferredFilename:(nullable NSString *)preferredFilename;

/* Apple declares this NONNULL and an empty dictionary is what "nothing was read" means; the classic
 * spelling of the same thing is the `fileAttributes` dictionary -attributesOfItemAtPath: answers. */
- (NSDictionary *)fileAttributes;
- (void)setFileAttributes:(NSDictionary *)fileAttributes;

- (nullable NSData *)regularFileContents;

/* ---- SERIALIZING THE WHOLE TREE (W8 slice 4b) ---------------------------------------------------- */

/* APPLE PUBLISHES THE FORM AND NOT THE SCHEMA, SO THE SCHEMA IS OURS AND IT IS WRITTEN DOWN HERE. The
 * form is stated in Apple's own words: the property holds "a data object in the format used by the
 * NSFileWrapper pasteboard type", and "this data object is also suitable for passing to
 * -initWithSerializedRepresentation:". That names a PROPERTY LIST, so the representation is one - an XML
 * plist, which is the format this library's plist layer speaks - with these keys, which are ours:
 *
 *   Type                     "Regular", "Directory" or "SymbolicLink"
 *   PreferredFileName        the name a parent would key this child by (present when known)
 *   FileName                 the name it was read from or written under (present when known)
 *   FileAttributes           the attributes dictionary, when anything was read
 *   RegularFileContents      a regular file's bytes
 *   SymbolicLinkDestination  a link's target
 *   FileWrappers             a dictionary of key -> nested representation (directories)
 *
 * It is therefore NOT byte-compatible with Apple's own, which is unverifiable by design (its format is
 * undocumented); what IS asserted is that a whole tree survives a round trip through it, and that the
 * data really is a property list - which is the half Apple does state. */
- (nullable NSData *)serializedRepresentation;
- (nullable instancetype)initWithSerializedRepresentation:(NSData *)data;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILEWRAPPER_H */
