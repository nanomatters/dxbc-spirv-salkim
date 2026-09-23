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


void testIrCseDependentUsers() {
  Builder builder;
  test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::Label());
  auto input = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(0u)));
  std::vector<std::pair<SsaDef, SsaDef>> results;
  /* Force several table rehashes while rewrites make later users identical. */
  for (uint32_t i = 1u; i <= 512u; i++) {
    auto value = builder.makeConstant(i);
    auto a = builder.add(Op::IMul(ScalarType::eU32, input, value));
    auto b = builder.add(Op::IMul(ScalarType::eU32, input, value));
    auto x = builder.add(Op::IAdd(ScalarType::eU32, a, a));
    auto y = builder.add(Op::IAdd(ScalarType::eU32, b, b));
    auto sink = builder.add(Op::Drain(ScalarType::eU32, x, y));
    results.emplace_back(sink, x);
  }
  builder.add(Op::Return());
  ok(CsePass::runPass(builder, { }));
  for (auto [sink, value] : results) {
    ok(SsaDef(builder.getOp(sink).getOperand(0u)) == value);
    ok(SsaDef(builder.getOp(sink).getOperand(1u)) == value);
  }
  ok(!CsePass::runPass(builder, { }));
}


void testIrCseLoopPhiRewrite() {
  Builder builder;
  test_api::setupTestFunction(builder, ShaderStage::eCompute);
  auto start = builder.add(Op::Label());
  auto condition = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(true)));
  auto header = builder.add(Op::Label());
  auto body = builder.add(Op::Label());
  auto end = builder.add(Op::Label());
  builder.rewriteOp(header, Op::LabelLoop(end, body));
  builder.addBefore(header, Op::Branch(header));
  auto zero = builder.makeConstant(0u);
  auto one = builder.makeConstant(1u);
  auto phiA = builder.addBefore(body, Op::Phi(ScalarType::eU32).addPhi(start, zero));
  auto phiB = builder.addBefore(body, Op::Phi(ScalarType::eU32).addPhi(start, zero));
  auto outA = builder.addBefore(body, Op::IMul(ScalarType::eU32, phiA, phiA));
  auto outB = builder.addBefore(body, Op::IMul(ScalarType::eU32, phiB, phiB));
  builder.addBefore(body, Op::BranchConditional(condition, body, end));
  auto nextA = builder.addBefore(end, Op::IAdd(ScalarType::eU32, phiA, one));
  auto nextB = builder.addBefore(end, Op::IAdd(ScalarType::eU32, phiA, one));
  builder.addBefore(end, Op::Branch(header));
  builder.rewriteOp(phiA, Op::Phi(ScalarType::eU32).addPhi(start, zero).addPhi(body, nextA));
  builder.rewriteOp(phiB, Op::Phi(ScalarType::eU32).addPhi(start, zero).addPhi(body, nextB));
  auto sink = builder.add(Op::Drain(ScalarType::eU32, outA, outB));
  builder.add(Op::Return());

  /* Deduplicating the increments first changes backward phi operands.
   * Merging the phis then changes operations already visited by CSE. */
  ok(CsePass::runPass(builder, { }));
  ok(!builder.getOp(nextB));
  ok(!builder.getOp(phiB));
  ok(SsaDef(builder.getOp(outB).getOperand(0u)) == phiA);
  ok(CsePass::runPass(builder, { }));
  ok(SsaDef(builder.getOp(sink).getOperand(0u)) == outA);
  ok(SsaDef(builder.getOp(sink).getOperand(1u)) == outA);
  ok(!CsePass::runPass(builder, { }));
}


void testIrCse() {
  RUN_TEST(testIrCsePhiProgress, false);
  RUN_TEST(testIrCsePhiProgress, true);
  RUN_TEST(testIrCseDependentUsers);
  RUN_TEST(testIrCseLoopPhiRewrite);
}

}
