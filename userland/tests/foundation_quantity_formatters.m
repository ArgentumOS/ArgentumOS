/*
 * foundation_quantity_formatters.m
 *
 * Copyright © 2026 Kyle J. Cardoza. MIT licensed — see LICENSE.
 * SPDX-License-Identifier: MIT
 *
 * THE FORMATTER TRIO (§62.50): `NSLengthFormatter`, `NSMassFormatter`, `NSEnergyFormatter`. Apple added one class
 * per KIND OF QUANTITY at 10.8, each with an enum of the units it knows and four doors for converting, naming and
 * rendering.
 *
 * WHAT IS MEASURED IS THE ARITHMETIC AND THE CHOICES, NOT THE NAMES. A formatter that declared every unit and
 * converted nothing would pass a check that counted declarations, so every value here is an ABSOLUTE one measured
 * against the definition of the unit: 1500 m is 1.5 km, half a kilogram is 500 g, a person 1.75 m tall is five
 * feet something. THE CHOOSER IS MEASURED IN BOTH DIRECTIONS — a small measurement must pick a SMALL unit and a
 * large one a large unit — because a chooser stuck at one end of the table would satisfy half the checks and look
 * right.
 *
 * THE LOCALE STANCE IS NAMED RATHER THAN ASSUMED: this library formats with the root locale, so the default family
 * is METRIC and `forPersonHeightUse` / `forPersonMassUse` / `forFoodEnergyUse` are what ask for the other one.
 */
#import <Foundation/Foundation.h>
#include <stdio.h>

static int okc = 0, failc = 0;

static void check(const char *name, BOOL held, NSString *why)
{
	if(held) {
		okc++;
		printf("FOUNDATION-QTYFMT %s ok\n", name);
	} else {
		failc++;
		printf("FOUNDATION-QTYFMT %s FAIL: %s\n", name, [why UTF8String]);
	}
}

