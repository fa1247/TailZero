/**
 * TailZero - unit test entry point.
 * SPDX-License-Identifier: MIT
 *
 * Optional argument: a substring filter on test names.
 */
#include "test_harness.h"

int main(int argc, char** argv) {
    return tztest::run_all(argc, argv);
}
