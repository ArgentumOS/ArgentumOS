/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * foundation_defaults — W5's acceptance (docs/design/foundation-plan.md §41). ONE unit, importing only
 * <Foundation/Foundation.h> plus <stdlib.h>/<unistd.h>/<pwd.h> for the scratch root and the user name.
 *
 * MUST BE LAUNCHED WITH `-ProbeArgument from-argv`: the argument domain is the one part of the search list
 * that is a fact about THIS PROCESS rather than about the store, and a check that could not see the launcher
 * would be asserting nothing. The case does that; a hand run without it fails `defaults-argument-domain`,
 * which is the check working.
 *
 * EVERY CHECK THAT TOUCHES DISK RUNS UNDER ITS OWN FNX_CONFIG_ROOT (the switch libconfig already has), so
 * the store under test is a tree in /System/Temporary Files and the image's own Configuration/ is never a
 * participant — the LAST check is the exception and says so.
 *
 *   defaults-registration        registerDefaults: answers for an absent key, and writes NO FILE (the
 *                                registration domain is volatile by contract)
 *   defaults-search-order        a key in two domains answers from the EARLIER one, and moving it out of
 *                                the earlier one reveals the later — the order is not a coincidence
 *   defaults-persist-round-trip  a value set on one object is read by a SECOND object over the same root,
 *                                which is the only way to tell a store from a dictionary
 *   defaults-types-round-trip    string, integer, double, bool, date, data, array, dictionary, URL — each
 *                                read back through a fresh object and each compared with isEqual:
 *   defaults-absent-answers      every absent key answers its documented ZERO: NO / 0 / 0.0 / nil
 *   defaults-coercion            Apple's documented coercion: the NUMBER 1.0, and the STRINGS "true",
 *                                "YES" and "1", are true; a string coerces for -integerForKey: too
 *   defaults-mutation-after-set  the caller keeps mutating the object it set, and a fresh read is the
 *                                value as it was WRITTEN — a store, not a live alias
 *   defaults-immutable-read      the answer to -stringForKey: is not an NSMutableString, which is Apple's
 *                                "the returned object is immutable, even if you set a mutable value"
 *   defaults-scope-precedence    SHARED, USER and SYSTEM files for one domain, each read through a FRESH
 *                                object: SYSTEM wins, then USER, then SHARED — checked by removing one
 *                                file at a time, so precedence is measured rather than asserted
 *   defaults-forced-key          -objectIsForcedForKey: is YES exactly when the SYSTEM file supplies it
 *   defaults-suites              a suite is found by lookup, removed by -removeSuiteNamed:, and is NOT
 *                                where a write lands — the suite's own file is read to prove it
 *   defaults-volatile-domains    the two built-in volatile names, plus one the caller adds and removes
 *   defaults-argument-domain     -ProbeArgument from-argv, and a key the launcher did NOT pass is absent
 *   defaults-dictionary-rep      the union of the search list, with the search order deciding a shared key
 *   defaults-persistent-domain   set/replace/remove a whole domain, and that a SHARED file alone counts
 *   defaults-change-notification DidChange fires on a write and NOT on removing a key that was absent
 *   defaults-refusals            a non-property-list value, an empty key and a domain name with a '/' all
 *                                RAISE rather than being written
 *   defaults-corrupt-file        a file that exists and will not parse RAISES: treating it as "empty" would
 *                                let the next write delete every setting it did not happen to set
 *   defaults-size-limit          crossing NSUserDefaultsMaximumDomainSize posts the notification AND still
 *                                saves the value
 *   defaults-store-location      THE ONE CHECK AT THE REAL ROOT: the file lands at
 *                                /Users/<user>/Configuration/<domain>.plist, and is removed afterwards
 */

#import <Foundation/Foundation.h>

#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#define PROBE_ROOT "/System/Temporary Files/nsuserdefaults-probe"

static int okc, failc;

