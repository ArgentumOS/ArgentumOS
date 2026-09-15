/* interface_codegen — Weaver W4 acceptance: the class emitters are
 * deterministic and produce the shape the tree expects (W4). */
#include <argentum/argentum.h>

#include <cstdio>
#include <string>

using namespace argentum;

int
main()
{
	InterfaceClassInfo c;

	c.name = "MyController";
	c.superClass = "Object";
	c.outlets = "greeting";
	c.actions = "doThing,reset";

	std::string h = interfaceEmitClassHeader("MyController", c);
	std::string s1 = interfaceEmitClassSource("MyController", c);
	std::string s2 = interfaceEmitClassSource("MyController", c);

	if (s1 != s2) {
		std::printf("W4: FAIL (not deterministic)\n");
		return 1;
	}
	const char *needH[] = {
		"#pragma once",
		"class MyController {",
		"void doThing();",
		"void reset();",
		"argentum::View *greeting = nullptr;",
	};
	for (const char *n : needH) {
		if (h.find(n) == std::string::npos) {
			std::printf("W4: FAIL header lacks `%s`\n", n);
			return 1;
		}
	}
	const char *needS[] = {
		"#include \"MyController.h\"",
		"MyController::MyController() {}",
		"void MyController::doThing() {",
		"void MyController::reset() {",
		"weaverBind_MyController",
		"d.bind(target, \"doThing\", [&obj]() { obj.doThing(); });",
		"d.bind(target, \"reset\", [&obj]() { obj.reset(); });",
	};
	for (const char *n : needS) {
		if (s1.find(n) == std::string::npos) {
			std::printf("W4: FAIL source lacks `%s`\n", n);
			return 1;
		}
	}
	std::printf("W4-OK (the class emitters are deterministic and keep "
		    "outlets/actions/bindings)\n");
	return 0;
}
