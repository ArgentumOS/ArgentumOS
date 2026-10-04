/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSUserDefaults.m — the settings store. The design is in NSUserDefaults.h; what is HERE is the two things
 * a reader should be able to check against the header's promises: WHERE the files are, and WHAT happens
 * when one of them is not what it should be.
 *
 * THREE FILES PER DOMAIN NAME, MERGED LOW TO HIGH (SHARED, USER, SYSTEM), and the merge is CACHED per
 * domain and INVALIDATED BY A WRITE. The cache is not a performance detail to skip: `-objectForKey:` walks
 * the search list, so without it a single lookup would re-parse every file of every domain in it.
 *
 * A FILE THAT EXISTS AND WILL NOT PARSE IS AN ERROR, NOT AN EMPTY DOMAIN — AND THAT IS THE ONE BEHAVIOUR
 * HERE THAT IS NOT OBVIOUS. Treating a corrupt `.plist` as "no settings" is the friendly reading and it is
 * the dangerous one: the next write would then write the domain back out with every key the caller did not
 * happen to set MISSING, i.e. the store would silently delete a user's settings on the way past. So a
 * malformed file RAISES, and the message names the path and the parser's own reason.
 *
 * `NSPropertyListSerialization` IS THE ONLY SERIALISER, and the store is XML v1.0 — the format that TYPES
 * its values. Nothing here reads or writes the `Configuration/` directory's `.conf` files: those are
 * libconfig's, they are the OS's own domains rather than app settings, and the two mechanisms do not
 * touch the same files (the extensions differ, which is what keeps this true).
 */

#import <Foundation/NSUserDefaults.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSData.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSException.h>
#import <Foundation/NSFileManager.h>
#import <Foundation/NSLock.h>
#import <Foundation/NSNotification.h>
#import <Foundation/NSNotificationCenter.h>
#import <Foundation/NSNumber.h>
#import <Foundation/NSProcessInfo.h>
#import <Foundation/NSPropertyListSerialization.h>
#import <Foundation/NSURL.h>

#include <pthread.h>
#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ---- the constants ------------------------------------------------------- */

NSString *const NSArgumentDomain = @"NSArgumentDomain";
NSString *const NSGlobalDomain = @"NSGlobalDomain";
NSString *const NSRegistrationDomain = @"NSRegistrationDomain";
NSString *const NSUserDefaultsDidChangeNotification = @"NSUserDefaultsDidChangeNotification";

/* §62.43: the legacy localization keys, and the ubiquity notifications whose absence is stated in the header.
 * THE VALUE IS THE NAME, which is the convention Apple's own keys keep - and the reason a probe can check it. */
NSString *const NSAMPMDesignation = @"NSAMPMDesignation";
NSString *const NSCurrencySymbol = @"NSCurrencySymbol";
NSString *const NSDateFormatString = @"NSDateFormatString";
NSString *const NSDateTimeOrdering = @"NSDateTimeOrdering";
NSString *const NSDecimalDigits = @"NSDecimalDigits";
NSString *const NSDecimalSeparator = @"NSDecimalSeparator";
NSString *const NSEarlierTimeDesignations = @"NSEarlierTimeDesignations";
NSString *const NSHourNameDesignations = @"NSHourNameDesignations";
NSString *const NSInternationalCurrencyString = @"NSInternationalCurrencyString";
NSString *const NSLaterTimeDesignations = @"NSLaterTimeDesignations";
NSString *const NSMonthNameArray = @"NSMonthNameArray";
NSString *const NSNegativeCurrencyFormatString = @"NSNegativeCurrencyFormatString";
NSString *const NSNextDayDesignations = @"NSNextDayDesignations";
NSString *const NSNextNextDayDesignations = @"NSNextNextDayDesignations";
NSString *const NSPositiveCurrencyFormatString = @"NSPositiveCurrencyFormatString";
NSString *const NSPriorDayDesignations = @"NSPriorDayDesignations";
NSString *const NSShortDateFormatString = @"NSShortDateFormatString";
NSString *const NSShortMonthNameArray = @"NSShortMonthNameArray";
NSString *const NSShortTimeDateFormatString = @"NSShortTimeDateFormatString";
NSString *const NSShortWeekDayNameArray = @"NSShortWeekDayNameArray";
NSString *const NSThisDayDesignations = @"NSThisDayDesignations";
NSString *const NSThousandsSeparator = @"NSThousandsSeparator";
NSString *const NSTimeDateFormatString = @"NSTimeDateFormatString";
NSString *const NSTimeFormatString = @"NSTimeFormatString";
NSString *const NSWeekDayNameArray = @"NSWeekDayNameArray";
NSString *const NSYearMonthWeekDesignations = @"NSYearMonthWeekDesignations";
NSString *const NSUbiquitousUserDefaultsCompletedInitialSyncNotification = @"NSUbiquitousUserDefaultsCompletedInitialSyncNotification";
NSString *const NSUbiquitousUserDefaultsDidChangeAccountsNotification = @"NSUbiquitousUserDefaultsDidChangeAccountsNotification";
NSString *const NSUbiquitousUserDefaultsNoCloudAccountNotification = @"NSUbiquitousUserDefaultsNoCloudAccountNotification";