static void check(const char *name, int ok, NSString * _Nullable detail)
{
	if (ok) {
		okc++;
		printf("FOUNDATION-DEFAULTS %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-DEFAULTS %s FAIL %s\n", name,
		       detail != nil ? [detail UTF8String] : "");
	}
}

static NSString *scratch_path(NSString *relative)
{
	return [NSString stringWithFormat:@"%s/%@", PROBE_ROOT, relative];
}

static NSString *user_name(void)
{
	struct passwd *pw = getpwuid(getuid());

	if (pw != NULL && pw->pw_name != NULL && pw->pw_name[0] != '\0') {
		return [NSString stringWithUTF8String:pw->pw_name];
	}
	return @"root";
}

/* THE SCOPE'S OWN DIRECTORY, and the USER one carries the ACCOUNT NAME. The first version of this probe
 * planted USER files at `Users/Configuration` — where the class never looks — so the precedence check was
 * reading SHARED and the suite check could not fail. The path is part of the claim. */
static NSString *scope_directory(NSString *scope)
{
	if ([scope isEqualToString:@"Users"]) {
		return [NSString stringWithFormat:@"%s/Users/%@/Configuration", PROBE_ROOT, user_name()];
	}
	return [NSString stringWithFormat:@"%s/%@/Configuration", PROBE_ROOT, scope];
}

/* Plant a scope's file by hand: the scope axis is the STORE's business, and a probe that could only write
 * through the class under test could not tell a merge from a cache. */
static BOOL plant_scope_file(NSString *scope, NSString *domain, NSDictionary *contents)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *directory = scope_directory(scope);
	NSString *path = [NSString stringWithFormat:@"%@/%@.plist", directory, domain];

	if (![manager createDirectoryAtPath:directory
		withIntermediateDirectories:YES
			 attributes:nil
			      error:NULL]) {
		return NO;
	}
	return [contents writeToFile:path atomically:YES];
}

static NSString *scope_file_path(NSString *scope, NSString *domain)
{
	return [NSString stringWithFormat:@"%@/%@.plist", scope_directory(scope), domain];
}

/* THE OBSERVER. It counts by notification NAME, because two notifications are checked and a single "did
 * anything arrive" boolean could not tell them apart. */
@interface FnDefaultsObserver : NSObject
{
	NSMutableArray *_names;
}
- (void)note:(NSNotification *)notification;
- (NSUInteger)countOf:(NSString *)name;
@end

@implementation FnDefaultsObserver

- (id)init
{
	self = [super init];
	if (self != nil) {
		_names = [[NSMutableArray alloc] init];
	}
	return self;
}

- (void)note:(NSNotification *)notification
{
	[_names addObject:[notification name]];
}

- (NSUInteger)countOf:(NSString *)name
{
	NSUInteger i, n = [_names count], found = 0;

	for (i = 0; i < n; i++) {
		if ([[_names objectAtIndex:i] isEqualToString:name]) {
			found++;
		}
	}
	return found;
}

@end

