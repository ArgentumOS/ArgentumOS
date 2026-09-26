/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSError.m — the error value.
 *
 * MANUAL OWNERSHIP: it stores three owned objects and implements no -retain/-release.
 */

#import <Foundation/NSError.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDictionary.h>
#include <stdio.h>

/* THE VALUES ARE COCOA'S, INCLUDING ITS ASYMMETRY: three of these drop the "Key"/"ErrorKey"
 * suffix that their NAMES carry, and NSLocalizedDescriptionKey does NOT. F12's probe found the
 * difference the hard way — it looked the message up under the literal "NSLocalizedDescriptionKey"
 * and found nothing, because this line said "NSLocalizedDescription". A caller that serialises a
 * userInfo dictionary is entitled to the same strings Cocoa uses. */
NSString *const NSLocalizedDescriptionKey = @"NSLocalizedDescriptionKey";
NSString *const NSLocalizedFailureReasonKey = @"NSLocalizedFailureReason";
NSString *const NSLocalizedRecoverySuggestionErrorKey = @"NSLocalizedRecoverySuggestion";
NSString *const NSUnderlyingErrorKey = @"NSUnderlyingError";

@implementation NSError

/* ---- THE ERROR DOMAINS, USER-INFO KEYS AND CODES (the coverage slice) ------------------------------
 *
 * THE NAMES ARE APPLE'S AND THE VALUES ARE OURS (§11.6.1 D2), WITH ONE PROPERTY THAT DOES MATTER: an error
 * code is a number a caller can compare, so each family here is numbered sequentially and its Minimum/Maximum
 * pair ENCLOSES EXACTLY ITS OWN CODES. A program that compares against the symbols is right; a program that
 * hardcodes Apple's numbers is not portable, and this header says so rather than implying a fidelity the
 * library does not claim. */
NSErrorDomain const NSCocoaErrorDomain = @"NSCocoaErrorDomain";
NSErrorDomain const NSDebugDescriptionErrorKey = @"NSDebugDescriptionErrorKey";
NSErrorDomain const NSFilePathErrorKey = @"NSFilePathErrorKey";
NSErrorDomain const NSHelpAnchorErrorKey = @"NSHelpAnchorErrorKey";
NSErrorDomain const NSLocalizedFailureErrorKey = @"NSLocalizedFailureErrorKey";
NSErrorDomain const NSLocalizedFailureReasonErrorKey = @"NSLocalizedFailureReasonErrorKey";
NSErrorDomain const NSLocalizedRecoveryOptionsErrorKey = @"NSLocalizedRecoveryOptionsErrorKey";
NSErrorDomain const NSMachErrorDomain = @"NSMachErrorDomain";
NSErrorDomain const NSMultipleUnderlyingErrorsKey = @"NSMultipleUnderlyingErrorsKey";
NSErrorDomain const NSOSStatusErrorDomain = @"NSOSStatusErrorDomain";
NSErrorDomain const NSPOSIXErrorDomain = @"NSPOSIXErrorDomain";
NSErrorDomain const NSRecoveryAttempterErrorKey = @"NSRecoveryAttempterErrorKey";
NSErrorDomain const NSStringEncodingErrorKey = @"NSStringEncodingErrorKey";

