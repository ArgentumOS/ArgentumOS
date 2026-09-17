/*
 * foundation_collection, unit 1 of 2 — the support unit (MRR).
 *
 * It imports ONLY <foundation/Foundation.h>, so the umbrella's completeness is
 * part of the check, and it returns a collection holding a collection.
 */

#import "foundation_collection.h"

NSDictionary *foundation_collection_nested(void)
{
	NSMutableArray *letters = [NSMutableArray array];
	NSMutableDictionary *record = [NSMutableDictionary dictionary];

	[letters addObject:@"a"];
	[letters addObject:@"b"];
	[record setObject:letters forKey:@"letters"];
	[record setObject:[NSNumber numberWithInt:2] forKey:@"count"];
	return record;
}
