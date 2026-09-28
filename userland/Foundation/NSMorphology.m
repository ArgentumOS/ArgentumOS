/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMorphology.m — the three morphology VALUE types (§62.78). MANUAL OWNERSHIP.
 *
 * WHAT IS REAL HERE IS THE VALUE: eight grammatical fields with the vocabulary's own "not set" as zero, a
 * pronoun with the morphology it agrees with, a custom-pronoun record of five forms - and for each of them
 * copying, equality, hashing and secure coding, which is what an attribute store and a caller actually use.
 *
 * AND WHAT IS REFUSED HERE IS REFUSED BY NAME: this system carries no PER-LANGUAGE PRONOUN DATA, so
 * -setCustomPronoun:forLanguage:error: answers NO with the reason in the error and +isSupportedForLanguage:
 * answers NO for the same ground. The alternative - accepting a value nothing can read back - is the kind of
 * silence this library's doors exist to avoid.
 */

#import <Foundation/NSMorphology.h>
#import <Foundation/NSArray.h>
#import <Foundation/NSCoder.h>
#import <Foundation/NSDictionary.h>
#import <Foundation/NSError.h>
#import <Foundation/NSString.h>

/* THE ONE SHAPE EVERY STRING SETTER HAS: snapshot through -initWithString: (this library's -copy is NOT a value
 * copy, which §62.60 and §62.66 both record), release what was there, keep the snapshot. */
static void fn_set_copy(NSString **slot, NSString *value)
{
	NSString *kept = value != nil ? [[NSString alloc] initWithString:value] : nil;

	[*slot release];
	*slot = kept;
}

#define FN_PRONOUN_UNSUPPORTED_DOMAIN @"NSMorphologyErrorDomain"
#define FN_PRONOUN_UNSUPPORTED_CODE 1

@implementation NSMorphologyCustomPronoun

- (nullable NSString *)subjectForm { return _subjectForm; }
- (void)setSubjectForm:(nullable NSString *)form { fn_set_copy(&_subjectForm, form); }
- (nullable NSString *)objectForm { return _objectForm; }
- (void)setObjectForm:(nullable NSString *)form { fn_set_copy(&_objectForm, form); }
- (nullable NSString *)possessiveForm { return _possessiveForm; }
- (void)setPossessiveForm:(nullable NSString *)form { fn_set_copy(&_possessiveForm, form); }
- (nullable NSString *)possessiveAdjectiveForm { return _possessiveAdjectiveForm; }
- (void)setPossessiveAdjectiveForm:(nullable NSString *)form { fn_set_copy(&_possessiveAdjectiveForm, form); }
- (nullable NSString *)reflexiveForm { return _reflexiveForm; }
- (void)setReflexiveForm:(nullable NSString *)form { fn_set_copy(&_reflexiveForm, form); }

+ (BOOL)isSupportedForLanguage:(NSString *)language
{
	(void)language;
	/* NO, AND THE GROUND IS THE ABSENCE OF PRONOUN DATA: no language's custom pronouns are known here, so the
	 * honest answer is that none is supported. */
	return NO;
}

+ (NSArray *)requiredKeysForLanguage:(NSString *)language
{
	(void)language;
	return [NSArray arrayWithObjects:@"subjectForm", @"objectForm", @"possessiveForm",
					 @"possessiveAdjectiveForm", @"reflexiveForm", nil];
}

- (void)dealloc
{
	[_subjectForm release];
	[_objectForm release];
	[_possessiveForm release];
	[_possessiveAdjectiveForm release];
	[_reflexiveForm release];
	[super dealloc];
}

- (id)copy
{
	NSMorphologyCustomPronoun *copy = [[NSMorphologyCustomPronoun alloc] init];

	[copy setSubjectForm:_subjectForm];
	[copy setObjectForm:_objectForm];
	[copy setPossessiveForm:_possessiveForm];
	[copy setPossessiveAdjectiveForm:_possessiveAdjectiveForm];
	[copy setReflexiveForm:_reflexiveForm];
	return copy;
}