NSString *const NSUserDefaultsSizeLimitExceededNotification = @"NSUserDefaultsSizeLimitExceededNotification";

/* ONE MEBIBYTE PER DOMAIN. Apple documents the notification and publishes no number, so this is a rule of
 * ours rather than a measurement of theirs — and because it is ours it can say WHY it is this size: a
 * defaults domain is settings, and a settings file that needs more than a mebibyte has picked the wrong
 * store. Crossing it NOTIFIES AND STILL SAVES (see the header). */
const NSUInteger NSUserDefaultsMaximumDomainSize = 1048576u;

/* ---- where the files are ------------------------------------------------- */

enum {
	FN_SCOPE_USER = 0,	/* the scope a WRITE lands in */
	FN_SCOPE_SHARED = 1,	/* the shipped default */
	FN_SCOPE_SYSTEM = 2	/* the administrator's value: the highest precedence */
};

static NSString *fn_config_root(void)
{
	const char *root = getenv("FNX_CONFIG_ROOT");

	return [NSString stringWithUTF8String:(root != NULL ? root : "")];
}

/* The same order libconfig uses, because these are the same directories and an app's settings must not
 * change owner depending on which library asked. */
static NSString *fn_config_user(void)
{
	struct passwd *pw = getpwuid(getuid());
	const char *user;

	if (pw != NULL && pw->pw_name != NULL && pw->pw_name[0] != '\0') {
		return [NSString stringWithUTF8String:pw->pw_name];
	}
	user = getenv("USER");
	if (user != NULL && user[0] != '\0') {
		return [NSString stringWithUTF8String:user];
	}
	return @"root";
}

static NSString *fn_scope_directory(int scope)
{
	NSString *root = fn_config_root();

	switch (scope) {
	case FN_SCOPE_USER:
		return [NSString stringWithFormat:@"%@/Users/%@/Configuration", root, fn_config_user()];
	case FN_SCOPE_SHARED:
		return [NSString stringWithFormat:@"%@/Shared/Configuration", root];
	default:
		return [NSString stringWithFormat:@"%@/System/Configuration", root];
	}
}

static NSString *fn_domain_path(int scope, NSString *domain)
{
	return [NSString stringWithFormat:@"%@/%@.plist", fn_scope_directory(scope), domain];
}

/* A DOMAIN NAME BECOMES A FILE NAME, so it may not contain a separator or walk upwards: a caller passing
 * "../../etc/passwd" must not be able to name a file outside the Configuration/ directory. The rule is
 * libconfig's, narrowed to the one shape that matters here — no '/', no leading '.', no empty name. */
static void fn_validate_domain(NSString *domain)
{
	const char *name;

	if (domain == nil || [domain length] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSUserDefaults: a domain name must not be empty"];
	}
	name = [domain UTF8String];
	if (name[0] == '.' || strchr(name, '/') != NULL) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSUserDefaults: %@ is not a usable domain name (no '/', no leading '.')",
				   domain];
	}
}

static void fn_validate_key(NSString *key)
{
	if (key == nil || [key length] == 0) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSUserDefaults: a settings key must be a non-empty string"];
	}
}

/* ---- reading and writing one file ---------------------------------------- */

/* NULL when the file is not there; the PARSED DICTIONARY when it is; and a RAISE when it is there and
 * cannot be read (see the file's header comment for why that is the choice). */
