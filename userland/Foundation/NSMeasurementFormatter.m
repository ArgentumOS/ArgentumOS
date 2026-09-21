/*
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 */
/*
 * NSMeasurementFormatter.m — (family, symbol) to a CLDR identifier, then one unumf call.
 * docs/design/foundation-plan.md §12.3 W12.
 *
 * MANUAL OWNERSHIP (MRC: the whole library is). The locale is copied, the number formatter retained.
 *
 * THE MAPPING IS PER-FAMILY AND IS ANSWERED BY A FUNCTION, not by a table of class pointers: a static C table
 * cannot hold `[NSUnitLength class]` (a class object is not a constant expression), so the dispatch is a chain
 * of `isKindOfClass:` over plain C tables of (symbol, identifier). That is also the shape that lets a family
 * be added in one place.
 *
 * THE SKELETON IS `measure-unit/<identifier>` PLUS A WIDTH, and the width option is BARE (ICU writes named
 * options without a dot — §29.1's trap, where the dotted form is a SYNTAX ERROR and the design pass's first
 * probe read that as "the widths are unreachable").
 *
 * `-stringFromUnit:` ANSWERS THE NAME ALONE, and ICU does not have a "name only" mode: it formats a QUANTITY
 * with a unit. So this takes the wide form of 1 and STRIPS THE LEADING NUMBER, which is the same extraction
 * the W11 components formatter does for its spell-out style — a documented operation on a shape ICU
 * guarantees, and the boundary is stated there.
 */

#import <Foundation/NSMeasurementFormatter.h>
#import <Foundation/NSLocale.h>
#import <Foundation/NSNumberFormatter.h>
#import <Foundation/NSMeasurement.h>
#import <Foundation/NSString.h>
#import <Foundation/NSDimension.h>
#import <Foundation/NSUnitInformationStorage.h>
#import <Foundation/NSUnitLength.h>
#import <Foundation/NSUnitMass.h>
#import <Foundation/NSUnitDuration.h>
#import <Foundation/NSUnitTemperature.h>
#import <Foundation/NSUnitVolume.h>
#import <Foundation/NSUnitArea.h>
#import <Foundation/NSUnitAngle.h>
#import <Foundation/NSUnitSpeed.h>
#import <Foundation/NSUnitAcceleration.h>
#import <Foundation/NSUnitFrequency.h>
#import <Foundation/NSUnitEnergy.h>
#import <Foundation/NSUnitPower.h>
#import <Foundation/NSUnitPressure.h>
#import <Foundation/NSUnitElectric.h>
#import <Foundation/NSUnitIlluminance.h>

#include <unicode/unumberformatter.h>
#include <unicode/uloc.h>
#include <unicode/ustring.h>

#include <stdio.h>
#include <string.h>

typedef struct { const char *symbol; const char *cldr; } fn_mf_entry;

static const fn_mf_entry fn_mf_length[] = {
	{ "m", "length-meter" }, { "km", "length-kilometer" }, { "cm", "length-centimeter" },
	{ "mm", "length-millimeter" }, { "µm", "length-micrometer" }, { "nm", "length-nanometer" },
	{ "pm", "length-picometer" }, { "Mm", "length-megameter" }, { "hm", "length-hectometer" },
	{ "dam", "length-decameter" }, { "dm", "length-decimeter" }, { "mi", "length-mile" },
	{ "yd", "length-yard" }, { "ft", "length-foot" }, { "in", "length-inch" },
	{ "nmi", "length-nautical-mile" }, { "ly", "length-light-year" }, { "ftm", "length-fathom" },
	{ "fur", "length-furlong" }, { "au", "length-astronomical-unit" }, { "pc", "length-parsec" },
	{ NULL, NULL }
};

