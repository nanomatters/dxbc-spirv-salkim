#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::spirv {

void testSpirvNonUniform();

void runTests() {
  RUN_TEST(testSpirvNonUniform);
}

}