static NSDictionary *fn_load_plist(NSString *path)
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSData *data;
	NSError *error = nil;
	id plist;

	if (![fm fileExistsAtPath:path]) {
		return nil;
	}
	data = [NSData dataWithContentsOfFile:path];
	if (data == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSUserDefaults: %@ exists and cannot be read", path];
	}
	plist = [NSPropertyListSerialization propertyListWithData:data
							  options:NSPropertyListImmutable
							   format:NULL
							    error:&error];
	if (plist == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSUserDefaults: %@ is not a readable property list: %@", path, error];
	}
	if (![plist isKindOfClass:[NSDictionary class]]) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSUserDefaults: %@ must hold a dictionary, not a %@",
				   path, NSStringFromClass([plist class])];
	}
	return plist;
}

/* ---- the coercions Apple documents --------------------------------------- */

/* Apple's examples are the rule: the numbers 1 and 1.0 are true, and so are the strings "true", "YES" and
 * "1". Case is not part of the contract, so the comparison folds it rather than enumerating spellings. */
static BOOL fn_boolean_value(id value)
{
	if ([value isKindOfClass:[NSNumber class]]) {
		return [(NSNumber *)value boolValue];
	}
	if ([value isKindOfClass:[NSString class]]) {
		NSString *folded = [(NSString *)value lowercaseString];

		return [folded isEqualToString:@"true"] ||
		       [folded isEqualToString:@"yes"] ||
		       [folded isEqualToString:@"1"];
	}
	return NO;
}

/* ---- the object ---------------------------------------------------------- */

/* `-NAME VALUE` and `-NAME=VALUE`, from THIS process's own argument vector through NSProcessInfo (which
 * reads /proc/self/cmdline). The keys are the argument names WITHOUT the leading '-', and the values are
 * STRINGS: the domain is a set of command-line overrides, and a caller who wants a number asks for it with
 * `-integerForKey:`, which coerces. */
static void fn_parse_arguments(NSMutableDictionary *into)
{
	NSArray *arguments = [[NSProcessInfo processInfo] arguments];
	NSUInteger i, n = [arguments count];

	for (i = 1; i < n; i++) {
		NSString *argument = [arguments objectAtIndex:i];
		NSString *key;
		NSRange equals;

		if ([argument length] < 2 || ![argument hasPrefix:@"-"]) {
			continue;
		}
		argument = [argument substringFromIndex:1];
		equals = [argument rangeOfString:@"="];
		if (equals.location != NSNotFound) {
			key = [argument substringToIndex:equals.location];
			if ([key length] > 0) {
				[into setObject:[argument substringFromIndex:equals.location + 1]
					 forKey:key];
			}
			continue;
		}
		key = argument;
		/* A VALUE THAT BEGINS WITH '-' IS THE NEXT SETTING, not this one's value: `-a -b` is two keys
		 * with the empty value, and reading it as `a = "-b"` would swallow a setting. */
		if (i + 1 < n && [[arguments objectAtIndex:i + 1] length] > 0 &&
		    ![[arguments objectAtIndex:i + 1] hasPrefix:@"-"]) {
			[into setObject:[arguments objectAtIndex:i + 1] forKey:key];
			i++;
		} else {
			[into setObject:@"" forKey:key];
		}
	}
}

@implementation NSUserDefaults

static NSUserDefaults *fn_standard_defaults = nil;
static pthread_once_t fn_standard_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t fn_standard_lock;

static void fn_make_standard_lock(void)
{
	pthread_mutex_init(&fn_standard_lock, NULL);
}

+ (NSUserDefaults *)standardUserDefaults
{
	/* §63.200: A LOCK RATHER THAN A ONE-SHOT, because +resetStandardUserDefaults has to be able to drop the
	 * cached instance and let the next caller build a fresh one that re-reads from disk. */
	pthread_once(&fn_standard_once, fn_make_standard_lock);
	pthread_mutex_lock(&fn_standard_lock);
	if (fn_standard_defaults == nil) {
		fn_standard_defaults = [[NSUserDefaults alloc] init];
	}
	{
		NSUserDefaults *answer = [[fn_standard_defaults retain] autorelease];

		pthread_mutex_unlock(&fn_standard_lock);
		return answer;
	}
}

+ (void)resetStandardUserDefaults
{
	pthread_once(&fn_standard_once, fn_make_standard_lock);
	pthread_mutex_lock(&fn_standard_lock);
	[fn_standard_defaults release];
	fn_standard_defaults = nil;
	pthread_mutex_unlock(&fn_standard_lock);
}