static const fn_mf_entry fn_mf_mass[] = {
	{ "kg", "mass-kilogram" }, { "g", "mass-gram" }, { "mg", "mass-milligram" },
	{ "µg", "mass-microgram" }, { "ng", "mass-nanogram" }, { "pg", "mass-picogram" },
	{ "dg", "mass-decigram" }, { "cg", "mass-centigram" }, { "oz", "mass-ounce" },
	{ "lb", "mass-pound" }, { "lbm", "mass-pound" }, { "st", "mass-stone" },
	{ "t", "mass-metric-ton" }, { "ton", "mass-ton" }, { "ct", "mass-carat" },
	{ "ozt", "mass-ounce-troy" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_duration[] = {
	{ "hr", "duration-hour" }, { "min", "duration-minute" }, { "s", "duration-second" },
	{ "ms", "duration-millisecond" }, { "µs", "duration-microsecond" },
	{ "ns", "duration-nanosecond" }, { "ps", "duration-picosecond" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_temperature[] = {
	{ "K", "temperature-kelvin" }, { "°C", "temperature-celsius" },
	{ "°F", "temperature-fahrenheit" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_volume[] = {
	{ "L", "volume-liter" }, { "mL", "volume-milliliter" }, { "cL", "volume-centiliter" },
	{ "dL", "volume-deciliter" }, { "kL", "volume-kiloliter" }, { "ML", "volume-megaliter" },
	{ "m³", "volume-cubic-meter" }, { "cm³", "volume-cubic-centimeter" },
	{ "mm³", "volume-cubic-millimeter" }, { "in³", "volume-cubic-inch" },
	{ "ft³", "volume-cubic-foot" }, { "yd³", "volume-cubic-yard" },
	{ "mi³", "volume-cubic-mile" }, { "km³", "volume-cubic-kilometer" },
	{ "ac·ft", "volume-acre-foot" }, { "bu", "volume-bushel" },
	{ "tsp", "volume-teaspoon" }, { "tbsp", "volume-tablespoon" },
	{ "fl oz", "volume-fluid-ounce" }, { "cup", "volume-cup" }, { "pt", "volume-pint" },
	{ "qt", "volume-quart" }, { "gal", "volume-gallon" },
	{ "imp gal", "volume-imperial-gallon" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_area[] = {
	{ "m²", "area-square-meter" }, { "km²", "area-square-kilometer" },
	{ "cm²", "area-square-centimeter" }, { "mm²", "area-square-millimeter" },
	{ "mi²", "area-square-mile" }, { "ft²", "area-square-foot" },
	{ "in²", "area-square-inch" }, { "yd²", "area-square-yard" },
	{ "ac", "area-acre" }, { "ha", "area-hectare" },
	{ "µm²", "area-square-micrometer" }, { "nm²", "area-square-nanometer" },
	{ "Mm²", "area-square-megameter" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_angle[] = {
	{ "°", "angle-degree" }, { "rad", "angle-radian" }, { "′", "angle-arc-minute" },
	{ "″", "angle-arc-second" }, { "grad", "angle-gradian" }, { "rev", "angle-revolution" },
	{ NULL, NULL }
};

static const fn_mf_entry fn_mf_speed[] = {
	{ "m/s", "speed-meter-per-second" }, { "km/h", "speed-kilometer-per-hour" },
	{ "mph", "speed-mile-per-hour" }, { "kn", "speed-knot" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_acceleration[] = {
	{ "m/s²", "acceleration-meter-per-square-second" }, { "g", "acceleration-g-force" },
	{ NULL, NULL }
};

static const fn_mf_entry fn_mf_frequency[] = {
	{ "Hz", "frequency-hertz" }, { "kHz", "frequency-kilohertz" },
	{ "MHz", "frequency-megahertz" }, { "GHz", "frequency-gigahertz" },
	{ "THz", "frequency-terahertz" }, { "mHz", "frequency-millihertz" },
	{ "µHz", "frequency-microhertz" }, { "nHz", "frequency-nanohertz" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_energy[] = {
	{ "J", "energy-joule" }, { "kJ", "energy-kilojoule" }, { "cal", "energy-calorie" },
	{ "kcal", "energy-kilocalorie" }, { "kWh", "energy-kilowatt-hour" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_power[] = {
	{ "W", "power-watt" }, { "kW", "power-kilowatt" }, { "MW", "power-megawatt" },
	{ "GW", "power-gigawatt" }, { "TW", "power-terawatt" }, { "mW", "power-milliwatt" },
	{ "µW", "power-microwatt" }, { "nW", "power-nanowatt" }, { "pW", "power-picowatt" },
	{ "fW", "power-femtowatt" }, { "hp", "power-horsepower" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_pressure[] = {
	{ "N/m²", "pressure-pascal" }, { "hPa", "pressure-hectopascal" },
	{ "kPa", "pressure-kilopascal" }, { "MPa", "pressure-megapascal" },
	{ "GPa", "pressure-gigapascal" }, { "bar", "pressure-bar" },
	{ "mbar", "pressure-millibar" }, { "mmHg", "pressure-millimeter-of-mercury" },
	{ "inHg", "pressure-inch-of-mercury" },
	{ "psi", "pressure-pound-per-square-inch" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_current[] = {
	{ "A", "electric-ampere" }, { "mA", "electric-milliampere" },
	{ "µA", "electric-microampere" }, { "kA", "electric-kiloampere" },
	{ "MA", "electric-megaampere" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_charge[] = {
	{ "C", "electric-coulomb" }, { "Ah", "electric-ampere-hour" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_voltage[] = {
	{ "V", "electric-volt" }, { "mV", "electric-millivolt" },
	{ "µV", "electric-microvolt" }, { "kV", "electric-kilovolt" },
	{ "MV", "electric-megavolt" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_resistance[] = {
	{ "Ω", "electric-ohm" }, { "mΩ", "electric-milliohm" },
	{ "µΩ", "electric-microohm" }, { "kΩ", "electric-kilo-ohm" },
	{ "MΩ", "electric-mega-ohm" }, { NULL, NULL }
};

static const fn_mf_entry fn_mf_storage[] = {
	{ "bit", "digital-bit" }, { "B", "digital-byte" },
	{ "kbit", "digital-kilobit" }, { "kB", "digital-kilobyte" },
	{ "Mbit", "digital-megabit" }, { "MB", "digital-megabyte" },
	{ "Gbit", "digital-gigabit" }, { "GB", "digital-gigabyte" },
	{ "Tbit", "digital-terabit" }, { "TB", "digital-terabyte" },
	{ "Pbit", "digital-petabit" }, { "PB", "digital-petabyte" },
	{ "Ebit", "digital-exabit" }, { "EB", "digital-exabyte" },
	{ "Kibit", "digital-kibibit" }, { "KiB", "digital-kibibyte" },
	{ "Mibit", "digital-mebibit" }, { "MiB", "digital-mebibyte" },
	{ "Gibit", "digital-gibibit" }, { "GiB", "digital-gibibyte" },
	{ "Tibit", "digital-tebibit" }, { "TiB", "digital-tebibyte" },
	{ "Pibit", "digital-pebibit" }, { "PiB", "digital-pebibyte" },
	{ NULL, NULL }
};

/* The single-unit family, kept beside the storage table because both are one-row lookups. */
static const fn_mf_entry fn_mf_illuminance[] = {
	{ "lx", "light-lux" }, { NULL, NULL }
};

/* Look a symbol up in one family's table. */
static const char *fn_mf_lookup(const fn_mf_entry *table, NSString *symbol)
{
	const char *text;
	int i;

	if (symbol == nil) {
		return NULL;
	}
	text = [symbol UTF8String];
	if (text == NULL) {
		return NULL;
	}
	for (i = 0; table[i].symbol != NULL; i++) {
		if (strcmp(table[i].symbol, text) == 0) {
			return table[i].cldr;
		}
	}
	return NULL;
}

/*
 * THE DISPATCH: which table a unit belongs to. A unit whose family is not listed, or whose symbol is not in
 * its family's table, answers NULL — and the header says why that is nil rather than a guessed identifier.
 */
static const char *fn_mf_cldr(NSUnit *unit)
{
	if (unit == nil) {
		return NULL;
	}
	if ([unit isKindOfClass:[NSUnitLength class]]) return fn_mf_lookup(fn_mf_length, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitMass class]]) return fn_mf_lookup(fn_mf_mass, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitDuration class]]) return fn_mf_lookup(fn_mf_duration, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitTemperature class]]) return fn_mf_lookup(fn_mf_temperature, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitVolume class]]) return fn_mf_lookup(fn_mf_volume, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitArea class]]) return fn_mf_lookup(fn_mf_area, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitAngle class]]) return fn_mf_lookup(fn_mf_angle, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitSpeed class]]) return fn_mf_lookup(fn_mf_speed, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitAcceleration class]]) return fn_mf_lookup(fn_mf_acceleration, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitFrequency class]]) return fn_mf_lookup(fn_mf_frequency, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitEnergy class]]) return fn_mf_lookup(fn_mf_energy, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitPower class]]) return fn_mf_lookup(fn_mf_power, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitPressure class]]) return fn_mf_lookup(fn_mf_pressure, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitInformationStorage class]]) return fn_mf_lookup(fn_mf_storage, [unit symbol]);
	if ([unit isKindOfClass:[NSUnitIlluminance class]]) {
		return fn_mf_lookup(fn_mf_illuminance, [unit symbol]);
	}
	/* THE ELECTRICAL FAMILIES SHARE A SUPERCLASS SHAPE and are four classes: dispatch by symbol, since their
	 * symbols do not collide across the four. */
	{
		const char *found;

		found = fn_mf_lookup(fn_mf_current, [unit symbol]);
		if (found == NULL) found = fn_mf_lookup(fn_mf_charge, [unit symbol]);
		if (found == NULL) found = fn_mf_lookup(fn_mf_voltage, [unit symbol]);
		if (found == NULL) found = fn_mf_lookup(fn_mf_resistance, [unit symbol]);
		if (found != NULL) {
			return found;
		}
	}
	return NULL;
}

