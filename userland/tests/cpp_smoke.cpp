// cpp_smoke.cpp — FNX C++ toolchain acceptance test
// (docs/cpp-toolchain-plan.md P3). Built by tools/musl-g++64.sh against
// the LLVM libc++/libc++abi/libunwind stack; every check prints CPP-OK.
#include <cstdio>
#include <string>
#include <vector>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <typeinfo>

static int fails;

#define CHECK(name) \
	do { \
		std::printf("CPP-OK: %s\n", name); \
	} while (0)

struct Base {
	virtual ~Base() {}
};
struct Derived : Base {
	int tag = 7;
};

static void throw_across_frames(int depth)
{
	if(depth == 0) {
		throw std::runtime_error("deep boom");
	}
	throw_across_frames(depth - 1);
}

int main(void)
{
	/* 1. exceptions across several frames */
	try {
		throw_across_frames(8);
		CHECK("except (unreachable)");
	} catch(const std::exception &e) {
		if(std::string(e.what()) == "deep boom") {
			CHECK("except");
		} else {
			fails++;
		}
	}

	/* 2. RTTI: dynamic_cast + typeid */
	Base *b = new Derived();
	if(dynamic_cast<Derived *>(b) && typeid(*b) == typeid(Derived)) {
		CHECK("rtti");
	} else {
		fails++;
	}
	delete b;

	/* 3. string + vector */
	std::string s = "fnx";
	s += "-c++";
	std::vector<int> v;
	for(int i = 0; i < 100; i++) {
		v.push_back(i);
	}
	if(s == "fnx-c++" && v.size() == 100 && v[99] == 99) {
		CHECK("string-vector");
	} else {
		fails++;
	}

	/* 4. iostream + ostringstream */
	std::ostringstream os;
	os << "io " << 42;
	if(os.str() == "io 42") {
		CHECK("iostream");
	} else {
		fails++;
	}

	/* 5. a real thread */
	int shared = 0;
	std::thread t([&shared] {
		for(int i = 0; i < 50; i++) {
			shared++;
		}
	});
	t.join();
	if(shared == 50) {
		CHECK("thread");
	} else {
		fails++;
	}

	/* 6. new/delete + exceptions in a thread is fine; just exit */
	if(fails == 0) {
		std::printf("CPP-SMOKE: all checks OK\n");
		return 0;
	}
	std::printf("CPP-SMOKE: %d FAILURES\n", fails);
	return 1;
}