- (instancetype)init
{
	return [self initWithSuiteName:nil];
}

- (instancetype)initWithSuiteName:(NSString *)suitename
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	if (suitename != nil && [suitename length] > 0) {
		fn_validate_domain(suitename);
		_appDomain = [suitename copy];
	} else {
		_appDomain = NSGlobalDomain;
	}
	_persistent = [[NSMutableDictionary alloc] init];
	_userFiles = [[NSMutableDictionary alloc] init];
	_volatileDomains = [[NSMutableDictionary alloc] init];
	_volatileNames = [[NSMutableArray alloc] init];
	_registration = [[NSMutableDictionary alloc] init];
	_argument = [[NSMutableDictionary alloc] init];
	_suites = [[NSMutableArray alloc] init];
	_lock = [[NSLock alloc] init];

	/* The two volatile domains exist on every object from the start, which is what makes
	 * `-volatileDomainNames` answer with them and the search list well formed before any registration. */
	[_volatileDomains setObject:_argument forKey:NSArgumentDomain];
	[_volatileDomains setObject:_registration forKey:NSRegistrationDomain];
	[_volatileNames addObject:NSArgumentDomain];
	[_volatileNames addObject:NSRegistrationDomain];

	fn_parse_arguments(_argument);
	return self;
}


/* ---- the caches, unlocked: every caller below holds _lock ----------------- */

- (NSDictionary *)fn_mergedDomainNamed:(NSString *)domain
{
	NSDictionary *cached = [_persistent objectForKey:domain];
	static const int order[3] = { FN_SCOPE_SHARED, FN_SCOPE_USER, FN_SCOPE_SYSTEM };
	NSMutableDictionary *merged;
	int i;

	if (cached != nil) {
		return cached;
	}
	merged = [NSMutableDictionary dictionary];
	for (i = 0; i < 3; i++) {
		NSDictionary *file = fn_load_plist(fn_domain_path(order[i], domain));

		if (file != nil) {
			[merged addEntriesFromDictionary:file];
		}
	}
	[_persistent setObject:merged forKey:domain];
	return merged;
}

- (NSDictionary *)fn_dictionaryForDomainName:(NSString *)name
{
	NSDictionary *volatileDomain = [_volatileDomains objectForKey:name];

	if (volatileDomain != nil) {
		return volatileDomain;
	}
	return [self fn_mergedDomainNamed:name];
}

- (NSMutableArray *)fn_searchList
{
	NSMutableArray *list = [NSMutableArray array];
	NSUInteger i, n = [_volatileNames count];

	[list addObject:NSArgumentDomain];
	for (i = 0; i < n; i++) {
		NSString *name = [_volatileNames objectAtIndex:i];

		if ([name isEqualToString:NSArgumentDomain] ||
		    [name isEqualToString:NSRegistrationDomain]) {
			continue;
		}
		[list addObject:name];
	}
	[list addObject:_appDomain];
	for (i = 0; i < [_suites count]; i++) {
		[list addObject:[_suites objectAtIndex:i]];
	}
	[list addObject:NSRegistrationDomain];
	return list;
}

- (id)fn_rawObjectForKey:(NSString *)key
{
	NSArray *list = [self fn_searchList];
	NSUInteger i, n = [list count];

	for (i = 0; i < n; i++) {
		NSDictionary *domain = [self fn_dictionaryForDomainName:[list objectAtIndex:i]];
		id value;

		if (domain == nil) {
			continue;
		}
		value = [domain objectForKey:key];
		if (value != nil) {
			return value;
		}
	}
	return nil;
}

/* The USER scope's own contents: the WRITE TARGET. It is kept SEPARATE from the merged view on purpose —
 * writing the merged view back would copy every SHARED and SYSTEM value into the user's file, so the next
 * edit of a shipped default would silently become a user override. */
- (NSMutableDictionary *)fn_userFileNamed:(NSString *)domain
{
	NSMutableDictionary *file = [_userFiles objectForKey:domain];

	if (file != nil) {
		return file;
	}
	file = [NSMutableDictionary dictionary];
	{
		NSDictionary *onDisk = fn_load_plist([self fnUserDomainPath:domain]);

		if (onDisk != nil) {
			[file addEntriesFromDictionary:onDisk];
		}
	}
	[_userFiles setObject:file forKey:domain];
	return file;
}