int main(void)
{
	NSFileManager *manager = [NSFileManager defaultManager];
	NSString *root = [NSString stringWithUTF8String:PROBE_ROOT];

	/* FROM NOTHING, every time: a previous run that died mid-way must not change this one. */
	[manager removeItemAtPath:root error:NULL];
	setenv("FNX_CONFIG_ROOT", PROBE_ROOT, 1);

	/* ---- registration: a fallback, and NOT a file ------------------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];
		NSString *appScope = [NSString stringWithFormat:@"%s/Users/%@/Configuration",
							    PROBE_ROOT, user_name()];

		[defaults registerDefaults:[NSDictionary dictionaryWithObject:@"registered"
								      forKey:@"RegisteredOnly"]];
		check("defaults-registration",
		      [[defaults stringForKey:@"RegisteredOnly"] isEqualToString:@"registered"],
		      [NSString stringWithFormat:@"registered answer was %@", [defaults stringForKey:@"RegisteredOnly"]]);
		check("defaults-registration-volatile",
		      ![manager fileExistsAtPath:appScope],
		      [NSString stringWithFormat:@"registerDefaults: created %@", appScope]);
	}

	/* ---- the search order, shown by REMOVING the winner ------------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];

		[defaults registerDefaults:[NSDictionary dictionaryWithObject:@"from-registration"
								      forKey:@"OrderedKey"]];
		[defaults setObject:@"from-app" forKey:@"OrderedKey"];

		check("defaults-search-order",
		      [[defaults stringForKey:@"OrderedKey"] isEqualToString:@"from-app"],
		      [NSString stringWithFormat:@"app domain lost to the registration domain: %@",
				[defaults stringForKey:@"OrderedKey"]]);

		/* The argument domain is the ONE domain ahead of the app domain, and it is checked by writing
		 * the same key into a volatile domain we control. */
		[defaults setVolatileDomain:[NSDictionary dictionaryWithObject:@"from-volatile"
								       forKey:@"OrderedKey"]
				    forName:@"FNOrderTest"];
		check("defaults-search-order-volatile",
		      [[defaults stringForKey:@"OrderedKey"] isEqualToString:@"from-volatile"],
		      [NSString stringWithFormat:@"a volatile domain did not lead: %@",
				[defaults stringForKey:@"OrderedKey"]]);
		[defaults removeObjectForKey:@"OrderedKey"];
	}

	/* ---- a value that survives the object that set it -------------------- */
	{
		NSUserDefaults *writer = [[NSUserDefaults alloc] init];
		NSUserDefaults *reader = [[NSUserDefaults alloc] init];

		[writer removePersistentDomainForName:NSGlobalDomain];
		[writer setObject:@"persisted" forKey:@"PersistKey"];
		check("defaults-persist-round-trip",
		      [[reader stringForKey:@"PersistKey"] isEqualToString:@"persisted"],
		      [NSString stringWithFormat:@"a second object read %@", [reader stringForKey:@"PersistKey"]]);
	}

	/* ---- every plist type, through a fresh object ------------------------ */
	{
		NSUserDefaults *writer = [[NSUserDefaults alloc] init];
		NSUserDefaults *reader = [[NSUserDefaults alloc] init];
		NSData *data = [NSData dataWithBytes:"raw-bytes" length:9];
		NSDate *date = [NSDate dateWithTimeIntervalSince1970:1234567890.0];
		NSArray *array = [NSArray arrayWithObjects:@"one", @"two", @"three", nil];
		NSDictionary *dictionary = [NSDictionary dictionaryWithObject:@"inner" forKey:@"InnerKey"];
		NSArray *strings = [NSArray arrayWithObjects:@"a", @"b", nil];
		NSURL *url = [NSURL URLWithString:@"https://example.org/path?q=1"];

		[writer setObject:@"a string" forKey:@"TString"];
		[writer setInteger:42 forKey:@"TInteger"];
		[writer setDouble:2.5 forKey:@"TDouble"];
		[writer setBool:YES forKey:@"TBool"];
		[writer setObject:date forKey:@"TDate"];
		[writer setObject:data forKey:@"TData"];
		[writer setObject:array forKey:@"TArray"];
		[writer setObject:dictionary forKey:@"TDictionary"];
		[writer setObject:strings forKey:@"TStringArray"];
		[writer setURL:url forKey:@"TURL"];

		check("defaults-types-round-trip",
		      [[reader stringForKey:@"TString"] isEqualToString:@"a string"] &&
		      [reader integerForKey:@"TInteger"] == 42 &&
		      [reader doubleForKey:@"TDouble"] == 2.5 &&
		      [reader boolForKey:@"TBool"] &&
		      [[reader objectForKey:@"TDate"] isEqual:date] &&
		      [[reader dataForKey:@"TData"] isEqual:data] &&
		      [[reader arrayForKey:@"TArray"] isEqual:array] &&
		      [[reader dictionaryForKey:@"TDictionary"] isEqual:dictionary] &&
		      [[reader stringArrayForKey:@"TStringArray"] isEqual:strings] &&
		      [[[reader URLForKey:@"TURL"] absoluteString] isEqualToString:@"https://example.org/path?q=1"],
		      [NSString stringWithFormat:@"string=%@ integer=%ld double=%g bool=%d date=%@ data=%@ "
				@"array=%@ dictionary=%@ strings=%@ url=%@",
				[reader stringForKey:@"TString"], (long)[reader integerForKey:@"TInteger"],
				[reader doubleForKey:@"TDouble"], (int)[reader boolForKey:@"TBool"],
				[reader objectForKey:@"TDate"], [reader dataForKey:@"TData"],
				[reader arrayForKey:@"TArray"], [reader dictionaryForKey:@"TDictionary"],
				[reader stringArrayForKey:@"TStringArray"], [reader URLForKey:@"TURL"]]);
	}

	/* ---- the absent answers, each one its documented zero ---------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];
		NSArray *strings;

		strings = [NSArray arrayWithObjects:@"no", [NSNumber numberWithInt:7], nil];
		[defaults setObject:strings forKey:@"NotAllStrings"];

		check("defaults-absent-answers",
		      [defaults objectForKey:@"AbsentKey"] == nil &&
		      [defaults stringForKey:@"AbsentKey"] == nil &&
		      [defaults arrayForKey:@"AbsentKey"] == nil &&
		      [defaults dictionaryForKey:@"AbsentKey"] == nil &&
		      [defaults dataForKey:@"AbsentKey"] == nil &&
		      [defaults URLForKey:@"AbsentKey"] == nil &&
		      [defaults stringArrayForKey:@"AbsentKey"] == nil &&
		      ![defaults boolForKey:@"AbsentKey"] &&
		      [defaults integerForKey:@"AbsentKey"] == 0 &&
		      [defaults doubleForKey:@"AbsentKey"] == 0.0,
		      @"an absent key did not answer its documented zero");

		/* -stringArrayForKey: is nil for an array that is not ALL strings: the typed accessor refuses
		 * rather than converting, which is Apple's contract and the useful one. */
		check("defaults-string-array-strict",
		      [defaults stringArrayForKey:@"NotAllStrings"] == nil &&
		      [[defaults arrayForKey:@"NotAllStrings"] count] == 2,
		      @"-stringArrayForKey: answered for an array holding a non-string");
	}

	/* ---- Apple's documented coercions ------------------------------------ */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];

		[defaults setObject:@"YES" forKey:@"CYes"];
		[defaults setObject:@"true" forKey:@"CTrue"];
		[defaults setObject:@"1" forKey:@"COne"];
		[defaults setObject:@"no" forKey:@"CNo"];
		[defaults setObject:[NSNumber numberWithDouble:1.0] forKey:@"CNumberOne"];
		[defaults setObject:[NSNumber numberWithInt:0] forKey:@"CNumberZero"];
		[defaults setObject:@"42" forKey:@"CIntegerText"];

		check("defaults-coercion",
		      [defaults boolForKey:@"CYes"] && [defaults boolForKey:@"CTrue"] &&
		      [defaults boolForKey:@"COne"] && [defaults boolForKey:@"CNumberOne"] &&
		      ![defaults boolForKey:@"CNo"] && ![defaults boolForKey:@"CNumberZero"] &&
		      [defaults integerForKey:@"CIntegerText"] == 42,
		      [NSString stringWithFormat:@"YES=%d true=%d 1=%d 1.0=%d no=%d 0=%d text=%ld",
				(int)[defaults boolForKey:@"CYes"], (int)[defaults boolForKey:@"CTrue"],
				(int)[defaults boolForKey:@"COne"], (int)[defaults boolForKey:@"CNumberOne"],
				(int)[defaults boolForKey:@"CNo"], (int)[defaults boolForKey:@"CNumberZero"],
				(long)[defaults integerForKey:@"CIntegerText"]]);
	}

	/* ---- a store, not an alias ------------------------------------------- */
	{
		NSUserDefaults *writer = [[NSUserDefaults alloc] init];
		NSMutableString *mutable = [[NSMutableString alloc] initWithString:@"before"];
		NSUserDefaults *reader;

		[writer setObject:mutable forKey:@"MutatedKey"];
		[mutable appendString:@"-after"];
		reader = [[NSUserDefaults alloc] init];

		check("defaults-mutation-after-set",
		      [[reader stringForKey:@"MutatedKey"] isEqualToString:@"before"],
		      [NSString stringWithFormat:@"read %@ after the caller mutated it",
				[reader stringForKey:@"MutatedKey"]]);

		check("defaults-immutable-read",
		      ![[reader stringForKey:@"MutatedKey"] isKindOfClass:[NSMutableString class]],
		      @"-stringForKey: answered a MUTABLE string, which Apple forbids");
	}

	/* ---- the scope axis: SYSTEM > USER > SHARED, measured ---------------- */
	{
		NSString *domain = @"com.example.scopes";
		NSUserDefaults *view;

		plant_scope_file(@"Shared", domain,
				 [NSDictionary dictionaryWithObject:@"shared" forKey:@"Where"]);
		plant_scope_file(@"Users", domain,
				 [NSDictionary dictionaryWithObject:@"user" forKey:@"Where"]);
		plant_scope_file(@"System", domain,
				 [NSDictionary dictionaryWithObject:@"system" forKey:@"Where"]);

		view = [[NSUserDefaults alloc] initWithSuiteName:domain];
		check("defaults-scope-system-wins",
		      [[view stringForKey:@"Where"] isEqualToString:@"system"],
		      [NSString stringWithFormat:@"SYSTEM did not win: %@", [view stringForKey:@"Where"]]);

		[manager removeItemAtPath:scope_file_path(@"System", domain) error:NULL];
		view = [[NSUserDefaults alloc] initWithSuiteName:domain];
		check("defaults-scope-user-next",
		      [[view stringForKey:@"Where"] isEqualToString:@"user"],
		      [NSString stringWithFormat:@"USER did not win without SYSTEM: %@",
			[view stringForKey:@"Where"]]);

		[manager removeItemAtPath:scope_file_path(@"Users", domain) error:NULL];
		view = [[NSUserDefaults alloc] initWithSuiteName:domain];
		check("defaults-scope-shared-last",
		      [[view stringForKey:@"Where"] isEqualToString:@"shared"],
		      [NSString stringWithFormat:@"SHARED did not answer on its own: %@",
			[view stringForKey:@"Where"]]);
	}

	/* ---- forced keys ARE the SYSTEM scope -------------------------------- */
	{
		NSString *domain = @"com.example.forced";
		NSUserDefaults *view;

		plant_scope_file(@"Users", domain,
				 [NSDictionary dictionaryWithObject:@"user" forKey:@"Forced"]);
		plant_scope_file(@"System", domain,
				 [NSDictionary dictionaryWithObjectsAndKeys:@"managed", @"Forced",
				  @"managed-only", @"ManagedOnly", nil]);

		view = [[NSUserDefaults alloc] initWithSuiteName:domain];
		check("defaults-forced-key",
		      [view objectIsForcedForKey:@"Forced"] &&
		      [view objectIsForcedForKey:@"ManagedOnly"] &&
		      ![view objectIsForcedForKey:@"NotManaged"] &&
		      [view objectIsForcedForKey:@"Forced" inDomain:domain],
		      @"-objectIsForcedForKey: did not answer from the SYSTEM scope");
	}

	/* ---- suites: readable, never the write target ------------------------ */
	{
		NSString *suite = @"com.example.suite";
		NSUserDefaults *defaults;
		NSDictionary *suiteFile;

		plant_scope_file(@"Shared", suite,
				 [NSDictionary dictionaryWithObject:@"from-suite" forKey:@"SuiteOnly"]);

		defaults = [[NSUserDefaults alloc] init];
		[defaults addSuiteNamed:suite];
		check("defaults-suite-lookup",
		      [[defaults stringForKey:@"SuiteOnly"] isEqualToString:@"from-suite"],
		      [NSString stringWithFormat:@"the suite was not searched: %@",
			[defaults stringForKey:@"SuiteOnly"]]);

		[defaults setObject:@"written-to-app" forKey:@"AppOnly"];
		suiteFile = [NSDictionary dictionaryWithContentsOfFile:scope_file_path(@"Users", suite)];
		check("defaults-suite-is-not-write-target",
		      suiteFile == nil,
		      [NSString stringWithFormat:@"a write to the app domain created the SUITE's file: %@",
			suiteFile]);

		[defaults removeSuiteNamed:suite];
		{
			NSUserDefaults *after = [[NSUserDefaults alloc] init];

			check("defaults-suite-removed",
			      [after stringForKey:@"SuiteOnly"] == nil,
			      [NSString stringWithFormat:@"the suite was still searched: %@",
				[after stringForKey:@"SuiteOnly"]]);
		}
	}

	/* ---- volatile domains ------------------------------------------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];
		NSArray *names;

		names = [defaults volatileDomainNames];
		check("defaults-volatile-built-ins",
		      [names containsObject:NSArgumentDomain] &&
		      [names containsObject:NSRegistrationDomain],
		      [NSString stringWithFormat:@"built-in volatile names were %@", names]);

		[defaults setVolatileDomain:[NSDictionary dictionaryWithObject:@"volatile"
								       forKey:@"VolKey"]
				    forName:@"FNVolatileName"];
		check("defaults-volatile-set",
		      [[defaults volatileDomainNames] containsObject:@"FNVolatileName"] &&
		      [[defaults stringForKey:@"VolKey"] isEqualToString:@"volatile"] &&
		      [[[defaults volatileDomainForName:@"FNVolatileName"] objectForKey:@"VolKey"]
			isEqualToString:@"volatile"],
		      @"a volatile domain the caller set was not searched");

		[defaults removeVolatileDomainForName:@"FNVolatileName"];
		check("defaults-volatile-removed",
		      ![[defaults volatileDomainNames] containsObject:@"FNVolatileName"] &&
		      [defaults stringForKey:@"VolKey"] == nil,
		      @"a removed volatile domain was still searched");
	}

	/* ---- the argument domain, which is a fact about THIS process -------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];

		check("defaults-argument-domain",
		      [[defaults stringForKey:@"ProbeArgument"] isEqualToString:@"from-argv"] &&
		      [defaults stringForKey:@"ProbeArgumentAbsent"] == nil,
		      [NSString stringWithFormat:@"ProbeArgument was %@ (launch the probe with "
				@"`-ProbeArgument from-argv`)", [defaults stringForKey:@"ProbeArgument"]]);
	}

	/* ---- the union, with the search order deciding ---------------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];
		NSDictionary *representation;

		[defaults registerDefaults:[NSDictionary dictionaryWithObject:@"registration"
								      forKey:@"BothDomains"]];
		[defaults setObject:@"app" forKey:@"BothDomains"];
		representation = [defaults dictionaryRepresentation];

		check("defaults-dictionary-representation",
		      [[representation objectForKey:@"BothDomains"] isEqualToString:@"app"] &&
		      [[representation objectForKey:@"ProbeArgument"] isEqualToString:@"from-argv"] &&
		      [representation count] >= 2,
		      [NSString stringWithFormat:@"the union answered %@ for a key in two domains",
			[representation objectForKey:@"BothDomains"]]);
	}

	/* ---- whole-domain doors --------------------------------------------- */
	{
		NSString *domain = @"com.example.wholedomain";
		NSUserDefaults *defaults;
		NSDictionary *view;

		plant_scope_file(@"Shared", @"com.example.sharedonly",
				 [NSDictionary dictionaryWithObject:@"shipped" forKey:@"Shipped"]);
		{
			NSUserDefaults *shared = [[NSUserDefaults alloc] initWithSuiteName:@"com.example.sharedonly"];

			check("defaults-persistent-domain-shared-only",
			      [[[shared persistentDomainForName:@"com.example.sharedonly"]
				objectForKey:@"Shipped"] isEqualToString:@"shipped"],
			      @"a domain that lives only in the SHARED scope did not answer");
		}

		defaults = [[NSUserDefaults alloc] initWithSuiteName:domain];
		[defaults setPersistentDomain:[NSDictionary dictionaryWithObject:@"1" forKey:@"First"]
				      forName:domain];
		view = [[[NSUserDefaults alloc] initWithSuiteName:domain] persistentDomainForName:domain];
		check("defaults-persistent-domain-set",
		      [[view objectForKey:@"First"] isEqualToString:@"1"],
		      [NSString stringWithFormat:@"the domain read back as %@", view]);

		[defaults setPersistentDomain:[NSDictionary dictionaryWithObject:@"2" forKey:@"Second"]
				      forName:domain];
		view = [[[NSUserDefaults alloc] initWithSuiteName:domain] persistentDomainForName:domain];
		check("defaults-persistent-domain-replaces",
		      [view objectForKey:@"First"] == nil &&
		      [[view objectForKey:@"Second"] isEqualToString:@"2"],
		      [NSString stringWithFormat:@"the domain MERGED instead of replacing: %@", view]);

		[defaults removePersistentDomainForName:domain];
		check("defaults-persistent-domain-remove",
		      [[[NSUserDefaults alloc] initWithSuiteName:domain] persistentDomainForName:domain] == nil &&
		      ![manager fileExistsAtPath:scope_file_path(@"Users", domain)],
		      @"the domain or its file survived -removePersistentDomainForName:");
	}

	/* ---- the change notification, and the non-change ------------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];
		FnDefaultsObserver *observer = [[FnDefaultsObserver alloc] init];
		NSUInteger before;

		[[NSNotificationCenter defaultCenter] addObserver:observer
							selector:@selector(note:)
							    name:NSUserDefaultsDidChangeNotification
							  object:defaults];

		[defaults setObject:@"changed" forKey:@"NotifyKey"];
		check("defaults-change-notification",
		      [observer countOf:NSUserDefaultsDidChangeNotification] == 1,
		      [NSString stringWithFormat:@"a write posted it %lu time(s)",
			(unsigned long)[observer countOf:NSUserDefaultsDidChangeNotification]]);

		before = [observer countOf:NSUserDefaultsDidChangeNotification];
		[defaults removeObjectForKey:@"NeverWasThere"];
		check("defaults-no-notification-for-no-change",
		      [observer countOf:NSUserDefaultsDidChangeNotification] == before,
		      @"removing a key that was absent announced a change that did not happen");

		[[NSNotificationCenter defaultCenter] removeObserver:observer];
	}

	/* ---- what is refused ------------------------------------------------- */
	{
		int raised = 0;
		NSUserDefaults *defaults = [[NSUserDefaults alloc] init];

		@try {
			[defaults setObject:[[NSObject alloc] init] forKey:@"NotAPlist"];
		} @catch (NSException *exception) {
			raised = [exception.name isEqualToString:NSInvalidArgumentException];
		}
		check("defaults-refuses-non-plist-value", raised,
		      @"a value the store could not read back was accepted");

		raised = 0;
		@try {
			[defaults setObject:@"value" forKey:@""];
		} @catch (NSException *exception) {
			raised = 1;
		}
		check("defaults-refuses-empty-key", raised, @"an empty key was accepted");

		raised = 0;
		@try {
			[[NSUserDefaults alloc] initWithSuiteName:@"../escape"];
		} @catch (NSException *exception) {
			raised = 1;
		}
		check("defaults-refuses-escaping-domain", raised,
		      @"a domain name containing '/' was accepted, so a domain can name a file outside "
		      @"Configuration/");
	}

	/* ---- a file that will not parse is an ERROR, not an empty domain ---- */
	{
		NSString *domain = @"com.example.corrupt";
		int raised = 0;

		plant_scope_file(@"Users", domain,
				 [NSDictionary dictionaryWithObject:@"fine" forKey:@"Before"]);
		{
			NSData *garbage = [NSData dataWithBytes:"this is not a property list"
							 length:28];
			NSString *path = scope_file_path(@"Users", domain);

			[garbage writeToFile:path atomically:YES];
		}
		@try {
			NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:domain];

			(void)[defaults stringForKey:@"Before"];
		} @catch (NSException *exception) {
			raised = [exception.name isEqualToString:NSInternalInconsistencyException];
		}
		check("defaults-corrupt-file-refused", raised,
		      @"a malformed plist was treated as an empty domain, which would let the next write "
		      @"delete the settings it did not set");
	}

	/* ---- the size limit: notify, and still save ------------------------- */
	{
		NSUserDefaults *defaults = [[NSUserDefaults alloc] initWithSuiteName:@"com.example.big"];
		FnDefaultsObserver *observer = [[FnDefaultsObserver alloc] init];
		NSMutableString *big = [[NSMutableString alloc] init];

		[[NSNotificationCenter defaultCenter]
			addObserver:observer
			   selector:@selector(note:)
			       name:NSUserDefaultsSizeLimitExceededNotification
			     object:defaults];
		/* GROWN PAST THE MAXIMUM MEASURED IN BYTES: see this file's header. */
		while ([big length] <= NSUserDefaultsMaximumDomainSize + 1024) {
			[big appendString:@"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
					 @"0123456789abcdef"];
		}
		[defaults setObject:big forKey:@"Big"];

		check("defaults-size-limit",
		      [observer countOf:NSUserDefaultsSizeLimitExceededNotification] == 1 &&
		      [[[[NSUserDefaults alloc] initWithSuiteName:@"com.example.big"]
			stringForKey:@"Big"] length] == [big length],
		      [NSString stringWithFormat:@"limit notification fired %lu time(s), and the value "
			@"read back with length %lu where %lu was written",
			(unsigned long)[observer countOf:NSUserDefaultsSizeLimitExceededNotification],
			(unsigned long)[[[[NSUserDefaults alloc] initWithSuiteName:@"com.example.big"]
			stringForKey:@"Big"] length], (unsigned long)[big length]]);

		[[NSNotificationCenter defaultCenter] removeObserver:observer];
		[defaults removePersistentDomainForName:@"com.example.big"];
	}

	/* ---- THE ONE CHECK AT THE REAL ROOT --------------------------------- */
	{
		NSString *domain = @"com.example.location";
		NSString *path = [NSString stringWithFormat:@"/Users/%@/Configuration/%@.plist",
								user_name(), domain];
		NSUserDefaults *defaults;

		unsetenv("FNX_CONFIG_ROOT");
		defaults = [[NSUserDefaults alloc] initWithSuiteName:domain];
		[defaults setObject:@"placed" forKey:@"Placed"];
		check("defaults-store-location",
		      [manager fileExistsAtPath:path],
		      [NSString stringWithFormat:@"nothing was written at %@", path]);

		[defaults removePersistentDomainForName:domain];
		check("defaults-store-location-cleanup",
		      ![manager fileExistsAtPath:path],
		      [NSString stringWithFormat:@"the probe left %@ behind", path]);
	}

	/* ---- the scratch tree, the image's Configuration/, and out ---------- */
	[manager removeItemAtPath:root error:NULL];

	printf("FOUNDATION-DEFAULTS RESULT ok=%d fail=%d\n", okc, failc);
	/* The exit status, in the probe's OWN output: after a probe the console can stop serving input for a
	 * while, so an `echo $?` the harness types may never run. This is the same value: failc ? 1 : 0 is the
	 * return statement below. */
	printf("FOUNDATION-DEFAULTS-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-DEFAULTS DONE\n");
	return failc ? 1 : 0;
}
