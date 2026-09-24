/*
 * NSFileAccessIntent.m — the two factories and the one published accessor (W8 slice 7a). The coordinator
 * class itself is 7b; see the header for the split and for why nothing beyond Apple's published surface is
 * declared here.
 */

#import <Foundation/NSFileCoordinator.h>
#import <Foundation/NSURL.h>

@implementation NSFileAccessIntent

+ (nullable NSFileAccessIntent *)readingIntentWithURL:(NSURL *)url
					     options:(NSFileCoordinatorReadingOptions)options
{
	return [self fnIntentWithURL:url writing:NO options:(NSUInteger)options];
}

+ (nullable NSFileAccessIntent *)writingIntentWithURL:(NSURL *)url
					     options:(NSFileCoordinatorWritingOptions)options
{
	return [self fnIntentWithURL:url writing:YES options:(NSUInteger)options];
}

/* ONE CONSTRUCTOR BEHIND THE TWO FACTORIES, so the kind is the only difference between them - which is
 * what Apple's abstract says the object IS ("the details of a coordinated-read or coordinated-write
 * operation"). A NIL URL IS REFUSED rather than carried: an intent whose -URL answered nil would be an
 * operation with no item, and the caller would find that out at the door instead of here. */
+ (nullable NSFileAccessIntent *)fnIntentWithURL:(nullable NSURL *)url
					writing:(BOOL)writing
					options:(NSUInteger)options
{
	NSFileAccessIntent *intent;

	if (url == nil) {
		return nil;
	}
	intent = [[self alloc] init];
	if (intent == nil) {
		return nil;
	}
	intent->_url = [url retain];
	intent->_writing = writing;
	intent->_options = options;
	return [intent autorelease];
}

- (NSURL *)URL
{
	return _url;
}

- (void)dealloc
{
	[_url release];
	[super dealloc];
}

@end