- (void)fn_writeUserFile:(NSDictionary *)contents forDomain:(NSString *)domain
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *directory = [self fnUserScopeDirectory];
	NSString *path = [self fnUserDomainPath:domain];
	NSData *data;
	NSError *error = nil;

	error = nil;
	if (![fm fileExistsAtPath:directory] &&
	    ![fm createDirectoryAtPath:directory
	  withIntermediateDirectories:YES
			   attributes:nil
				error:&error]) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSUserDefaults: cannot create %@: %@", directory, error];
	}
	data = [NSPropertyListSerialization dataWithPropertyList:contents
							  format:NSPropertyListXMLFormat_v1_0
							 options:0
							   error:&error];
	if (data == nil) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSUserDefaults: %@ cannot be written as a property list: %@",
				   domain, error];
	}
	if (![data writeToFile:path atomically:YES]) {
		[NSException raise:NSInternalInconsistencyException
			    format:@"NSUserDefaults: cannot write %@", path];
	}
	/* The merged view for this domain just changed, and only for this domain. */
	[_persistent removeObjectForKey:domain];

	if ([data length] > NSUserDefaultsMaximumDomainSize) {
		[[NSNotificationCenter defaultCenter]
			postNotificationName:NSUserDefaultsSizeLimitExceededNotification
				      object:self];
	}
}

- (void)fn_noteChange
{
	[[NSNotificationCenter defaultCenter]
		postNotificationName:NSUserDefaultsDidChangeNotification
			      object:self];
}

/* Every write lands in the APP DOMAIN — Apple's contract, and the reason `-addSuiteNamed:` is a way to READ
 * another domain rather than to write one. */
- (void)fn_setRawObject:(id)value forKey:(NSString *)key
{
	NSMutableDictionary *file;

	[_lock lock];
	@try {
		file = [self fn_userFileNamed:_appDomain];
		[file setObject:value forKey:key];
		[self fn_writeUserFile:file forDomain:_appDomain];
		[self fn_noteChange];
	} @finally {
		[_lock unlock];
	}
}

/* ---- registration -------------------------------------------------------- */

- (void)registerDefaults:(NSDictionary *)registrationDictionary
{
	[_lock lock];
	@try {
		[_registration addEntriesFromDictionary:registrationDictionary];
	} @finally {
		[_lock unlock];
	}
}

/* ---- reading ------------------------------------------------------------ */

- (id)objectForKey:(NSString *)defaultName
{
	id value;

	fn_validate_key(defaultName);
	[_lock lock];
	@try {
		value = [[[self fn_rawObjectForKey:defaultName] copy] autorelease];
	} @finally {
		[_lock unlock];
	}
	/* Apple's contract: the answer is IMMUTABLE even for a key that was set to a mutable value. The copy
	 * above is what makes that true for a value a caller set and then kept mutating. */
	return value;
}

- (NSString *)stringForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	return [value isKindOfClass:[NSString class]] ? (NSString *)value : nil;
}

- (NSArray *)arrayForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	return [value isKindOfClass:[NSArray class]] ? (NSArray *)value : nil;
}

- (NSArray *)stringArrayForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];
	NSUInteger i, n;

	if (![value isKindOfClass:[NSArray class]]) {
		return nil;
	}
	n = [(NSArray *)value count];
	for (i = 0; i < n; i++) {
		if (![[(NSArray *)value objectAtIndex:i] isKindOfClass:[NSString class]]) {
			return nil;
		}
	}
	return (NSArray *)value;
}

- (NSDictionary *)dictionaryForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	return [value isKindOfClass:[NSDictionary class]] ? (NSDictionary *)value : nil;
}

- (NSData *)dataForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	return [value isKindOfClass:[NSData class]] ? (NSData *)value : nil;
}

- (NSURL *)URLForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	/* THE STRING FORM ONLY — an archived URL is data, and decoding that needs the keyed unarchiver's
	 * class allowlist, which is not decided. Refusing beats half-decoding. */
	if (![value isKindOfClass:[NSString class]]) {
		return nil;
	}
	return [NSURL URLWithString:(NSString *)value];
}

- (BOOL)boolForKey:(NSString *)defaultName
{
	return fn_boolean_value([self objectForKey:defaultName]);
}

