#include "../../ir/ir_legalize.h"
#include "../../ir/passes/ir_pass_conditions.h"

#include "../api/test_api_common.h"
#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

static uint32_t countOp(const Builder& builder, OpCode code) {
  uint32_t count = 0u;
  for (auto op = builder.begin(); op != builder.end(); ++op)
    count += op->getOpCode() == code;
  return count;
}

static void testConditionEffects(OpCode code, bool fullPipeline) {
  Builder builder;
  SsaDef function;

  if (code == OpCode::eFunctionCall) {
    auto param = builder.add(Op::DclParam(ScalarType::eU32));
    function = builder.add(Op::Function(ScalarType::eU32).addParam(param));
    if (!fullPipeline)
      builder.add(Op::Label());
    auto value = builder.add(Op::ParamLoad(ScalarType::eU32, function, param));
    builder.add(Op::Drain(ScalarType::eU32, value));
    builder.add(Op::Return(ScalarType::eU32, value));
    builder.add(Op::FunctionEnd());
  }

  auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::SetCsWorkgroupSize(entry, 1u, 1u, 1u));
  if (!fullPipeline)
    builder.add(Op::Label());

  auto bufferType = Type(ScalarType::eU32).addArrayDimension(0u);
  auto uav = builder.add(Op::DclUav(bufferType, entry, 0u, 0u, 2u, ResourceKind::eBufferRaw, {}));
  auto descriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, uav, builder.makeConstant(0u)));
  auto input = builder.add(Op::BufferLoad(ScalarType::eU32, descriptor, builder.makeConstant(1u), 4u));
  auto condition = builder.add(Op::INe(ScalarType::eBool, input, builder.makeConstant(0u)));
  auto increment = builder.add(Op::Select(ScalarType::eU32, condition,
    builder.makeConstant(1u), builder.makeConstant(0u)));

  SsaDef effect;
  switch (code) {
    case OpCode::eBufferAtomic:
      effect = builder.add(Op::BufferAtomic(AtomicOp::eAdd, ScalarType::eU32,
        descriptor, builder.makeConstant(0u), increment));
      break;

    case OpCode::eLdsAtomic: {
      auto lds = builder.add(Op::DclLds(Type(ScalarType::eU32).addArrayDimension(1u), entry));
      effect = builder.add(Op::LdsAtomic(AtomicOp::eAdd, ScalarType::eU32,
        lds, builder.makeConstant(0u), increment));
    } break;

    case OpCode::eImageAtomic: {
      auto image = builder.add(Op::DclUav(ScalarType::eU32, entry, 0u, 2u, 1u,
        ResourceKind::eImage1D, UavFlag::eFixedFormat));
      auto imageDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUav, image, builder.makeConstant(0u)));
      effect = builder.add(Op::ImageAtomic(AtomicOp::eAdd, ScalarType::eU32,
        imageDescriptor, {}, builder.makeConstant(0u), increment));
    } break;

    case OpCode::eCounterAtomic: {
      auto counter = builder.add(Op::DclUavCounter(entry, uav));
      auto counterDescriptor = builder.add(Op::DescriptorLoad(ScalarType::eUavCounter, counter, increment));
      effect = builder.add(Op::CounterAtomic(AtomicOp::eInc, ScalarType::eU32, counterDescriptor));
    } break;

    case OpCode::eMemoryAtomic: {
      auto address = builder.add(Op::Drain(ScalarType::eU64, builder.makeConstant(uint64_t(4096u))));
      auto pointer = builder.add(Op::Pointer(bufferType, address, {}));
      effect = builder.add(Op::MemoryAtomic(AtomicOp::eAdd, ScalarType::eU32,
        pointer, builder.makeConstant(0u), increment));
    } break;

    case OpCode::eFunctionCall:
      effect = builder.add(Op::FunctionCall(ScalarType::eU32, function).addParam(increment));
      break;

    case OpCode::eDrain:
      effect = builder.add(Op::Drain(ScalarType::eU32, increment));
      break;

    default:
      effect = builder.add(Op::IAdd(ScalarType::eU32, increment, input));
      break;
  }

  auto result = builder.add(Op::Select(ScalarType::eU32, condition, effect, builder.makeConstant(0u)));
  builder.add(Op::BufferStore(descriptor, builder.makeConstant(2u), result, 4u));
  builder.add(Op::Return());

  if (fullPipeline)
    legalizeIr(builder, {});
  else
    ConditionPass::runPass(builder);

  ok(countOp(builder, code) == 1u);
  if (code != OpCode::eIAdd) {
    ok(builder.getOp(effect).getOpCode() == code);
    ok(builder.getUseCount(effect) == 1u);
  } else {
    auto [first, last] = builder.getCode();
    while (first != last && first->getOpCode() != OpCode::eIAdd)
      ++first;
    ok(first != last);
    if (first != last) {
      bool hasOne = false;
      for (uint32_t i = 0u; i < 2u; i++) {
        const auto& arg = builder.getOpForOperand(*first, i);
        hasOne |= arg.isConstant() && uint32_t(arg.getOperand(0u)) == 1u;
      }
      ok(hasOne);
    }
  }
}

void testIrConditions() {
  const OpCode effects[] = {
    OpCode::eBufferAtomic, OpCode::eLdsAtomic, OpCode::eImageAtomic,
    OpCode::eCounterAtomic, OpCode::eMemoryAtomic, OpCode::eFunctionCall,
    OpCode::eDrain, OpCode::eIAdd,
  };
  for (auto code : effects) {
    testConditionEffects(code, false);
    testConditionEffects(code, true);
  }
}

}
