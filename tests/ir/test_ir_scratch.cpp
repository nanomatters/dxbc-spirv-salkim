#include "../../ir/passes/ir_pass_scratch.h"
#include "../../ir/passes/ir_pass_arithmetic.h"
#include "../../ir/passes/ir_pass_scalarize.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrScratchCbvOffsets() {
  for (bool constantFirst : { false, true }) {
    for (bool sharedBase : { false, true }) {
      Builder builder;
      auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
      auto scratch = builder.add(Op::DclScratch(Type(ScalarType::eU32).addArrayDimension(3u), entry));
      auto cbv = builder.add(Op::DclCbv(Type(ScalarType::eU32, 4u).addArrayDimension(64u), entry, 0u, 0u, 1u));
      builder.add(Op::Label());
      auto base = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(0u)));
      auto otherBase = sharedBase ? base : builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(1u)));
      auto descriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbv, builder.makeConstant(0u)));

      for (uint32_t i = 0u; i < 2u; i++) {
        auto dynamic = i ? otherBase : base;
        /* Different bases must not be merged, even when doubling just the
         * constants happens to produce the expected two-element stride. */
        auto offset = builder.makeConstant(i ? (sharedBase ? 3u : 2u) : 1u);
        auto address = builder.add(Op::IAdd(ScalarType::eU32,
          constantFirst ? offset : dynamic, constantFirst ? dynamic : offset));
        auto value = builder.add(Op::BufferLoad(BasicType(ScalarType::eU32, 4u), descriptor, address, 16u));
        value = builder.add(Op::CompositeExtract(ScalarType::eU32, value, builder.makeConstant(0u)));
        builder.add(Op::ScratchStore(scratch, builder.makeConstant(2u * i), value));
      }

      auto result = builder.add(Op::ScratchLoad(ScalarType::eU32, scratch, builder.makeConstant(2u)));
      builder.add(Op::Drain(ScalarType::eU32, result));
      builder.add(Op::Return());

      ok(CleanupScratchPass::runResolveCbvToScratchCopyPass(builder, { }) == sharedBase);
      ok((builder.getOp(result).getOpCode() != OpCode::eScratchLoad) == sharedBase);
    }
  }
}


void testIrScratchCbvMask(uint32_t arraySize, uint32_t omittedIndex, bool boundChecking, uint32_t componentCount) {
  for (uint32_t input : { 0u, 1u, 30u, 31u, 32u, arraySize - 1u, arraySize, 0xffffffffu }) {
    Builder builder;
    auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
    auto valueType = BasicType(ScalarType::eU32, componentCount);
    auto scratch = builder.add(Op::DclScratch(Type(valueType).addArrayDimension(arraySize), entry));
    auto cbv = builder.add(Op::DclCbv(Type(ScalarType::eU32, 4u).addArrayDimension(128u), entry, 0u, 0u, 1u));
    builder.add(Op::Label());
    auto descriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbv, builder.makeConstant(0u)));
    for (uint32_t i = 0u; i < arraySize; i++) {
      if (i == omittedIndex)
        continue;
      auto index = builder.makeConstant(i);
      auto value = builder.add(Op::BufferLoad(BasicType(ScalarType::eU32, 4u), descriptor, index, 16u));
      for (uint32_t j = 0u; j < componentCount; j++) {
        auto scalar = builder.add(Op::CompositeExtract(ScalarType::eU32, value, builder.makeConstant(j)));
        auto address = componentCount == 1u ? index : builder.makeConstant(i, j);
        builder.add(Op::ScratchStore(scratch, address, scalar));
      }
    }
    auto index = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(input)));
    auto result = builder.add(Op::ScratchLoad(valueType, scratch, index));
    builder.add(Op::Drain(valueType, result));
    builder.add(Op::Return());

    CleanupScratchPass::Options options;
    options.enableBoundChecking = boundChecking;
    bool sparse = omittedIndex < arraySize;
    bool expectedPromotion = !sparse || arraySize <= 32u;
    bool promoted = CleanupScratchPass::runResolveCbvToScratchCopyPass(builder, options);
    ok(promoted == expectedPromotion);
    if (!promoted)
      continue;

    /* Give the generated helper concrete arguments and a known CBV value.
     * Keep its mask index observable separately: an out-of-range extraction
     * is not made safe by an AND with a false bounds-check result. */
    SsaDef returnDef, indexDef, bitOffsetDef;
    uint32_t paramIndex = 0u;
    for (const auto& op : builder) {
      if (op.getOpCode() == OpCode::eParamLoad) {
        if (paramIndex++)
          indexDef = op.getDef();
      } else if (op.getOpCode() == OpCode::eUBitExtract) {
        bitOffsetDef = SsaDef(op.getOperand(1u));
      } else if (op.getOpCode() == OpCode::eReturn && op.getOperandCount()) {
        returnDef = op.getDef();
      }
    }
    ok(bool(bitOffsetDef) == sparse);
    SsaDef bitOffsetSink;
    if (bitOffsetDef)
      bitOffsetSink = builder.addBefore(returnDef, Op::Drain(ScalarType::eU32, bitOffsetDef));
    builder.rewriteDef(indexDef, builder.makeConstant(input));
    /* Only the helper's load is needed for checking the returned value. */
    std::vector<SsaDef> bufferLoads;
    for (const auto& op : builder) {
      if (op.getOpCode() == OpCode::eBufferLoad)
        bufferLoads.push_back(op.getDef());
    }
    for (auto load : bufferLoads)
      builder.rewriteDef(load, builder.makeConstant(123u, 123u, 123u, 123u));
    while (ArithmeticPass::runPass(builder, { }) |
           ScalarizePass::runResolveRedundantCompositesPass(builder)) { }

    if (bitOffsetSink) {
      const auto& offset = builder.getOpForOperand(bitOffsetSink, 0u);
      ok(offset.isConstant());
      ok(uint32_t(offset.getOperand(0u)) < 32u);
    }
    if (boundChecking || sparse || input < arraySize) {
      const auto& value = builder.getOpForOperand(returnDef, 0u);
      ok(value.isConstant());
      if (value.isConstant()) {
        for (uint32_t j = 0u; j < componentCount; j++) {
          ok(uint32_t(value.getOperand(j)) ==
            ((input < arraySize && input != omittedIndex) ? 123u : 0u));
        }
      }
    }
  }
}


