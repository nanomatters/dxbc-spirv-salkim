#include "../../ir/ir_dominance.h"
#include "../../ir/passes/ir_pass_cse.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

/* Reference the original list walk, including declarative/null semantics. */
bool walkDefDominates(const Builder& builder, const DominanceGraph& graph, SsaDef a, SsaDef b) {
  if (!a || builder.getOp(a).isDeclarative()) return true;
  if (!b || builder.getOp(b).isDeclarative()) return false;
  auto blockA = graph.getBlockForDef(a);
  auto blockB = graph.getBlockForDef(b);
  if (blockA != blockB) return graph.dominates(blockA, blockB);
  while (b != a && b != blockB) b = builder.getPrev(b);
  return b == a;
}


void checkInstructionOrder(const Builder& builder, const DominanceGraph& graph) {
  std::vector<SsaDef> defs = { SsaDef() };
  for (const auto& op : builder) {
    if (op.getOpCode() != OpCode::eFunction && op.getOpCode() != OpCode::eFunctionEnd)
      defs.push_back(op.getDef());
  }
  for (auto a : defs) {
    for (auto b : defs)
      ok(graph.defDominates(a, b) == walkDefDominates(builder, graph, a, b));
  }
}


void testIrDominanceOrder() {
  Builder builder;
  auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entry, 1u, 1u, 1u));
  auto block = builder.add(Op::Label());
  auto constant = builder.makeConstant(0u);
  std::vector<SsaDef> defs;
  for (uint32_t i = 0u; i < 32u; i++)
    defs.push_back(builder.add(Op::Drain(ScalarType::eU32, constant)));
  auto end = builder.add(Op::Return());

  /* Neither recycled IDs nor the creation order describe list order. */
  builder.remove(defs[6]);
  auto reused = builder.addBefore(defs[2], Op::Drain(ScalarType::eU32, constant));
  ok(reused == defs[6]);
  builder.reorderBefore(defs[3], defs[18], defs[22]);
  builder.reorderAfter(defs[8], defs[15], defs[16]);

  DominanceGraph graph(builder);
  checkInstructionOrder(builder, graph);
  ok(graph.defDominates(reused, defs[2]));
  ok(!graph.defDominates(defs[2], reused));
  builder.remove(defs[31]);
  defs.pop_back();
  checkInstructionOrder(builder, graph);

  /* Repeated moves within the same block must not reuse an old position. */
  for (auto def : defs) {
    builder.reorderBefore(end, def, def);
    graph.notifyMoveBeforeTerminator(def, block);
    checkInstructionOrder(builder, graph);
  }
}


void testIrDominanceHoist(bool manual) {
  Builder builder;
  auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entry, 1u, 1u, 1u));
  auto header = builder.add(Op::Label());
  auto cond = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(true)));
  auto input = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(0u)));
  auto left = builder.add(Op::Label());
  auto right = builder.add(Op::Label());
  auto merge = builder.add(Op::Label());
  builder.rewriteOp(header, Op::LabelSelection(merge));
  auto branch = builder.addBefore(left, Op::BranchConditional(cond, left, right));
  auto leftEnd = builder.addBefore(right, Op::Branch(merge));
  auto rightEnd = builder.addBefore(merge, Op::Branch(merge));
  builder.add(Op::Return());

  std::vector<SsaDef> leftOps, rightOps;
  auto type = BasicType(ScalarType::eU32, 2u);
  for (uint32_t i = 1u; i <= 8u; i++) {
    auto value = builder.makeConstant(i);
    leftOps.push_back(builder.addBefore(leftEnd, Op::CompositeConstruct(type, input, value)));
    rightOps.push_back(builder.addBefore(rightEnd, Op::CompositeConstruct(type, input, value)));
    builder.addBefore(leftEnd, Op::Drain(type, leftOps.back()));
    builder.addBefore(rightEnd, Op::Drain(type, rightOps.back()));
  }

  /* Simulate CSE's cross-block movement and compare every query to the walk. */
  DominanceGraph graph(builder);
  checkInstructionOrder(builder, graph);
  ok(!graph.defDominates(leftOps[0], rightOps[0]));
  if (manual) {
    for (auto def : leftOps) {
      builder.reorderBefore(branch, def, def);
      graph.notifyMoveBeforeTerminator(def, header);
      checkInstructionOrder(builder, graph);
    }
    ok(graph.defDominates(leftOps[0], rightOps[0]));
  }
  ok(CsePass::runPass(builder, { }));
  uint32_t composites = 0u;
  for (const auto& op : builder) composites += op.getOpCode() == OpCode::eCompositeConstruct;
  ok(composites == 8u);
  DominanceGraph result(builder);
  for (auto def : leftOps)
    ok(result.getBlockForDef(def) == header);
  checkInstructionOrder(builder, result);
}


void testIrDominance() {
  RUN_TEST(testIrDominanceOrder);
  RUN_TEST(testIrDominanceHoist, false);
  RUN_TEST(testIrDominanceHoist, true);
}

}
