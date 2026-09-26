#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::sm3 {

void testSm3Lit();

void runTests() {
  RUN_TEST(testSm3Lit);
}

}
