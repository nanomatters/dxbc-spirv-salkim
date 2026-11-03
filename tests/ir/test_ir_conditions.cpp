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

static bool floatEqual(uint64_t a, uint64_t b, ScalarType type, bool flush) {
  uint32_t mantissa = type == ScalarType::eF16 ? 10u : type == ScalarType::eF32 ? 23u : 52u;
  uint64_t magnitude = (uint64_t(1u) << (bitWidth(type) - 1u)) - 1u;
  uint64_t fraction = (uint64_t(1u) << mantissa) - 1u;
  uint64_t exponent = magnitude & ~fraction;

  if (((a & exponent) == exponent && (a & fraction)) ||
      ((b & exponent) == exponent && (b & fraction)))
    return false;

  if (!(a & magnitude) || (flush && (a & magnitude) <= fraction))
    a = 0u;
  if (!(b & magnitude) || (flush && (b & magnitude) <= fraction))
    b = 0u;
  return a == b;
}

static uint64_t evaluateCondition(const Builder& builder, SsaDef def,
    uint64_t input, ScalarType type, bool flush) {
  const auto& op = builder.getOp(def);
  auto arg = [&](uint32_t i) {
    return evaluateCondition(builder, SsaDef(op.getOperand(i)), input, type, flush);
  };

  switch (op.getOpCode()) {
    case OpCode::eConstant: return uint64_t(op.getOperand(0u));
    case OpCode::eDrain: return input;
    case OpCode::eFEq: return floatEqual(arg(0u), arg(1u), type, flush);
    case OpCode::eFNe: return !floatEqual(arg(0u), arg(1u), type, flush);
    case OpCode::eBAnd: return arg(0u) && arg(1u);
    case OpCode::eBOr: return arg(0u) || arg(1u);
    case OpCode::eBNot: return !arg(0u);
    case OpCode::eSelect: return arg(arg(0u) ? 1u : 2u);
    default:
      ok(false);
      return 0u;
  }
}

static void testConditionFloatComparisons(ScalarType type) {
  uint32_t mantissa = type == ScalarType::eF16 ? 10u : type == ScalarType::eF32 ? 23u : 52u;
  uint64_t sign = uint64_t(1u) << (bitWidth(type) - 1u);
  uint64_t normal = uint64_t(1u) << mantissa;
  uint64_t infinity = (sign - 1u) & ~(normal - 1u);
  const uint64_t values[] = {
    0u, sign, 1u, sign | 1u, normal - 1u, normal, normal + 1u, infinity, infinity | 1u,
  };

  for (bool flush : { false, true }) {
    for (auto a : values) {
      for (auto b : values) {
        Builder builder;
        auto entry = test_api::setupTestFunction(builder, ShaderStage::eCompute);
        builder.add(Op::SetFpMode(entry, type, {}, RoundMode::eNearestEven,
          flush ? DenormMode::eFlush : DenormMode::ePreserve));
        auto constant = [&](uint64_t bits) {
          return builder.add(Op(OpCode::eConstant, type).addOperand(Operand(bits)));
        };
        auto input = builder.add(Op::Drain(type, constant(0u)));
        auto left = builder.add(Op::FEq(ScalarType::eBool, input, constant(a)));
        auto right = builder.add(Op::FEq(ScalarType::eBool, input, constant(b)));
        auto result = builder.add(Op::BAnd(ScalarType::eBool, left, right));
        auto sink = builder.add(Op::Drain(ScalarType::eBool, result));
        builder.add(Op::Return());

        legalizeIr(builder, {});
        result = builder.getOpForOperand(sink, 0u).getDef();
        for (auto value : values) {
          bool expected = floatEqual(value, a, type, flush) && floatEqual(value, b, type, flush);
          ok(bool(evaluateCondition(builder, result, value, type, flush)) == expected);
        }

        if (a == normal && b == normal + 1u) {
          const auto& folded = builder.getOp(result);
          ok(folded.isConstant() && !bool(folded.getOperand(0u)));
        }
      }
    }
  }
}

static void testConditionRecursion(uint32_t depth, bool booleanChain) {
  Builder builder;
  test_api::setupTestFunction(builder, ShaderStage::eCompute);
  builder.add(Op::Label());
  auto condition = builder.add(Op::Drain(ScalarType::eBool, builder.makeConstant(true)));
  SsaDef value;

  if (booleanChain) {
    value = condition;
    for (uint32_t i = 0u; i < depth; i++)
      value = builder.add(Op::BNot(ScalarType::eBool, value));
    value = builder.add(Op::Select(ScalarType::eBool, condition, value, builder.makeConstant(false)));
    builder.add(Op::Drain(ScalarType::eBool, value));
  } else {
    auto a = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(2u)));
    auto b = builder.add(Op::Drain(ScalarType::eU32, builder.makeConstant(3u)));
    value = builder.add(Op::Select(ScalarType::eU32, condition, a, b));
    for (uint32_t i = 0u; i < depth; i++)
      value = builder.add(Op::IAdd(ScalarType::eU32, value, builder.makeConstant(i)));
    value = builder.add(Op::Select(ScalarType::eU32, condition, value, builder.makeConstant(0u)));
    builder.add(Op::Drain(ScalarType::eU32, value));
  }
  builder.add(Op::Return());

  ConditionPass::runPass(builder);

  /* Short expressions still fold, but deep ones keep the unresolved subtree. */
  if (booleanChain)
    ok((countOp(builder, OpCode::eBNot) != 0u) == (depth == 512u));
  else
    ok(countOp(builder, OpCode::eSelect) == (depth == 512u ? 2u : 1u));
}

void testIrConditions() {
  for (auto depth : { 8u, 512u }) {
    testConditionRecursion(depth, false);
    testConditionRecursion(depth, true);
  }

  for (auto type : { ScalarType::eF16, ScalarType::eF32, ScalarType::eF64 })
    testConditionFloatComparisons(type);

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