/* The unit style, as ICU's WIDTH. Apple publishes no mapping, so this one is stated in the header. */
static const char *fn_mf_width(NSFormattingUnitStyle style)
{
	switch (style) {
	case NSFormattingUnitStyleShort:	return "unit-width-narrow";
	case NSFormattingUnitStyleMedium:	return "unit-width-short";
	default:				return "unit-width-full-name";
	}
}

static const char *fn_mf_locale(NSLocale *locale)
{
	static char name[64];
	const char *identifier;

	identifier = locale != nil ? [[locale localeIdentifier] UTF8String] : uloc_getDefault();
	if (identifier == NULL) {
		identifier = "en_US";
	}
	{
		size_t i;

		for (i = 0; identifier[i] != '\0' && i + 2 < sizeof name; i++) {
			name[i] = (identifier[i] == '-') ? '_' : identifier[i];
		}
		name[i] = '\0';
	}
	return name;
}

@implementation NSMeasurementFormatter

- (id)init
{
	self = [super init];
	if (self == nil) {
		return nil;
	}
	_unitOptions = NSMeasurementFormatterUnitOptionsProvidedUnit;
	_unitStyle = NSFormattingUnitStyleMedium;
	_locale = nil;
	_numberFormatter = nil;
	return self;
}

- (void)dealloc
{
	[_locale release];
	[_numberFormatter release];
	[super dealloc];
}

