#include "../../ir/passes/ir_pass_scalarize.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrScalarize() {
  /* Exercise vector, array-of-vector, nested array and mixed struct indices. */
  const Type types[] = {
    Type(ScalarType::eU32, 4u),
    Type(ScalarType::eU32, 4u).addArrayDimension(3u),
    Type(ScalarType::eU32, 4u).addArrayDimension(2u).addArrayDimension(3u),
    Type().addStructMember(ScalarType::eU32, 2u).addStructMember(ScalarType::eU32, 4u),
  };

  for (auto type : types) {
    for (uint32_t component = 0u; component < 4u; component++) {
      Builder builder;
      test_api::setupTestFunction(builder, ShaderStage::eCompute);
      builder.add(Op::Label());
      Op constant(OpCode::eConstant, type);
      for (uint32_t i = 0u; i < type.computeFlattenedScalarCount(); i++)
        constant.addOperand(100u + i);
      auto value = builder.add(std::move(constant));
      auto subType = type;
      uint32_t offset = 0u;
      Op address(OpCode::eConstant, ScalarType::eU32);
      while (!subType.isVectorType()) {
        address.addOperand(1u);
        offset += subType.computeScalarIndex(1u);
        subType = subType.getSubType(1u);
      }
      address.addOperand(component);
      address.setType(BasicType(ScalarType::eU32, address.getOperandCount()));
      auto result = builder.add(Op::CompositeExtract(ScalarType::eU32, value, builder.add(std::move(address))));
      auto sink = builder.add(Op::Drain(ScalarType::eU32, result));
      builder.add(Op::Return());
      ScalarizePass::runPass(builder, { });
      const auto& folded = builder.getOpForOperand(builder.getOp(sink), 0u);
      ok(folded.isConstant());
      ok(uint32_t(folded.getOperand(0u)) == 100u + offset + component);
    }
  }
}

}
