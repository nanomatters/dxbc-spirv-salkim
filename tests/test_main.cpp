#include <iostream>

#ifdef DXBC_SPV_ENABLE_SM3
#include "./sm3/test_sm3.h"
#endif

#ifdef DXBC_SPV_ENABLE_SM5
#include "./dxbc/test_dxbc.h"
#endif

#include "./ir/test_ir.h"

#ifdef DXBC_SPV_ENABLE_SPIRV
#include "./spirv/test_spirv.h"
#endif

#include "./util/test_util.h"

namespace dxbc_spv::tests {

TestState g_testState;

void runTests() {
  util::runTests();
  ir::runTests();

#ifdef DXBC_SPV_ENABLE_SM3
  sm3::runTests();
#endif

#ifdef DXBC_SPV_ENABLE_SPIRV
  spirv::runTests();
#endif

#ifdef DXBC_SPV_ENABLE_SM5
  dxbc::runTests();
#endif

  std::cerr << "Tests run: " << g_testState.testsRun
    << ", failed: " << g_testState.testsFailed << std::endl;
}

}

int main(int, char**) {
  dxbc_spv::tests::runTests();
  return dxbc_spv::tests::g_testState.testsFailed ? 1 : 0;
}
