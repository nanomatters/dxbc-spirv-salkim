#include <array>

#include "../../ir/passes/ir_pass_arithmetic.h"
#include "../../ir/passes/ir_pass_buffer_indexing.h"
#include "../../ir/passes/ir_pass_scalarize.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

static void testBufferIndexingCast(BasicType loadType,
    std::initializer_list<BasicType> casts, int32_t component,
    BasicType resultCast, bool optimize) {
  for (uint32_t input : { 0u, 1u, 2u }) {
    Builder builder;
    auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
    auto cbv = builder.add(Op::DclCbv(Type(loadType).addArrayDimension(16u), entry, 0u, 0u, 1u));
    builder.add(Op::Label());
    auto index = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(input)));
    auto descriptor = builder.add(Op::DescriptorLoad(ScalarType::eCbv, cbv, builder.makeConstant(0u)));
    std::array<SsaDef, 2u> selects, values;
    Type resultType;
    SsaDef result;

    for (uint32_t i = 0u; i < 2u; i++) {
      auto value = builder.add(Op::BufferLoad(loadType, descriptor, builder.makeConstant(i + 2u), loadType.byteSize()));
      resultType = loadType;
      for (auto type : casts) {
        value = builder.add(Op::Cast(type, value));
        resultType = type;
      }
      if (component >= 0) {
        resultType = resultType.getSubType(uint32_t(component));
        value = builder.add(Op::CompositeExtract(resultType, value, builder.makeConstant(uint32_t(component))));
      }
      if (!resultCast.isVoidType()) {
        resultType = resultCast;
        value = builder.add(Op::Cast(resultType, value));
      }
      if (!result)
        result = builder.makeConstantZero(resultType);
      auto condition = builder.add(Op::IEq(ScalarType::eBool, index, builder.makeConstant(i)));
      values[i] = value;
      selects[i] = result = builder.add(Op::Select(resultType, condition, value, result));
    }
    auto sink = builder.add(Op::Drain(resultType, result));
    builder.add(Op::Return());

    BufferIndexingPass::Options options;
    options.allowDynamicIndexing = true;
    bool changed = BufferIndexingPass::runPass(builder, options);
    ok(changed == optimize);
    for (uint32_t i = 0u; i < selects.size(); i++)
      ok((builder.getOpForOperand(selects[i], 1u).getDef() != values[i]) == optimize);

    /* Every rewritten extraction must still select an existing component. */
    bool valid = true;
    for (const auto& op : builder) {
      if (op.getOpCode() == OpCode::eCompositeExtract) {
        auto sourceType = builder.getOpForOperand(op, 0u).getType();
        auto componentIndex = uint32_t(builder.getOpForOperand(op, 1u).getOperand(0u));
        valid &= !op.getType().isVoidType() && sourceType.getSubType(componentIndex) == op.getType();
      } else if (op.getOpCode() == OpCode::eCast) {
        valid &= op.getType().byteSize() == builder.getOpForOperand(op, 0u).getType().byteSize();
      }
    }
    ok(valid);
    if (!valid)
      continue;

    /* Evaluate the rewritten expression with known buffer contents. This also
     * catches valid-looking casts that select the wrong half of a loaded word. */
    builder.rewriteDef(index, builder.makeConstant(input));
    ArithmeticPass::runPass(builder, { });
    std::vector<SsaDef> loads;
    for (const auto& op : builder) {
      if (op.getOpCode() == OpCode::eBufferLoad)
        loads.push_back(op.getDef());
    }
    for (auto load : loads) {
      const auto& address = builder.getOpForOperand(load, 1u);
      ok(address.isConstant());
      auto offset = uint32_t(address.getOperand(0u));
      Op constant(OpCode::eConstant, loadType);
      for (uint32_t i = 0u; i < loadType.getVectorSize(); i++)
        constant.addOperand(0x11223344u + offset * 0x01010101u + i * 0x10101010u);
      builder.rewriteDef(load, builder.add(std::move(constant)));
    }
    while (ArithmeticPass::runPass(builder, { }) |
           ScalarizePass::runResolveRedundantCompositesPass(builder)) { }
    const auto& folded = builder.getOpForOperand(sink, 0u);
    ok(folded.isConstant());
    if (!folded.isConstant())
      continue;

    auto castType = casts.size() ? *(casts.end() - 1u) : loadType;
    uint32_t byteOffset = component < 0 ? 0u : uint32_t(component) * castType.byteAlignment();
    auto scalarBytes = resultType.getBaseType(0u).byteAlignment();
    for (uint32_t i = 0u; i < resultType.getBaseType(0u).getVectorSize(); i++) {
      uint64_t expected = 0u;
      if (input < 2u) {
        for (uint32_t j = 0u; j < scalarBytes; j++) {
          uint32_t byte = byteOffset + i * scalarBytes + j;
          uint32_t word = 0x11223344u + (input + 2u) * 0x01010101u + (byte / 4u) * 0x10101010u;
          expected |= uint64_t((word >> ((byte % 4u) * 8u)) & 0xffu) << (j * 8u);
        }
      }
      ok(uint64_t(folded.getOperand(i)) == expected);
    }
  }
}

void testIrBufferIndexing() {
  /* Reinterpreting the load changes the meaning and valid range of a later
   * component index. Keep these paths intact instead of extracting first. */
  testBufferIndexingCast(ScalarType::eU32, { BasicType(ScalarType::eU16, 2u) },
    1, { }, false);
  testBufferIndexingCast(BasicType(ScalarType::eU32, 2u), { BasicType(ScalarType::eU16, 4u) },
    3, { }, false);

  /* Ordinary extraction and component-preserving casts still optimize. */
  testBufferIndexingCast(BasicType(ScalarType::eU32, 4u), { }, 2, { }, true);
  testBufferIndexingCast(BasicType(ScalarType::eU32, 4u), { BasicType(ScalarType::eF32, 4u) },
    2, { }, true);
  testBufferIndexingCast(BasicType(ScalarType::eU32, 4u),
    { BasicType(ScalarType::eF32, 4u), BasicType(ScalarType::eI32, 4u) }, 1, { }, true);

  /* Shape-changing casts are safe when they do not precede extraction. */
  testBufferIndexingCast(BasicType(ScalarType::eU32, 2u), { BasicType(ScalarType::eU16, 4u) },
    -1, { }, true);
  testBufferIndexingCast(BasicType(ScalarType::eU32, 4u), { }, 1, BasicType(ScalarType::eU16, 2u), true);
}

}