- (NSInteger)integerForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	if ([value isKindOfClass:[NSNumber class]]) {
		return [(NSNumber *)value integerValue];
	}
	if ([value isKindOfClass:[NSString class]]) {
		return [(NSString *)value integerValue];
	}
	return 0;
}

- (float)floatForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	if ([value isKindOfClass:[NSNumber class]]) {
		return [(NSNumber *)value floatValue];
	}
	if ([value isKindOfClass:[NSString class]]) {
		return [(NSString *)value floatValue];
	}
	return 0.0f;
}

- (double)doubleForKey:(NSString *)defaultName
{
	id value = [self objectForKey:defaultName];

	if ([value isKindOfClass:[NSNumber class]]) {
		return [(NSNumber *)value doubleValue];
	}
	if ([value isKindOfClass:[NSString class]]) {
		return [(NSString *)value doubleValue];
	}
	return 0.0;
}

- (NSDictionary *)dictionaryRepresentation
{
	NSMutableDictionary *union_ = [NSMutableDictionary dictionary];
	NSArray *list;
	NSUInteger i;

	[_lock lock];
	@try {
		list = [self fn_searchList];
		/* LOWEST PRECEDENCE FIRST, so the last write wins and the answer is what a lookup would give. */
		for (i = [list count]; i > 0; i--) {
			NSDictionary *domain =
				[self fn_dictionaryForDomainName:[list objectAtIndex:i - 1]];

			if (domain != nil) {
				[union_ addEntriesFromDictionary:domain];
			}
		}
	} @finally {
		[_lock unlock];
	}
	return union_;
}

/* ---- writing ------------------------------------------------------------ */

- (void)setObject:(id)value forKey:(NSString *)defaultName
{
	fn_validate_key(defaultName);
	if (value == nil) {
		[self removeObjectForKey:defaultName];
		return;
	}
	/* A VALUE THE STORE CANNOT READ BACK IS REFUSED HERE rather than written and lost. The test is a
	 * one-key dictionary, because the writer's question is about the whole document. */
	if (![NSPropertyListSerialization
			propertyList:[NSDictionary dictionaryWithObject:value forKey:defaultName]
			   isValidForFormat:NSPropertyListXMLFormat_v1_0]) {
		[NSException raise:NSInvalidArgumentException
			    format:@"NSUserDefaults: %@ is not a property-list object, so it cannot be stored "
				   "under %@", NSStringFromClass([value class]), defaultName];
	}
	[self fn_setRawObject:value forKey:defaultName];
}

- (void)setBool:(BOOL)value forKey:(NSString *)defaultName
{
	[self setObject:[NSNumber numberWithBool:value] forKey:defaultName];
}

- (void)setInteger:(NSInteger)value forKey:(NSString *)defaultName
{
	[self setObject:[NSNumber numberWithInteger:value] forKey:defaultName];
}

- (void)setFloat:(float)value forKey:(NSString *)defaultName
{
	[self setObject:[NSNumber numberWithFloat:value] forKey:defaultName];
}

- (void)setDouble:(double)value forKey:(NSString *)defaultName
{
	[self setObject:[NSNumber numberWithDouble:value] forKey:defaultName];
}

- (void)setURL:(NSURL *)url forKey:(NSString *)defaultName
{
	if (url == nil) {
		[self removeObjectForKey:defaultName];
		return;
	}
	[self setObject:[url absoluteString] forKey:defaultName];
}

- (void)removeObjectForKey:(NSString *)defaultName
{
	fn_validate_key(defaultName);
	[_lock lock];
	@try {
		NSMutableDictionary *file = [self fn_userFileNamed:_appDomain];

		if ([file objectForKey:defaultName] == nil) {
			return;	/* nothing was there, so nothing changed and nothing is announced */
		}
		[file removeObjectForKey:defaultName];
		[self fn_writeUserFile:file forDomain:_appDomain];
		[self fn_noteChange];
	} @finally {
		[_lock unlock];
	}
}

/* ---- the search list, as a caller may edit it ---------------------------- */

- (void)addSuiteNamed:(NSString *)suiteName
{
	fn_validate_domain(suiteName);
	[_lock lock];
	@try {
		if (![_suites containsObject:suiteName]) {
			[_suites addObject:suiteName];
		}
	} @finally {
		[_lock unlock];
	}
}

- (void)removeSuiteNamed:(NSString *)suiteName
{
	[_lock lock];
	@try {
		[_suites removeObject:suiteName];
	} @finally {
		[_lock unlock];
	}
}

