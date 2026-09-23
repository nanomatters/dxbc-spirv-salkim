#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::spirv {

void testSpirvNonUniform();
void testSpirvCbvStorageClass();

void runTests() {
  RUN_TEST(testSpirvNonUniform);
  RUN_TEST(testSpirvCbvStorageClass);
}

}