- (BOOL)isEqual:(id)other
{
	NSMorphologyCustomPronoun *that;

	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSMorphologyCustomPronoun class]]) {
		return NO;
	}
	that = other;
	return (_subjectForm == that->_subjectForm || [_subjectForm isEqual:that->_subjectForm]) &&
	       (_objectForm == that->_objectForm || [_objectForm isEqual:that->_objectForm]) &&
	       (_possessiveForm == that->_possessiveForm || [_possessiveForm isEqual:that->_possessiveForm]) &&
	       (_possessiveAdjectiveForm == that->_possessiveAdjectiveForm ||
		[_possessiveAdjectiveForm isEqual:that->_possessiveAdjectiveForm]) &&
	       (_reflexiveForm == that->_reflexiveForm || [_reflexiveForm isEqual:that->_reflexiveForm]);
}

- (NSUInteger)hash
{
	return [_subjectForm hash] ^ [_objectForm hash] ^ [_possessiveForm hash] ^
	       [_possessiveAdjectiveForm hash] ^ [_reflexiveForm hash];
}

+ (BOOL)supportsSecureCoding { return YES; }

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_subjectForm forKey:@"subjectForm"];
	[coder encodeObject:_objectForm forKey:@"objectForm"];
	[coder encodeObject:_possessiveForm forKey:@"possessiveForm"];
	[coder encodeObject:_possessiveAdjectiveForm forKey:@"possessiveAdjectiveForm"];
	[coder encodeObject:_reflexiveForm forKey:@"reflexiveForm"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		[self setSubjectForm:[coder decodeObjectForKey:@"subjectForm"]];
		[self setObjectForm:[coder decodeObjectForKey:@"objectForm"]];
		[self setPossessiveForm:[coder decodeObjectForKey:@"possessiveForm"]];
		[self setPossessiveAdjectiveForm:[coder decodeObjectForKey:@"possessiveAdjectiveForm"]];
		[self setReflexiveForm:[coder decodeObjectForKey:@"reflexiveForm"]];
	}
	return self;
}

@end

@implementation NSMorphology

/* A FRESH MORPHOLOGY IS UNSET IN EVERY FIELD, AND THAT NEEDS AN -init RATHER THAN A ZEROED ALLOCATION.
 *
 * The probe caught this: `NSGrammaticalGenderNotSet` is THREE (it is the value the enum shipped with, whose
 * Feminine is zero), so a freshly allocated instance - which is all zeroes - claimed to be FEMININE, and
 * -isUnspecified correctly said it was not unspecified. The five enums added beside this class put their NotSet
 * at zero precisely so that a zeroed allocation means "nothing said", and this -init extends that guarantee to
 * the one enum that predates them rather than special-casing the reader. */
- (instancetype)init
{
	self = [super init];
	if (self != nil) {
		_grammaticalGender = NSGrammaticalGenderNotSet;
		_number = NSGrammaticalNumberNotSet;
		_partOfSpeech = NSGrammaticalPartOfSpeechNotSet;
		_definiteness = NSGrammaticalDefinitenessNotSet;
		_determination = NSGrammaticalDeterminationNotSet;
		_grammaticalCase = NSGrammaticalCaseNotSet;
		_grammaticalPerson = NSGrammaticalPersonNotSet;
		_pronounType = NSGrammaticalPronounTypeNotSet;
	}
	return self;
}

/* A FRESH VALUE EVERY TIME, INCLUDING FOR +userMorphology: Apple's answers the user's preferences, and this
 * system has none, so the answer is an UNSPECIFIED morphology - and a fresh one, because a morphology is
 * MUTABLE and a shared instance would let one caller's -setNumber: change every other caller's answer. */
+ (NSMorphology *)userMorphology
{
	return [[[NSMorphology alloc] init] autorelease];
}

- (NSGrammaticalGender)grammaticalGender { return _grammaticalGender; }
- (void)setGrammaticalGender:(NSGrammaticalGender)value { _grammaticalGender = value; }
- (NSGrammaticalNumber)number { return _number; }
- (void)setNumber:(NSGrammaticalNumber)value { _number = value; }
- (NSGrammaticalPartOfSpeech)partOfSpeech { return _partOfSpeech; }
- (void)setPartOfSpeech:(NSGrammaticalPartOfSpeech)value { _partOfSpeech = value; }
- (NSGrammaticalDefiniteness)definiteness { return _definiteness; }
- (void)setDefiniteness:(NSGrammaticalDefiniteness)value { _definiteness = value; }
- (NSGrammaticalDetermination)determination { return _determination; }
- (void)setDetermination:(NSGrammaticalDetermination)value { _determination = value; }
- (NSGrammaticalCase)grammaticalCase { return _grammaticalCase; }
- (void)setGrammaticalCase:(NSGrammaticalCase)value { _grammaticalCase = value; }
- (NSGrammaticalPerson)grammaticalPerson { return _grammaticalPerson; }
- (void)setGrammaticalPerson:(NSGrammaticalPerson)value { _grammaticalPerson = value; }
- (NSGrammaticalPronounType)pronounType { return _pronounType; }
- (void)setPronounType:(NSGrammaticalPronounType)value { _pronounType = value; }

