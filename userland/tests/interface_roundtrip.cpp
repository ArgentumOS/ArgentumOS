/*
 * Weaver IB0 acceptance (docs/design/weaver-plan.md §8): the INTERFACE
 * DOCUMENT ROUND-TRIPS.
 *
 * For each fixture: write it, load it, emit it, write THAT, load it again,
 * emit again — and require the two emissions to be byte-identical. That is the
 * property the acceptance names. But idempotence ALONE is satisfied by
 * dropping every field, so each fixture also asserts on the content that must
 * survive: the class, the identifiers, each scalar kind, and the strut binding.
 *
 * Fixtures are embedded on purpose: the probe needs no staged data, and a
 * fixture that breaks is one edit away in this file.
 *
 * Display-free by design — the model, the emitter and the libconfig reader
 * need no X connection, so IB0 proves itself without a window, without input,
 * and without any staged control. Those come later.
 *
 * Prints `IB0: ...` lines and a final `IB0-OK` / `IB0-FAIL`; the exit code is
 * what the host case asserts.
 */
#include <argentum/argentum.h>

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace argentum;

/* ---------- fixtures ---------- */

static const char *const TEXT1 = R"(# a small interface
version = 1
interface = {
	class = "Window"
	frame = {
		x = 200
		y = 120
		w = 480
		h = 320
	}

	child0 = {
		class = "Label"
		id = "greeting"
		frame = {
			x = 20
			y = 16
			w = 240
			h = 20
		}
		title = "Hello"
	}

	child1 = {
		class = "Button"
		id = "okButton"
		frame = {
			x = 370
			y = 280
			w = 90
			h = 24
		}
		title = "OK"
		enabled = true
		scale = 1.5
		count = 3
		strut0 = {
			edge = "left"
			ref = "greeting"
			refEdge = "right"
			offset = 8
		}
	}
}
)";

static const char *const EXP1[] = {
	"class = \"Window\"",
	"id = \"greeting\"",
	"id = \"okButton\"",
	"title = \"Hello\"",
	"title = \"OK\"",
	"enabled = true",
	"scale = 1.5",
	"count = 3",
	"child0 = {",
	"child1 = {",
	"edge = \"left\"",
	"ref = \"greeting\"",
	"refEdge = \"right\"",
	"offset = 8",
	nullptr
};

/* the four escapes parse_quoted() understands, and nothing else: libconfig
 * treats any other backslash sequence as a parse error, so a string that is
 * not escaped to exactly this set cannot be read back at all */
static const char *const TEXT2 = R"(version = 1
interface = {
	class = "Label"
	frame = {
		x = 0
		y = 0
		w = 10
		h = 10
	}
	title = "say \"hi\" \\ ok"
	note = "line\nnext\ttab"
}
)";

static const char *const EXP2[] = {
	"class = \"Label\"",
	"title = \"say \\\"hi\\\" \\\\ ok\"",
	"note = \"line\\nnext\\ttab\"",
	nullptr
};

/* minimal: no identifier, no properties, no children */
static const char *const TEXT3 = R"(version = 1
interface = {
	class = "Label"
	frame = {
		x = 0
		y = 0
		w = 10
		h = 10
	}
}
)";

static const char *const EXP3[] = {
	"class = \"Label\"",
	"!id = ",
	nullptr
};

/* tolerant reads (plan D7): an array is skipped with a warning, and what can
 * be read is still read */
static const char *const TEXT4 = R"(version = 1
interface = {
	class = "Box"
	frame = {
		x = 0
		y = 0
		w = 100
		h = 100
	}
	title = "kept"
	items = [ 1, 2, 3 ]
}
)";

static const char *const EXP4[] = {
	"class = \"Box\"",
	"title = \"kept\"",
	"!items",
	nullptr
};

struct Fixture {
	const char *name;
	const char *text;
	const char *const *expect;
};

static const Fixture FIXTURES[] = {
	{ "window, two children, every scalar kind, a strut", TEXT1, EXP1 },
	{ "the four escapes", TEXT2, EXP2 },
	{ "minimal node", TEXT3, EXP3 },
	{ "an array is skipped, not fatal", TEXT4, EXP4 },
};

/* ---------- helpers ---------- */

static bool
writeFile(const std::string &path, const std::string &data)
{
	FILE *f = std::fopen(path.c_str(), "w");
	bool ok;

	if (!f) {
		return false;
	}
	ok = (std::fwrite(data.data(), 1, data.size(), f) == data.size());
	if (std::fclose(f) != 0) {
		ok = false;
	}
	return ok;
}

