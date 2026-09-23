#include "../../ir/passes/ir_pass_cse.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrCsePhiProgress(bool duplicate) {
  Builder builder;
  auto entryPoint = test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entryPoint, 1u, 1u, 1u));

  auto entry = builder.add(Op::Label());
  auto condition = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(true)));
  auto left = builder.add(Op::Label());
  auto right = builder.add(Op::Label());
  auto merge = builder.add(Op::Label());

  builder.rewriteOp(entry, Op::LabelSelection(merge));
  builder.addBefore(left, Op::BranchConditional(condition, left, right));
  builder.addBefore(right, Op::Branch(merge));
  builder.addBefore(merge, Op::Branch(merge));

  auto one = builder.makeConstant(1u);
  auto two = builder.makeConstant(2u);
  auto three = builder.makeConstant(3u);

  auto phiA = builder.add(Op::Phi(ScalarType::eU32).addPhi(left, one).addPhi(right, two));
  auto phiB = builder.add(Op::Phi(ScalarType::eU32).addPhi(left, one).addPhi(right, duplicate ? two : three));
  auto addA = builder.add(Op::IAdd(ScalarType::eU32, phiA, three));
  auto addB = builder.add(Op::IAdd(ScalarType::eU32, phiB, three));
  auto drain = builder.add(Op::Drain(ScalarType::eU32, addA, addB));
  builder.add(Op::Return());

  /* Phi deduplication happens after ordinary expressions have been visited.
   * It must report progress so that their users get another CSE pass. */
  ok(CsePass::runPass(builder, { }) == duplicate);

  uint32_t phiCount = 0u;
  for (const auto& op : builder)
    phiCount += op.getOpCode() == OpCode::ePhi;

  ok(phiCount == (duplicate ? 1u : 2u));
  ok(builder.getOp(addA).getOperand(0u) == Operand(phiA));
  ok(builder.getOp(addB).getOperand(0u) == Operand(duplicate ? phiA : phiB));

  ok(CsePass::runPass(builder, { }) == duplicate);
  ok(builder.getOp(drain).getOperand(0u) == Operand(addA));
  ok(builder.getOp(drain).getOperand(1u) == Operand(duplicate ? addA : addB));
  ok(!CsePass::runPass(builder, { }));
}


void testIrCse() {
  RUN_TEST(testIrCsePhiProgress, false);
  RUN_TEST(testIrCsePhiProgress, true);
}

}