NSInteger const NSBundleErrorMaximum = 104;
NSInteger const NSBundleErrorMinimum = 100;
NSInteger const NSBundleOnDemandResourceExceededMaximumSizeError = 101;
NSInteger const NSBundleOnDemandResourceInvalidTagError = 102;
NSInteger const NSBundleOnDemandResourceOutOfSpaceError = 103;
NSInteger const NSCloudSharingConflictError = 1005;
NSInteger const NSCloudSharingErrorMaximum = 1011;
NSInteger const NSCloudSharingErrorMinimum = 1004;
NSInteger const NSCloudSharingNetworkFailureError = 1006;
NSInteger const NSCloudSharingNoPermissionError = 1007;
NSInteger const NSCloudSharingOtherError = 1008;
NSInteger const NSCloudSharingQuotaExceededError = 1009;
NSInteger const NSCloudSharingTooManyParticipantsError = 1010;
NSInteger const NSCoderErrorMaximum = 1915;
NSInteger const NSCoderErrorMinimum = 1911;
NSInteger const NSCoderInvalidValueError = 1912;
NSInteger const NSCoderReadCorruptError = 1913;
NSInteger const NSCoderValueNotFoundError = 1914;
NSInteger const NSExecutableArchitectureMismatchError = 2816;
NSInteger const NSExecutableErrorMaximum = 2821;
NSInteger const NSExecutableErrorMinimum = 2815;
NSInteger const NSExecutableLinkError = 2817;
NSInteger const NSExecutableLoadError = 2818;
NSInteger const NSExecutableNotLoadableError = 2819;
NSInteger const NSExecutableRuntimeMismatchError = 2820;
NSInteger const NSFeatureUnsupportedError = 3722;
NSInteger const NSFileErrorMaximum = 4645;
NSInteger const NSFileErrorMinimum = 4623;
NSInteger const NSFileLockingError = 4624;
NSInteger const NSFileManagerUnmountBusyError = 4625;
NSInteger const NSFileManagerUnmountUnknownError = 4626;
NSInteger const NSFileNoSuchFileError = 4627;
NSInteger const NSFileReadCorruptFileError = 4628;
NSInteger const NSFileReadInapplicableStringEncodingError = 4629;
NSInteger const NSFileReadInvalidFileNameError = 4630;
NSInteger const NSFileReadNoPermissionError = 4631;
NSInteger const NSFileReadNoSuchFileError = 4632;
NSInteger const NSFileReadTooLargeError = 4633;
NSInteger const NSFileReadUnknownError = 4634;
NSInteger const NSFileReadUnknownStringEncodingError = 4635;
NSInteger const NSFileReadUnsupportedSchemeError = 4636;
NSInteger const NSFileWriteFileExistsError = 4637;
NSInteger const NSFileWriteInapplicableStringEncodingError = 4638;
NSInteger const NSFileWriteInvalidFileNameError = 4639;
NSInteger const NSFileWriteNoPermissionError = 4640;
NSInteger const NSFileWriteOutOfSpaceError = 4641;
NSInteger const NSFileWriteUnknownError = 4642;
NSInteger const NSFileWriteUnsupportedSchemeError = 4643;
NSInteger const NSFileWriteVolumeReadOnlyError = 4644;
NSInteger const NSFormattingError = 5546;
NSInteger const NSFormattingErrorMaximum = 5547;
NSInteger const NSFormattingErrorMinimum = 5545;
NSInteger const NSPropertyListErrorMaximum = 6453;
NSInteger const NSPropertyListErrorMinimum = 6447;
NSInteger const NSPropertyListReadCorruptError = 6448;
NSInteger const NSPropertyListReadStreamError = 6449;
NSInteger const NSPropertyListReadUnknownVersionError = 6450;
NSInteger const NSPropertyListWriteInvalidError = 6451;
NSInteger const NSPropertyListWriteStreamError = 6452;
NSInteger const NSUbiquitousFileErrorMaximum = 7357;
NSInteger const NSUbiquitousFileErrorMinimum = 7353;
NSInteger const NSUbiquitousFileNotUploadedDueToQuotaError = 7354;
NSInteger const NSUbiquitousFileUbiquityServerNotAvailable = 7355;
NSInteger const NSUbiquitousFileUnavailableError = 7356;
NSInteger const NSUserActivityConnectionUnavailableError = 9160;
NSInteger const NSUserActivityErrorMaximum = 9164;
NSInteger const NSUserActivityErrorMinimum = 9159;
NSInteger const NSUserActivityHandoffFailedError = 9161;
NSInteger const NSUserActivityHandoffUserInfoTooLargeError = 9162;
NSInteger const NSUserActivityRemoteApplicationTimedOutError = 9163;
NSInteger const NSUserCancelledError = 8258;
NSInteger const NSValidationErrorMaximum = 10065;
NSInteger const NSValidationErrorMinimum = 10064;


+ (instancetype)errorWithDomain:(NSErrorDomain)domain
			   code:(NSInteger)code
		       userInfo:(NSDictionary *)userInfo
{
	return [[self alloc] initWithDomain:domain code:code userInfo:userInfo];
}

- (id)initWithDomain:(NSErrorDomain)domain
		code:(NSInteger)code
	    userInfo:(NSDictionary *)userInfo
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	/* Copied, not retained: an error is a value, and a caller that hands over a
	 * mutable dictionary must not be able to edit the error afterwards. */
	_domain = [domain copy];
	_code = code;
	_userInfo = [userInfo copy];
	return self;
}

- (NSErrorDomain)domain
{
	return _domain;
}

- (NSInteger)code
{
	return _code;
}

- (NSDictionary *)userInfo
{
	return _userInfo;
}

- (NSString *)localizedDescription
{
	NSString *described = [_userInfo objectForKey:NSLocalizedDescriptionKey];

	if (described != nil) {
		return described;
	}
	return [NSString stringWithFormat:@"The operation could not be completed. (%@ error %ld.)",
					 _domain, (long)_code];
}

- (NSString *)localizedFailureReason
{
	return [_userInfo objectForKey:NSLocalizedFailureReasonKey];
}

- (BOOL)isEqualToError:(NSError *)other
{
	if (other == nil) {
		return NO;
	}
	if (other == self) {
		return YES;
	}
	if (_code != [other code] || ![[other domain] isEqualToString:_domain]) {
		return NO;
	}
	{
		NSDictionary *theirs = [other userInfo];

		if (theirs == _userInfo) {
			return YES;
		}
		if (theirs == nil || _userInfo == nil) {
			return NO;
		}
		return [_userInfo isEqualToDictionary:theirs];
	}
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (other == nil || ![other isKindOfClass:[NSError class]]) {
		return NO;
	}
	return [self isEqualToError:(NSError *)other];
}

- (NSUInteger)hash
{
	return [_domain hash] ^ (NSUInteger)_code;
}

- (NSString *)description
{
	/* Cocoa's shape: domain, code, the localised description, then the userInfo. */
	return [NSString stringWithFormat:@"Error Domain=%@ Code=%ld \"%@\" UserInfo=%@",
					 _domain, (long)_code, [self localizedDescription],
					 (_userInfo != nil) ? [_userInfo description] : @"(null)"];
}

- (id)copy
{
	return [self retain];	/* +1: `copy` is an OWNED family (plan §15.2) — immutable */
}


@end
