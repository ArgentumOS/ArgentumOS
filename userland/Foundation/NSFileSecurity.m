/*
 * NSFileSecurity.m — the stub, implemented as the stub it is. See the header for the measurement and
 * for §11.6.1 D13, the registered boundary: this tree has no CoreFoundation, so there is no
 * CFFileSecurity to be bridged to, and the facts are reachable through NSFileManager (owner, group,
 * mode) and the kernel's xattr-backed POSIX-ACL substrate instead.
 */

#import <Foundation/NSFileSecurity.h>
#import <Foundation/NSCoder.h>

@implementation NSFileSecurity

/* THE THREE CONFORMANCES ARE THE PUBLISHED SURFACE, and the secure half follows this tree's standing
 * ruling (NSCoding.h): the declaration ships and `+supportsSecureCoding` answers, while the
 * unarchiver's enforcement is a named coder work item rather than a class's business. */
+ (BOOL)supportsSecureCoding
{
	return YES;
}

/* The object carries no state here, so the pair writes and reads NOTHING - but the pair EXISTS, because
 * it is how an archive naming this class comes back AS this class. Both halves are here for the same
 * reason every other class in this library implements both: a class that can be written and never read
 * is half a protocol. */
- (void)encodeWithCoder:(NSCoder *)coder
{
	(void)coder;
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	if (coder == nil) {
		[self release];
		return nil;
	}
	return [super init];
}

/* A COPY IS ITS OWN OBJECT - not `[self retain]`: the type's facts are SETTABLE through the bridge
 * Apple documents, so two references to one object would stop being independent the moment that bridge
 * exists. Nothing is carried over because nothing is carried. */
- (id)copy
{
	return [[NSFileSecurity alloc] init];
}

@end
