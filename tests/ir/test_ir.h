#pragma once

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

void testIrArithmetic();
void testIrBuilder();
void testIrCse();
void testIrDominance();
void testIrInputMap();
void testIrOp();
void testIrSerialize();
void testIrSsa();
void testIrType();
void testIrTypePropagation();

void runTests() {
  RUN_TEST(testIrType);
  RUN_TEST(testIrOp);
  RUN_TEST(testIrArithmetic);
  RUN_TEST(testIrBuilder);
  RUN_TEST(testIrCse);
  RUN_TEST(testIrDominance);
  RUN_TEST(testIrSerialize);
  RUN_TEST(testIrSsa);
  RUN_TEST(testIrTypePropagation);
  RUN_TEST(testIrInputMap);
}

}