- (NSArray *)volatileDomainNames
{
	NSArray *names;

	[_lock lock];
	@try {
		names = [[_volatileNames copy] autorelease];
	} @finally {
		[_lock unlock];
	}
	return names;
}

/* ---- whole domains ------------------------------------------------------ */

- (NSDictionary *)persistentDomainForName:(NSString *)domainName
{
	NSDictionary *domain;

	fn_validate_domain(domainName);
	[_lock lock];
	@try {
		if ([_volatileDomains objectForKey:domainName] != nil) {
			domain = nil;	/* Apple: a volatile domain is not a persistent one */
		} else {
			domain = [self fn_mergedDomainNamed:domainName];
			if ([domain count] == 0) {
				domain = nil;	/* Apple: "if the domain contains no keys ... nil" */
			}
		}
	} @finally {
		[_lock unlock];
	}
	return domain;
}

- (void)setPersistentDomain:(NSDictionary *)domain forName:(NSString *)domainName
{
	NSMutableDictionary *replacement;
	NSEnumerator *keys;
	NSString *key;

	fn_validate_domain(domainName);
	/* Every key and every value is checked BEFORE anything is written: a domain that would fail to
	 * serialise halfway through is not allowed to leave a half-written file. */
	keys = [domain keyEnumerator];
	while ((key = [keys nextObject]) != nil) {
		fn_validate_key(key);
		if (![NSPropertyListSerialization
				propertyList:[NSDictionary dictionaryWithObject:
						[domain objectForKey:key] forKey:key]
				   isValidForFormat:NSPropertyListXMLFormat_v1_0]) {
			[NSException raise:NSInvalidArgumentException
				    format:@"NSUserDefaults: the value for %@ is not a property-list object",
					   key];
		}
	}
	replacement = [[[NSMutableDictionary alloc] initWithDictionary:domain] autorelease];
	[_lock lock];
	@try {
		[_userFiles setObject:replacement forKey:domainName];
		[self fn_writeUserFile:replacement forDomain:domainName];
		[self fn_noteChange];
	} @finally {
		[_lock unlock];
	}
}

- (void)removePersistentDomainForName:(NSString *)domainName
{
	NSFileManager *fm = [NSFileManager defaultManager];
	NSString *path = [self fnUserDomainPath:domainName];
	NSError *error = nil;

	fn_validate_domain(domainName);
	[_lock lock];
	@try {
		[_userFiles removeObjectForKey:domainName];
		if ([fm fileExistsAtPath:path] && ![fm removeItemAtPath:path error:&error]) {
			[NSException raise:NSInternalInconsistencyException
				    format:@"NSUserDefaults: cannot remove %@: %@", path, error];
		}
		[_persistent removeObjectForKey:domainName];
		[self fn_noteChange];
	} @finally {
		[_lock unlock];
	}
}

- (NSDictionary *)volatileDomainForName:(NSString *)domainName
{
	NSDictionary *domain;

	[_lock lock];
	@try {
		domain = [_volatileDomains objectForKey:domainName];
		if (domain == nil) {
			domain = [NSDictionary dictionary];
		}
	} @finally {
		[_lock unlock];
	}
	return domain;
}

- (void)setVolatileDomain:(NSDictionary *)domain forName:(NSString *)domainName
{
	NSMutableDictionary *replacement;

	fn_validate_domain(domainName);
	replacement = [[[NSMutableDictionary alloc] initWithDictionary:domain] autorelease];
	[_lock lock];
	@try {
		/* NSArgumentDomain and NSRegistrationDomain are volatile domains like any other once they are
		 * named, so setting one replaces it and the object stops pointing at the two built at -init. */
		[_volatileDomains setObject:replacement forKey:domainName];
		if (![_volatileNames containsObject:domainName]) {
			[_volatileNames addObject:domainName];
		}
		if ([domainName isEqualToString:NSArgumentDomain]) {
			[replacement retain];
			[_argument release];
			_argument = replacement;
		} else if ([domainName isEqualToString:NSRegistrationDomain]) {
			[replacement retain];
			[_registration release];
			_registration = replacement;
		}
	} @finally {
		[_lock unlock];
	}
}