/* EACH FIELD AGAINST ITS OWN `...NotSet`, which is zero in every one of the five enums added beside this class
 * and three in NSGrammaticalGender (the value Apple's own header shipped here) - so the test names the cases
 * rather than comparing to zero. */
- (BOOL)isUnspecified
{
	return _grammaticalGender == NSGrammaticalGenderNotSet &&
	       _number == NSGrammaticalNumberNotSet &&
	       _partOfSpeech == NSGrammaticalPartOfSpeechNotSet &&
	       _definiteness == NSGrammaticalDefinitenessNotSet &&
	       _determination == NSGrammaticalDeterminationNotSet &&
	       _grammaticalCase == NSGrammaticalCaseNotSet &&
	       _grammaticalPerson == NSGrammaticalPersonNotSet &&
	       _pronounType == NSGrammaticalPronounTypeNotSet;
}

- (nullable NSMorphologyCustomPronoun *)customPronounForLanguage:(NSString *)language
{
	(void)language;
	/* NIL BECAUSE NOTHING CAN HAVE BEEN STORED: the setter below refuses, so there is no language with an entry
	 * to answer with. */
	return nil;
}

- (BOOL)setCustomPronoun:(nullable NSMorphologyCustomPronoun *)features
	     forLanguage:(NSString *)language
		   error:(NSError ** _Nullable)error
{
	(void)features;
	/* THE NAMED REFUSAL, with the reason in the error the caller passed a slot for. */
	if (error != NULL) {
		*error = [NSError errorWithDomain:FN_PRONOUN_UNSUPPORTED_DOMAIN
					     code:FN_PRONOUN_UNSUPPORTED_CODE
					 userInfo:[NSDictionary dictionaryWithObject:
			[NSString stringWithFormat:@"per-language pronoun data is not carried by this system, so "
						   @"\"%@\" has none to set", language]
							  forKey:NSLocalizedDescriptionKey]];
	}
	return NO;
}

- (void)dealloc
{
	[super dealloc];
}

- (id)copy
{
	NSMorphology *copy = [[NSMorphology alloc] init];

	[copy setGrammaticalGender:_grammaticalGender];
	[copy setNumber:_number];
	[copy setPartOfSpeech:_partOfSpeech];
	[copy setDefiniteness:_definiteness];
	[copy setDetermination:_determination];
	[copy setGrammaticalCase:_grammaticalCase];
	[copy setGrammaticalPerson:_grammaticalPerson];
	[copy setPronounType:_pronounType];
	return copy;
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSMorphology class]]) {
		return NO;
	}
	return _grammaticalGender == ((NSMorphology *)other)->_grammaticalGender &&
	       _number == ((NSMorphology *)other)->_number &&
	       _partOfSpeech == ((NSMorphology *)other)->_partOfSpeech &&
	       _definiteness == ((NSMorphology *)other)->_definiteness &&
	       _determination == ((NSMorphology *)other)->_determination &&
	       _grammaticalCase == ((NSMorphology *)other)->_grammaticalCase &&
	       _grammaticalPerson == ((NSMorphology *)other)->_grammaticalPerson &&
	       _pronounType == ((NSMorphology *)other)->_pronounType;
}

- (NSUInteger)hash
{
	return (NSUInteger)(_grammaticalGender * 31 + _number * 17 + _partOfSpeech * 13 + _definiteness * 11 +
			    _determination * 7 + _grammaticalCase * 5 + _grammaticalPerson * 3 + _pronounType);
}

