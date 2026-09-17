/*
 * NSFastEnumeration — the protocol behind `for (id x in collection)`, and the
 * state record clang reads. docs/design/foundation-plan.md, F3.
 *
 * BOTH NAMES AND THE RECORD'S MEMBERS ARE THE ABI, and none of them is ours to
 * choose:
 *
 *   - clang lowers a for-in loop to a call to
 *     -countByEnumeratingWithState:objects:count: — it looks those three
 *     selectors up by name in its own codegen (CGObjC.cpp), not through any
 *     header we control;
 *   - the state record's four members are read BY NAME (state, itemsPtr,
 *     mutationsPtr, extra), so this layout has to be exactly this;
 *   - the protocol's NAME matters as well: clang diagnoses a for-in over a
 *     receiver that does not conform to a protocol called NSFastEnumeration.
 *
 * The mutation guard is the runtime's: the loop snapshots *mutationsPtr and
 * calls objc_enumerationMutation when the word changes under it. libobjc2 ships
 * it (mutation.m), so a collection only has to keep a word and bump it.
 */

#ifndef FOUNDATION_NSFASTENUMERATION_H
#define FOUNDATION_NSFASTENUMERATION_H

typedef struct {
	unsigned long state;		/* the collection's cursor; opaque to the loop */
	id __unsafe_unretained *itemsPtr;	/* a batch of elements, valid until the next call */
	unsigned long *mutationsPtr;	/* a word this collection bumps on mutation */
	unsigned long extra[5];		/* reserved for the collection */
} NSFastEnumerationState;

@protocol NSFastEnumeration

- (unsigned long)countByEnumeratingWithState:(NSFastEnumerationState *)state
                                     objects:(id __unsafe_unretained *)buffer
                                       count:(unsigned long)length;

@end

#endif /* FOUNDATION_NSFASTENUMERATION_H */
