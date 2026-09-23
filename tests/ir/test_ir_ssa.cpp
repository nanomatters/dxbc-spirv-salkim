#include "../../ir/passes/ir_pass_ssa.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void checkIrBuilderUses(const Builder& builder);

void checkIrSsaTemps(const Builder& builder) {
  for (const auto& op : builder) {
    ok(op.getOpCode() != OpCode::eDclTmp);
    ok(op.getOpCode() != OpCode::eTmpLoad);
    ok(op.getOpCode() != OpCode::eTmpStore);
  }
  checkIrBuilderUses(builder);
}


void testIrSsaTempFanout(bool debug) {
  Builder builder;
  auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
  auto temp = builder.add(Op::DclTmp(ScalarType::eU32, entry));
  auto unused = builder.add(Op::DclTmp(ScalarType::eU32, entry));
  auto keep = builder.add(Op::DebugName(entry, "keep"));
  if (debug) {
    builder.add(Op::DebugName(temp, "temp"));
    builder.add(Op::DebugName(unused, "unused"));
  }
  builder.add(Op::Label());
  std::vector<std::pair<SsaDef, SsaDef>> results;
  for (uint32_t i = 0u; i < 2048u; i++) {
    auto value = builder.makeConstant(i);
    builder.add(Op::TmpStore(temp, value));
    auto load = builder.add(Op::TmpLoad(ScalarType::eU32, temp));
    auto drain = builder.add(Op::Drain(ScalarType::eU32, load));
    results.emplace_back(drain, value);
  }
  if (debug) builder.add(Op::DebugName(temp, "late debug user"));
  builder.add(Op::Return());

  SsaConstructionPass::runPass(builder);
  checkIrSsaTemps(builder);
  ok(builder.getOp(keep).getOpCode() == OpCode::eDebugName);
  for (auto [drain, value] : results)
    ok(SsaDef(builder.getOp(drain).getOperand(0u)) == value);
  SsaConstructionPass::runPass(builder);
  checkIrSsaTemps(builder);
}


void testIrSsaTempLoop() {
  Builder builder;
  auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
  auto temp = builder.add(Op::DclTmp(ScalarType::eU32, entry));
  builder.add(Op::DebugName(temp, "counter"));
  auto start = builder.add(Op::Label());
  auto cond = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(true)));
  auto header = builder.add(Op::Label());
  auto body = builder.add(Op::Label());
  auto end = builder.add(Op::Label());
  builder.rewriteOp(header, Op::LabelLoop(end, body));
  auto zero = builder.makeConstant(0u);
  builder.addBefore(header, Op::TmpStore(temp, zero));
  builder.addBefore(header, Op::Branch(header));
  builder.addBefore(body, Op::BranchConditional(cond, body, end));
  auto load = builder.addBefore(end, Op::TmpLoad(ScalarType::eU32, temp));
  auto increment = builder.addBefore(end, Op::IAdd(ScalarType::eU32, load, builder.makeConstant(1u)));
  builder.addBefore(end, Op::TmpStore(temp, increment));
  builder.addBefore(end, Op::Branch(header));
  auto result = builder.add(Op::TmpLoad(ScalarType::eU32, temp));
  auto drain = builder.add(Op::Drain(ScalarType::eU32, result));
  builder.add(Op::Return());

  SsaConstructionPass::runPass(builder);
  checkIrSsaTemps(builder);
  const auto& phi = builder.getOpForOperand(builder.getOp(drain), 0u);
  ok(phi.getOpCode() == OpCode::ePhi);
  ok(phi.getOperandCount() == 4u);
  ok(SsaDef(builder.getOp(increment).getOperand(0u)) == phi.getDef());
  bool entryFound = false, backEdgeFound = false;
  forEachPhiOperand(phi, [&] (SsaDef block, SsaDef value) {
    entryFound |= block == start && value == zero;
    backEdgeFound |= block == body && value == increment;
  });
  ok(entryFound);
  ok(backEdgeFound);
}


void testIrSsa() {
  RUN_TEST(testIrSsaTempFanout, false);
  RUN_TEST(testIrSsaTempFanout, true);
  RUN_TEST(testIrSsaTempLoop);
}

}