/* ONE PLACE FORMATS A QUANTITY WITH A UNIT, and both doors go through it. */
- (nullable NSString *)fnStringForValue:(double)value cldr:(const char *)cldr
{
	char skeleton[128];
	UChar uskeleton[128];
	UChar out[192];
	char utf8[512];
	UErrorCode status = U_ZERO_ERROR;
	UNumberFormatter *formatter = NULL;
	UFormattedNumber *result = NULL;
	int32_t skeletonLength = 0;
	int32_t length = 0;
	int32_t used = 0;
	NSString *answer = nil;

	if (cldr == NULL) {
		return nil;
	}
	/* THE OPTION THAT NEEDS NO UNIT: ICU's `temperature-generic` renders "20°" rather than "20°C". */
	if ((_unitOptions & NSMeasurementFormatterUnitOptionsTemperatureWithoutUnit) && cldr != NULL &&
	    strstr(cldr, "temperature-") != NULL) {
		cldr = "temperature-generic";
	}
	snprintf(skeleton, sizeof skeleton, "measure-unit/%s %s", cldr, fn_mf_width(_unitStyle));
	u_strFromUTF8(uskeleton, 128, &skeletonLength, skeleton, -1, &status);
	if (U_FAILURE(status)) {
		return nil;
	}
	status = U_ZERO_ERROR;
	formatter = unumf_openForSkeletonAndLocale(uskeleton, skeletonLength, fn_mf_locale(_locale), &status);
	if (U_FAILURE(status) || formatter == NULL) {
		return nil;
	}
	status = U_ZERO_ERROR;
	result = unumf_openResult(&status);
	if (U_FAILURE(status) || result == NULL) {
		unumf_close(formatter);
		return nil;
	}
	status = U_ZERO_ERROR;
	unumf_formatDouble(formatter, value, result, &status);
	if (U_SUCCESS(status)) {
		status = U_ZERO_ERROR;
		length = unumf_resultToString(result, out, 192, &status);
		if (U_SUCCESS(status) && length > 0) {
			status = U_ZERO_ERROR;
			u_strToUTF8(utf8, (int32_t)sizeof utf8, &used, out, length, &status);
			if (U_SUCCESS(status)) {
				utf8[used] = '\0';
				answer = [NSString stringWithUTF8String:utf8];
			}
		}
	}
	unumf_closeResult(result);
	unumf_close(formatter);
	return answer;
}