void testIrScratchCbvIncompleteStores() {
  for (uint32_t variant = 0u; variant < 3u; variant++) {
    Builder builder;
    auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
    auto scratch = builder.add(Op::DclScratch(Type(ScalarType::eU32, 4u).addArrayDimension(2u), entry));
    auto cbv = builder.add(Op::DclCbv(Type(ScalarType::eU32, 4u).addArrayDimension(16u), entry, 0u, 0u, 1u));
    builder.add(Op::Label());
    auto descriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbv, builder.makeConstant(0u)));
    for (uint32_t i = 0u; i < 2u; i++) {
      auto value = builder.add(Op::BufferLoad(BasicType(ScalarType::eU32, 4u), descriptor, builder.makeConstant(i), 16u));
      for (uint32_t j = 0u; j < 4u; j++) {
        /* Missing the first or a later component must not look like a hole
         * in the element mask. Out-of-bounds stores cannot extend it either. */
        if (!i && variant < 2u && j == variant)
          continue;
        auto index = builder.makeConstant(variant == 2u ? i + 1u : i, j);
        auto scalar = builder.add(Op::CompositeExtract(ScalarType::eU32, value, builder.makeConstant(j)));
        builder.add(Op::ScratchStore(scratch, index, scalar));
      }
    }
    auto result = builder.add(Op::ScratchLoad(BasicType(ScalarType::eU32, 4u), scratch, builder.makeConstant(0u)));
    builder.add(Op::Drain(BasicType(ScalarType::eU32, 4u), result));
    builder.add(Op::Return());
    ok(!CleanupScratchPass::runResolveCbvToScratchCopyPass(builder, { }));
    ok(builder.getOp(result).getOpCode() == OpCode::eScratchLoad);
  }
}


void testIrScratch() {
  RUN_TEST(testIrScratchCbvOffsets);
  RUN_TEST(testIrScratchCbvIncompleteStores);
  for (uint32_t size : { 31u, 32u, 33u, 64u }) {
    for (bool bounds : { false, true }) {
      for (uint32_t components : { 1u, 4u }) {
        RUN_TEST(testIrScratchCbvMask, size, 0xffffffffu, bounds, components);
        RUN_TEST(testIrScratchCbvMask, size, 0u, bounds, components);
        RUN_TEST(testIrScratchCbvMask, size, 1u, bounds, components);
        RUN_TEST(testIrScratchCbvMask, size, 31u, bounds, components);
      }
    }
  }
}

}
