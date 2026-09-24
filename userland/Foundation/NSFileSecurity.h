/*
 * NSFileSecurity.h — A CONTAINER APPLE DESCRIBES AS A STUB, SHIPPED AS ONE.
 *
 * APPLE'S OWN OVERVIEW, VERBATIM: "A stub class that encapsulates security information about a file."
 * and "contains no methods of its own. Instead, it is transparently bridged to CFFileSecurity."
 *
 * THAT SECOND SENTENCE IS THE WHOLE DESIGN, AND IT IS A MEASUREMENT RATHER THAN A READING. The
 * documentation index carries EXACTLY ONE member page under this class - `init?(coder:)` - and every
 * accessor a reader would expect (an owner, a group, a mode, an access control list, an owner UUID, a
 * group UUID) is published on **CFFileSecurity** instead, as the C functions CFFileSecurityGetOwner,
 * CFFileSecuritySetOwner, CFFileSecurityGetGroup, CFFileSecurityGetMode,
 * CFFileSecurityCopyAccessControlList, CFFileSecurityCopyOwnerUUID, CFFileSecurityCopyGroupUUID and
 * their setters. The class's "Conforms To" list is the other half of the published surface: NSCoding,
 * NSCopying and NSSecureCoding.
 *
 * SO ON ARGENTUM THERE IS NOTHING TO BE BRIDGED TO, AND THIS IS THE BOUNDARY (§11.6.1 D13): this tree
 * HAS NO COREFOUNDATION — no `CFFileSecurity`, no `CFUUID`, no CoreFoundation family at all — so the
 * facts the container stands for are not reachable THROUGH THIS CLASS here, and they are not invented
 * onto it either (inventing an API is worse than refusing one, which is the same standard §11.5
 * applies to everything else). They remain reachable where this system actually keeps them:
 *
 *   - the OWNER, the GROUP and the MODE, through NSFileManager's `-attributesOfItemAtPath:error:` and
 *     `-setAttributes:ofItemAtPath:error:` (both shipped, with the keys NSFileOwnerAccountID,
 *     NSFileGroupOwnerAccountID and NSFilePosixPermissions);
 *   - the ACCESS CONTROL LIST, through the POSIX-ACL substrate the kernel ships (an xattr-backed store
 *     with its own gate — `kernel/acl.c`, `include/fnx/acl.h`) and the `acl` tool on top of it.
 *
 * WHAT THIS CLASS SHIPS INSTEAD is therefore the whole of what Apple publishes FOR IT: the object, the
 * three conformances, being secure-codeable, copying, and going into and coming out of an archive. It
 * carries NO STATE, and that is a decision rather than an omission: with no door able to fill it and no
 * door able to read it, private state would be code that nothing could ever observe - and this library
 * demands behaviour by CHECK. The probe asserts what is here, and asserts the ABSENCE of the bridged
 * accessors as a fact, so the boundary is machine-checked rather than merely written down.
 *
 * WHEN A COREFOUNDATION ARRIVES, the state lands WITH its doors and this row is updated; until then the
 * absence is named here, in §11.6.1 D13, and in the probe.
 */

#ifndef FOUNDATION_NSFILESECURITY_H
#define FOUNDATION_NSFILESECURITY_H

#import <Foundation/NSObject.h>
#import <Foundation/NSCoding.h>

@class NSCoder;

NS_ASSUME_NONNULL_BEGIN

/* The class's ONE published method: Apple lists `init?(coder:)` under Initializers and nothing else, so
 * it is declared here rather than left to the protocol - the published surface is the specification. */
@interface NSFileSecurity : NSObject <NSCopying, NSSecureCoding>

- (nullable instancetype)initWithCoder:(NSCoder *)coder;

/* Immutable to the outside (nothing here can change it) but COPIED rather than shared: the type's facts
 * are SETTABLE through the bridge Apple documents, so a copy must be its own object. */
- (id)copy;

@end

NS_ASSUME_NONNULL_END

#endif /* FOUNDATION_NSFILESECURITY_H */