- (nullable NSString *)stringFromMeasurement:(NSMeasurement *)measurement
{
	if (measurement == nil) {
		return nil;
	}
	return [self fnStringForValue:[measurement doubleValue] cldr:fn_mf_cldr([measurement unit])];
}

- (nullable NSString *)stringFromUnit:(NSUnit *)unit
{
	NSString *whole;
	NSRange space;

	whole = [self fnStringForValue:1.0 cldr:fn_mf_cldr(unit)];
	if (whole == nil) {
		return nil;
	}
	/* ICU FORMATS A QUANTITY WITH A UNIT AND HAS NO NAME-ONLY MODE, so the name is what follows the number
	 * "1". The boundary is the same one the components formatter's spell-out records: the quantity precedes
	 * the unit in the patterns this system ships. */
	space = [whole rangeOfString:@" "];
	if (space.location == NSNotFound) {
		return whole;
	}
	return [whole substringFromIndex:space.location + space.length];
}

- (nullable NSString *)stringForObjectValue:(nullable id)object
{
	if (![object isKindOfClass:[NSMeasurement class]]) {
		return nil;
	}
	return [self stringFromMeasurement:(NSMeasurement *)object];
}

- (NSMeasurementFormatterUnitOptions)unitOptions { return _unitOptions; }
- (void)setUnitOptions:(NSMeasurementFormatterUnitOptions)options { _unitOptions = options; }
- (NSFormattingUnitStyle)unitStyle { return _unitStyle; }
- (void)setUnitStyle:(NSFormattingUnitStyle)style { _unitStyle = style; }

- (NSLocale *)locale
{
	return _locale != nil ? _locale : [NSLocale currentLocale];
}

- (void)setLocale:(nullable NSLocale *)value
{
	id copy = [value copy];

	[_locale release];
	_locale = copy;
}

- (NSNumberFormatter *)numberFormatter
{
	if (_numberFormatter == nil) {
		_numberFormatter = [[NSNumberFormatter alloc] init];
	}
	return _numberFormatter;
}

- (void)setNumberFormatter:(nullable NSNumberFormatter *)value
{
	id retained = [value retain];

	[_numberFormatter release];
	_numberFormatter = retained;
}

@end