static std::string
lineAt(const std::string &s, size_t pos)
{
	size_t ls = (pos == 0) ? 0 : s.rfind('\n', pos - 1);
	size_t le = s.find('\n', pos);

	ls = (ls == std::string::npos) ? 0 : ls + 1;
	le = (le == std::string::npos) ? s.size() : le;
	return s.substr(ls, le - ls);
}

static std::string
firstDiff(const std::string &a, const std::string &b)
{
	size_t i = 0;
	char buf[64];

	while (i < a.size() && i < b.size() && a[i] == b[i]) {
		i++;
	}
	std::snprintf(buf, sizeof(buf), "byte %d whole-file differs: `",
		      (int) (a.size() == b.size() ? i : i));
	return std::string(buf) + lineAt(a, i) + "` vs `" + lineAt(b, i) + "`";
}

static bool
runFixture(const Fixture &fx, const std::string &dir, std::string &why)
{
	const std::string pathA = dir + "/ib0_from.conf";
	const std::string pathB = dir + "/ib0_again.conf";
	InterfaceDocument d1;
	InterfaceDocument d2;
	std::string err, e1, e2;

	if (!writeFile(pathA, fx.text)) {
		why = "cannot write " + pathA;
		return false;
	}
	if (!interfaceLoadFile(pathA.c_str(), d1, err)) {
		why = "load: " + err;
		return false;
	}
	e1 = interfaceEmit(d1);
	for (int i = 0; fx.expect[i]; i++) {
		const char *needle = fx.expect[i];
		bool absent = (needle[0] == '!');
		const char *n = absent ? needle + 1 : needle;
		bool found = (e1.find(n) != std::string::npos);

		if (found == absent) {
			why = std::string(absent ? "emitted text still has: "
						 : "emitted text lost: ") + n;
			return false;
		}
	}
	if (!writeFile(pathB, e1)) {
		why = "cannot write " + pathB;
		return false;
	}
	if (!interfaceLoadFile(pathB.c_str(), d2, err)) {
		why = "reload: " + err;
		return false;
	}
	e2 = interfaceEmit(d2);
	if (e1 != e2) {
		why = "not idempotent - " + firstDiff(e1, e2);
		return false;
	}
	return true;
}

/* Pick a writable temporary directory. HOME IS NOT A GUIDE HERE: the guest's
 * serial console shell runs as root with HOME=/, so trusting it produced the
 * path `//Temporary Files` and every fixture failed to write. Try the FSH temp
 * directories and take the first that accepts a write. */
static const char *const TEMP_DIRS[] = {
	"/System/Temporary Files",
	"/Users/Admin/Temporary Files",
};

static std::string
pickTempDir(void)
{
	for (size_t i = 0; i < sizeof(TEMP_DIRS) / sizeof(TEMP_DIRS[0]); i++) {
		std::string t = std::string(TEMP_DIRS[i]) + "/ib0_probe.tmp";

		if (writeFile(t, "x")) {
			std::remove(t.c_str());
			return TEMP_DIRS[i];
		}
	}
	return "";
}

int
main(void)
{
	const std::string dir = pickTempDir();
	const int total = (int) (sizeof(FIXTURES) / sizeof(FIXTURES[0]));
	int failed = 0;

	if (dir.empty()) {
		std::printf("IB0-FAIL (no writable temporary directory: tried");
		for (size_t i = 0; i < sizeof(TEMP_DIRS) / sizeof(TEMP_DIRS[0]); i++) {
			std::printf(" %s", TEMP_DIRS[i]);
		}
		std::printf(")\n");
		return 1;
	}
	std::printf("IB0: interface document round trip, %d fixture(s) in %s\n",
		    total, dir.c_str());
	for (int i = 0; i < total; i++) {
		std::string why;

		if (runFixture(FIXTURES[i], dir, why)) {
			std::printf("IB0: fixture %d (%s) OK\n", i + 1,
				    FIXTURES[i].name);
		} else {
			failed++;
			std::printf("IB0: fixture %d (%s) FAIL: %s\n", i + 1,
				    FIXTURES[i].name, why.c_str());
		}
		std::fflush(stdout);
	}
	if (failed) {
		std::printf("IB0-FAIL (%d of %d fixture(s) failed)\n", failed,
			    total);
		return 1;
	}
	std::printf("IB0-OK (%d/%d fixtures, emissions idempotent)\n", total,
		    total);
	return 0;
}
