#pragma once
// Minimal dependency-free test harness shared by the core test files.
#include <cstdio>

namespace nltest {
inline int failed = 0;
inline int checks = 0;
}

#define CHECK(cond) do { ++nltest::checks; if (!(cond)) { ++nltest::failed; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } } while (0)