+ (BOOL)supportsSecureCoding { return YES; }

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeInteger:(NSInteger)_grammaticalGender forKey:@"grammaticalGender"];
	[coder encodeInteger:(NSInteger)_number forKey:@"number"];
	[coder encodeInteger:(NSInteger)_partOfSpeech forKey:@"partOfSpeech"];
	[coder encodeInteger:(NSInteger)_definiteness forKey:@"definiteness"];
	[coder encodeInteger:(NSInteger)_determination forKey:@"determination"];
	[coder encodeInteger:(NSInteger)_grammaticalCase forKey:@"grammaticalCase"];
	[coder encodeInteger:(NSInteger)_grammaticalPerson forKey:@"grammaticalPerson"];
	[coder encodeInteger:(NSInteger)_pronounType forKey:@"pronounType"];
	/* THE PER-LANGUAGE PRONOUNS ARE NOT ENCODED, and that is a fact about the class rather than an omission: the
	 * setter refuses, so the map is empty by construction. */
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	self = [super init];
	if (self != nil) {
		_grammaticalGender = (NSGrammaticalGender)[coder decodeIntegerForKey:@"grammaticalGender"];
		_number = (NSGrammaticalNumber)[coder decodeIntegerForKey:@"number"];
		_partOfSpeech = (NSGrammaticalPartOfSpeech)[coder decodeIntegerForKey:@"partOfSpeech"];
		_definiteness = (NSGrammaticalDefiniteness)[coder decodeIntegerForKey:@"definiteness"];
		_determination = (NSGrammaticalDetermination)[coder decodeIntegerForKey:@"determination"];
		_grammaticalCase = (NSGrammaticalCase)[coder decodeIntegerForKey:@"grammaticalCase"];
		_grammaticalPerson = (NSGrammaticalPerson)[coder decodeIntegerForKey:@"grammaticalPerson"];
		_pronounType = (NSGrammaticalPronounType)[coder decodeIntegerForKey:@"pronounType"];
	}
	return self;
}

@end

@implementation NSMorphologyPronoun

- (instancetype)initWithPronoun:(NSString *)pronoun
		     morphology:(NSMorphology *)morphology
	    dependentMorphology:(nullable NSMorphology *)dependentMorphology
{
	/* APPLE'S OWN RULE: a pronoun is the whole point of the value, so a nil one is refused rather than stored
	 * (the same shape -initWithIdentifier: uses in §62.66). */
	if (pronoun == nil || [pronoun length] == 0) {
		[self release];
		[NSException raise:NSInvalidArgumentException
			    format:@"-[NSMorphologyPronoun initWithPronoun:morphology:dependentMorphology:] needs a "
				   @"pronoun"];
	}
	self = [super init];
	if (self != nil) {
		fn_set_copy(&_pronoun, pronoun);
		_morphology = [morphology copy];
		_dependentMorphology = [dependentMorphology copy];
	}
	return self;
}

- (NSString *)pronoun { return _pronoun; }
- (NSMorphology *)morphology { return _morphology; }
- (nullable NSMorphology *)dependentMorphology { return _dependentMorphology; }

- (void)dealloc
{
	[_pronoun release];
	[_morphology release];
	[_dependentMorphology release];
	[super dealloc];
}

- (id)copy
{
	return [[NSMorphologyPronoun alloc] initWithPronoun:_pronoun
						 morphology:_morphology
					    dependentMorphology:_dependentMorphology];
}

- (BOOL)isEqual:(id)other
{
	if (other == self) {
		return YES;
	}
	if (![other isKindOfClass:[NSMorphologyPronoun class]]) {
		return NO;
	}
	return _pronoun == ((NSMorphologyPronoun *)other)->_pronoun ||
	       [_pronoun isEqual:((NSMorphologyPronoun *)other)->_pronoun];
}

- (NSUInteger)hash
{
	return [_pronoun hash];
}

+ (BOOL)supportsSecureCoding { return YES; }

- (void)encodeWithCoder:(NSCoder *)coder
{
	[coder encodeObject:_pronoun forKey:@"pronoun"];
	[coder encodeObject:_morphology forKey:@"morphology"];
	[coder encodeObject:_dependentMorphology forKey:@"dependentMorphology"];
}

- (nullable instancetype)initWithCoder:(NSCoder *)coder
{
	id pronoun = [coder decodeObjectForKey:@"pronoun"];

	if (pronoun == nil) {
		[self release];
		return nil;		/* a pronoun value with no pronoun cannot be built */
	}
	self = [super init];
	if (self != nil) {
		fn_set_copy(&_pronoun, pronoun);
		_morphology = [[coder decodeObjectForKey:@"morphology"] copy];
		_dependentMorphology = [[coder decodeObjectForKey:@"dependentMorphology"] copy];
	}
	return self;
}

@end
