#pragma once

#include <cstdio>

// sma2_cart_aot_diag_run() returns exactly 1 for aggregate success and 0 for
// failure. Treat any unexpected value as failure as well.
inline int sma2_cart_aot_diag_report_exit(int result) {
    const bool passed = result == 1;
    std::fprintf(stderr, "SMA2_BOUNDED_AOT_DIAG=%s\n", passed ? "PASS" : "FAIL");
    return passed ? 0 : 1;
}