- (void)removeVolatileDomainForName:(NSString *)domainName
{
	[_lock lock];
	@try {
		[_volatileDomains removeObjectForKey:domainName];
		[_volatileNames removeObject:domainName];
	} @finally {
		[_lock unlock];
	}
}

/* ---- managed keys ------------------------------------------------------- */

- (BOOL)objectIsForcedForKey:(NSString *)key inDomain:(NSString *)domain
{
	NSDictionary *system;

	fn_validate_key(key);
	fn_validate_domain(domain);
	[_lock lock];
	@try {
		system = fn_load_plist(fn_domain_path(FN_SCOPE_SYSTEM, domain));
	} @finally {
		[_lock unlock];
	}
	return system != nil && [system objectForKey:key] != nil;
}

- (BOOL)objectIsForcedForKey:(NSString *)key
{
	return [self objectIsForcedForKey:key inDomain:_appDomain];
}

/* THE LIBRARY IS MRC, SO OWNERSHIP IS SPELLED OUT. Every ivar below is +1 in -initWithSuiteName: (the two
 * volatile dictionaries it also aliases in _volatileDomains are retained there as well), and this is where
 * they are given back. A defaults object is created and destroyed routinely — the probe makes a dozen —
 * so this is not a formality. */
- (void)dealloc
{
	if (_appDomain != NSGlobalDomain) {	/* the default IS the constant, which must not be released */
		[_appDomain release];
	}
	[_persistent release];
	[_userFiles release];
	[_volatileDomains release];
	[_volatileNames release];
	[_registration release];
	[_argument release];
	[_suites release];
	[_lock release];
	[super dealloc];
}


- (instancetype)initWithUser:(NSString *)userName
{
	self = [self initWithSuiteName:nil];
	if (self != nil) {
		_userName = [userName copy];
	}
	return self;
}

- (NSString *)fnUserScopeDirectory
{
	if (_userName != nil && [_userName length] > 0) {
		return [NSString stringWithFormat:@"%@/Users/%@/Configuration", fn_config_root(), _userName];
	}
	return fn_scope_directory(FN_SCOPE_USER);
}

- (NSString *)fnUserDomainPath:(NSString *)domain
{
	return [NSString stringWithFormat:@"%@/%@.plist", [self fnUserScopeDirectory], domain];
}

- (NSArray *)persistentDomainNames
{
	/* THE DOMAINS THAT EXIST ON DISK, in the scope order the reads use (user, shared, system), deduped. */
	NSMutableArray *names = [NSMutableArray array];
	NSFileManager *manager = [NSFileManager defaultManager];
	int scopes[3] = { FN_SCOPE_USER, FN_SCOPE_SHARED, FN_SCOPE_SYSTEM };
	int i;

	for (i = 0; i < 3; i++) {
		NSString *directory = (scopes[i] == FN_SCOPE_USER) ? [self fnUserScopeDirectory]
								   : fn_scope_directory(scopes[i]);
		NSArray *entries = [manager contentsOfDirectoryAtPath:directory error:NULL];
		NSUInteger j;

		for (j = 0; entries != nil && j < [entries count]; j++) {
			NSString *entry = [entries objectAtIndex:j];

			if ([entry hasSuffix:@".plist"]) {
				NSString *domain = [entry substringToIndex:[entry length] - 6];

				if (![names containsObject:domain]) {
					[names addObject:domain];
				}
			}
		}
	}
	return names;
}

- (BOOL)synchronize
{
	/* EVERY WRITE ALREADY REACHED THE DISK BEFORE IT RETURNED, so this door WRITES the user files this
	 * instance holds and then CONFIRMS each one is there — which is the promise, and it is checkable rather
	 * than a wait for a background flush this tree does not have. */
	NSArray *domains;
	BOOL ok = YES;
	NSUInteger i;

	[_lock lock];
	domains = [[_userFiles allKeys] retain];
	[_lock unlock];
	for (i = 0; i < [domains count]; i++) {
		NSString *domain = [domains objectAtIndex:i];
		NSDictionary *contents;

		[_lock lock];
		contents = [[[_userFiles objectForKey:domain] retain] autorelease];
		[_lock unlock];
		if (contents != nil) {
			[self fn_writeUserFile:contents forDomain:domain];
			if (![[NSFileManager defaultManager] fileExistsAtPath:[self fnUserDomainPath:domain]]) {
				ok = NO;
			}
		}
	}
	[domains release];
	return ok;
}

@end
