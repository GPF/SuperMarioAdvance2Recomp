// KOS SH-4 libstdc++ omits these C99 declarations from namespace std.
// Re-export the existing newlib declarations without adding implementations.
#pragma once
#include <stdio.h>
#include <stdlib.h>
namespace std {
using ::snprintf;
using ::vsnprintf;
using ::strtoull;
using ::strtoll;
using ::atoll;
}
