#include "../../ir/passes/ir_pass_arithmetic.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrArithmeticSelectMerge(uint32_t variant) {
  Builder builder;
  test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::Label());
  auto cond = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(true)));
  auto otherCond = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(false)));
  auto a = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(3u)));
  auto b = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(5u)));
  auto c = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(7u)));
  auto d = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(11u)));
  auto left = builder.add(Op::Select(ScalarType::eU32, cond,
    variant == 5u ? builder.makeConstant(3u) : a, b));
  auto right = builder.add(Op::Select(ScalarType::eU32,
    variant == 3u ? otherCond : cond,
    variant == 5u ? builder.makeConstant(7u) : c, d));
  auto result = builder.add(Op::IMul(ScalarType::eU32,
    variant == 1u ? a : left, variant == 2u ? c : right));
  auto sink = builder.add(Op::Drain(ScalarType::eU32, result));

  if (variant >= 4u)
    builder.add(Op::Drain(ScalarType::eU32, left, right));

  builder.add(Op::Return());
  bool merge = variant == 0u || variant == 5u;
  bool progress = ArithmeticPass::runPass(builder, { });
  ok(progress || !merge);
  /* Newly inserted operations precede the iterator and fold on the next pass. */
  if (variant == 5u)
    ok(ArithmeticPass::runPass(builder, { }));
  const auto& op = builder.getOpForOperand(builder.getOp(sink), 0u);
  ok(op.getOpCode() == (merge ? OpCode::eSelect : OpCode::eIMul));

  if (merge) {
    ok(SsaDef(op.getOperand(0u)) == cond);
    const auto& trueOp = builder.getOpForOperand(op, 1u);
    const auto& falseOp = builder.getOpForOperand(op, 2u);
    if (variant == 5u) {
      ok(trueOp.isConstant());
      ok(uint32_t(trueOp.getOperand(0u)) == 21u);
    } else {
      ok(trueOp.getOpCode() == OpCode::eIMul);
      ok(SsaDef(trueOp.getOperand(0u)) == a);
      ok(SsaDef(trueOp.getOperand(1u)) == c);
    }
    ok(falseOp.getOpCode() == OpCode::eIMul);
    ok(SsaDef(falseOp.getOperand(0u)) == b);
    ok(SsaDef(falseOp.getOperand(1u)) == d);
  }
}


void testIrArithmetic() {
  /* Matching selects, non-select on either side, mismatched conditions,
   * shared selects, and shared selects with a constant branch. */
  for (uint32_t variant = 0u; variant < 6u; variant++)
    RUN_TEST(testIrArithmeticSelectMerge, variant);
}

}