int main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);

	/* --- THE VOCABULARY -------------------------------------------------------------------------------- */
	{
		NSSet *lengthUnits = [NSSet setWithArray:[NSArray arrayWithObjects:
			[NSNumber numberWithInt:NSLengthFormatterUnitMillimeter],
			[NSNumber numberWithInt:NSLengthFormatterUnitCentimeter],
			[NSNumber numberWithInt:NSLengthFormatterUnitMeter],
			[NSNumber numberWithInt:NSLengthFormatterUnitKilometer],
			[NSNumber numberWithInt:NSLengthFormatterUnitInch],
			[NSNumber numberWithInt:NSLengthFormatterUnitFoot],
			[NSNumber numberWithInt:NSLengthFormatterUnitYard],
			[NSNumber numberWithInt:NSLengthFormatterUnitMile], nil]];
		NSSet *massUnits = [NSSet setWithArray:[NSArray arrayWithObjects:
			[NSNumber numberWithInt:NSMassFormatterUnitGram],
			[NSNumber numberWithInt:NSMassFormatterUnitKilogram],
			[NSNumber numberWithInt:NSMassFormatterUnitOunce],
			[NSNumber numberWithInt:NSMassFormatterUnitPound],
			[NSNumber numberWithInt:NSMassFormatterUnitStone], nil]];
		NSSet *energyUnits = [NSSet setWithArray:[NSArray arrayWithObjects:
			[NSNumber numberWithInt:NSEnergyFormatterUnitJoule],
			[NSNumber numberWithInt:NSEnergyFormatterUnitKilojoule],
			[NSNumber numberWithInt:NSEnergyFormatterUnitCalorie],
			[NSNumber numberWithInt:NSEnergyFormatterUnitKilocalorie], nil]];

		check("the-three-formatters-are-declared-and-their-units-are-distinct",
		      [[[NSLengthFormatter alloc] init] isKindOfClass:[NSFormatter class]] &&
		      [[[NSMassFormatter alloc] init] isKindOfClass:[NSFormatter class]] &&
		      [[[NSEnergyFormatter alloc] init] isKindOfClass:[NSFormatter class]] &&
		      [lengthUnits count] == 8 && [massUnits count] == 5 && [energyUnits count] == 4,
		      [NSString stringWithFormat:@"%lu length, %lu mass, %lu energy unit(s), each a formatter",
			(unsigned long)[lengthUnits count], (unsigned long)[massUnits count],
			(unsigned long)[energyUnits count]]);
	}

	/* --- A VALUE IN A UNIT THE CALLER NAMES ------------------------------------------------------------ */
	{
		NSLengthFormatter *length = [[NSLengthFormatter alloc] init];
		NSMassFormatter *mass = [[NSMassFormatter alloc] init];
		NSEnergyFormatter *energy = [[NSEnergyFormatter alloc] init];
		NSString *metres = [length stringFromValue:1.5 unit:NSLengthFormatterUnitMeter];
		NSString *feet = [length stringFromValue:6 unit:NSLengthFormatterUnitFoot];
		NSString *pounds = [mass stringFromValue:154 unit:NSMassFormatterUnitPound];
		NSString *kiloCalories = [energy stringFromValue:250 unit:NSEnergyFormatterUnitKilocalorie];

		check("a-value-in-a-named-unit-renders-that-unit",
		      [metres isEqual:@"1.5 m"] && [feet isEqual:@"6 ft"] &&
		      [pounds isEqual:@"154 lb"] && [kiloCalories isEqual:@"250 kcal"] &&
		      [[length unitStringFromValue:3 unit:NSLengthFormatterUnitKilometer] isEqual:@"km"],
		      [NSString stringWithFormat:@"1.5 m -> \"%@\"; 6 -> \"%@\"; 154 -> \"%@\"; 250 -> \"%@\"",
			metres, feet, pounds, kiloCalories]);
		check("a-number-formatter-set-is-the-one-returned",
		      [length numberFormatter] == nil &&
		      [mass numberFormatter] == nil &&
		      [energy numberFormatter] == nil,
		      @"the property starts empty on all three, so a caller can tell whether they set one");
	}

	/* --- THE NATURAL UNIT, MEASURED IN BOTH DIRECTIONS -------------------------------------------------- */
	{
		NSLengthFormatter *length = [[NSLengthFormatter alloc] init];
		NSMassFormatter *mass = [[NSMassFormatter alloc] init];
		NSEnergyFormatter *energy = [[NSEnergyFormatter alloc] init];
		NSLengthFormatterUnit usedUnit = 0;
		NSString *kilometres = [length stringFromMeters:1500];
		NSString *centimetres = [length stringFromMeters:0.5];
		NSString *millimetres = [length stringFromMeters:0.0005];
		NSString *grams = [mass stringFromKilograms:0.5];
		NSString *kilojoules = [energy stringFromJoules:1000];
		NSString *unitName = [length unitStringFromMeters:1500 usedUnit:&usedUnit];

		check("a-value-with-no-unit-gets-the-natural-one",
		      [kilometres isEqual:@"1.5 km"] && [centimetres isEqual:@"50 cm"] &&
		      [millimetres isEqual:@"0.5 mm"] && [grams isEqual:@"500 g"] &&
		      [kilojoules isEqual:@"1 kJ"] &&
		      usedUnit == NSLengthFormatterUnitKilometer && [unitName isEqual:@"km"],
		      [NSString stringWithFormat:@"1500 m -> \"%@\"; 0.5 -> \"%@\"; 0.0005 -> \"%@\"; 0.5 kg -> \"%@\"; "
			@"1000 J -> \"%@\"; usedUnit %d -> \"%@\"", kilometres, centimetres, millimetres, grams,
			kilojoules, (int)usedUnit, unitName]);

		check("the-three-quantities-use-their-own-base",
		      [[length stringFromValue:1 unit:NSLengthFormatterUnitMeter] isEqual:@"1 m"] &&
		      [[mass stringFromValue:1 unit:NSMassFormatterUnitKilogram] isEqual:@"1 kg"] &&
		      [[length stringFromValue:1000 unit:NSLengthFormatterUnitMillimeter]
			hasPrefix:@"1000"] &&
		      [energy stringFromValue:4184 unit:NSEnergyFormatterUnitJoule] != nil,
		      @"a metre, a kilogram and a joule are each their own base, and the tables are not shared");
	}

	/* --- THE PERSON FLAGS, WHICH ARE THE ONLY THING THAT CHANGES THE FAMILY ----------------------------- */
	{
		NSLengthFormatter *height = [[NSLengthFormatter alloc] init];
		NSMassFormatter *personMass = [[NSMassFormatter alloc] init];
		NSEnergyFormatter *food = [[NSEnergyFormatter alloc] init];

		[height setForPersonHeightUse:YES];
		[personMass setForPersonMassUse:YES];
		[food setForFoodEnergyUse:YES];

		{
			NSString *tall = [height stringFromMeters:1.75];
			NSString *heavy = [personMass stringFromKilograms:70];
			NSString *meal = [food stringFromJoules:836800];

			/* 1.75 m is 5 ft 8.9 in; 70 kg is 154.3 lb; 836800 J is 200 kcal. Each is an ABSOLUTE value: a flag
			 * that changed nothing would leave the metric render in place and fail here. */
			check("each-person-flag-changes-the-family",
			      [tall hasPrefix:@"5 ft"] && [tall hasSuffix:@"in"] &&
			      [heavy isEqual:@"154.3 lb"] && [meal isEqual:@"200 kcal"],
			      [NSString stringWithFormat:@"1.75 m -> \"%@\"; 70 kg -> \"%@\"; 836800 J -> \"%@\"",
				tall, heavy, meal]);

			/* AND THE SAME MEASUREMENT WITHOUT THE FLAG IS METRIC, which is the control: the flag is the only
			 * difference between these two strings. */
			[height setForPersonHeightUse:NO];
			check("without-the-flag-the-same-measurement-is-metric",
			      [[height stringFromMeters:1.75] isEqual:@"1.75 m"],
			      [NSString stringWithFormat:@"1.75 m without the flag -> \"%@\"",
				[height stringFromMeters:1.75]]);
		}
	}

	/* --- A UNIT THIS FORMATTER DOES NOT KNOW ------------------------------------------------------------ */
	{
		NSLengthFormatter *length = [[NSLengthFormatter alloc] init];
		NSString *rendered = [length stringFromValue:1.5 unit:(NSLengthFormatterUnit)77];
		NSString *named = [length unitStringFromValue:1.5 unit:(NSLengthFormatterUnit)77];

		check("an-unknown-unit-names-nothing-and-renders-a-bare-number",
		      rendered != nil && [named isEqual:@""] && [rendered isEqual:@"1.5"],
		      [NSString stringWithFormat:@"unit 77 rendered \"%@\" and named \"%@\"", rendered, named]);
	}

	printf("FOUNDATION-QTYFMT RESULT ok=%d fail=%d\n", okc, failc);
	printf("FOUNDATION-QTYFMT-STATUS=%d\n", failc ? 1 : 0);
	printf("FOUNDATION-QTYFMT DONE\n");
	return failc ? 1 : 0;
}
