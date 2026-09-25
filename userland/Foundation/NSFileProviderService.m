/*
 * NSFileProviderService.m — the two members, and the refusal that is the whole point of declaring the class
 * (W8 slice 9). See the header for what this system has and does not have.
 */

#import <Foundation/NSFileProviderService.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>

#include <errno.h>
#include <string.h>

static NSError *fn_service_error(int err, NSString *what)
{
	return [NSError errorWithDomain:@"NSPOSIXErrorDomain"
				   code:err
			       userInfo:[NSDictionary dictionaryWithObject:
					 [NSString stringWithFormat:@"%@: %s", what, strerror(err)]
								    forKey:NSLocalizedDescriptionKey]];
}

@implementation NSFileProviderService

- (NSString *)name
{
	/* NEVER NIL, which is Apple's own nullability for the property: a service nothing named has an EMPTY
	 * name rather than a missing one. */
	return _name != nil ? _name : @"";
}

- (void)getFileProviderConnectionWithCompletionHandler:
	(void (^)(NSXPCConnection *connection, NSError *error))completionHandler
{
	if (completionHandler == NULL) {
		return;
	}
	/* REFUSED BY NAME: "the custom communication channel" is an NSXPCConnection, and this system has none -
	 * so the answer is nil WITH an error, and a program that asked finds out rather than waiting. */
	completionHandler(nil, fn_service_error(ENOTSUP,
		@"this system has no XPC and no file provider extension, so there is no service connection"));
}

@end
