#include <cmath>
#include <limits>

#include "../../ir/ir_utils.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrUtilsFloatConstants() {
  const ScalarType types[] = {
    ScalarType::eF16, ScalarType::eF32, ScalarType::eMinF16, ScalarType::eF64,
  };
  const double values[] = {
    0.0, -0.0, 1.5, -1.5, 65504.0, -65504.0, 1.0e30, -1.0e30,
    std::numeric_limits<double>::infinity(),
    -std::numeric_limits<double>::infinity(),
    std::numeric_limits<double>::quiet_NaN(),
  };
  auto encode = [] (ScalarType type, double value) {
    if (type == ScalarType::eF16)
      return Operand(float16_t(value));
    if (type == ScalarType::eF64)
      return Operand(value);
    return Operand(float(value));
  };
  auto decode = [] (ScalarType type, Operand value) -> double {
    if (type == ScalarType::eF16)
      return double(float16_t(value));
    if (type == ScalarType::eF64)
      return double(value);
    return float(value);
  };

  for (auto srcType : types) {
    for (auto dstType : types) {
      for (auto value : values) {
        auto src = Op(OpCode::eConstant, BasicType(srcType, 2u))
          .addOperands(encode(srcType, value), encode(srcType, -value));
        for (bool consume : { false, true }) {
          /* Consume includes a bitcast between normalized types, which
           * requires matching widths. Numeric conversion has no such limit. */
          if (consume && ((srcType == ScalarType::eF64) != (dstType == ScalarType::eF64)))
            continue;
          auto result = consume ? consumeConstant(src, BasicType(dstType, 2u))
                                : convertConstant(src, BasicType(dstType, 2u));
          ok(result.isConstant());
          ok(result.getType() == BasicType(dstType, 2u));
          ok(result.getOperandCount() == 2u);
          for (uint32_t i = 0u; i < 2u; i++) {
            auto expected = decode(dstType, encode(dstType, decode(srcType, src.getOperand(i))));
            auto actual = decode(dstType, result.getOperand(i));
            if (std::isnan(expected)) {
              ok(std::isnan(actual));
            } else {
              ok(actual == expected);
              ok(std::signbit(actual) == std::signbit(expected));
            }
          }
        }
      }
    }
  }

  /* Keep ordinary cross-category conversion semantics unchanged. */
  ok(int32_t(convertConstant(Op::Constant(-3.75f), ScalarType::eI32).getOperand(0u)) == -3);
  ok(uint32_t(convertConstant(Op::Constant(3.75f), ScalarType::eU32).getOperand(0u)) == 3u);
  ok(float(convertConstant(Op::Constant(-123), ScalarType::eF32).getOperand(0u)) == -123.0f);
}

void testIrUtilsConstantCasts() {
  const std::pair<ScalarType, ScalarType> types[] = {
    { ScalarType::eU8, ScalarType::eI8 },
    { ScalarType::eU16, ScalarType::eI16 },
    { ScalarType::eU32, ScalarType::eI32 },
    { ScalarType::eU64, ScalarType::eI64 },
  };
  for (const auto& pair : types) {
    uint64_t sign = uint64_t(1u) << (bitWidth(pair.first) - 1u);
    uint64_t mask = sign | (sign - 1u);
    const uint64_t values[] = { 0u, 1u, sign - 1u, sign, mask };
    for (uint32_t count = 1u; count <= 4u; count++) {
      for (bool sourceSigned : { false, true }) {
        for (bool destinationSigned : { false, true }) {
          for (uint32_t start = 0u; start < 5u; start++) {
            auto sourceType = sourceSigned ? pair.second : pair.first;
            auto destinationType = destinationSigned ? pair.second : pair.first;
            Op source(OpCode::eConstant, BasicType(sourceType, count));
            for (uint32_t i = 0u; i < count; i++) {
              uint64_t value = values[(start + i) % 5u];
              if (sourceSigned && (value & sign))
                value |= ~mask;
              source.addOperand(value);
            }
            auto result = castConstant(source, BasicType(destinationType, count));
            ok(result.getType() == BasicType(destinationType, count));
            ok(result.getOperandCount() == count);
            for (uint32_t i = 0u; i < count; i++) {
              uint64_t expected = values[(start + i) % 5u];
              if (destinationSigned && (expected & sign))
                expected |= ~mask;
              ok(uint64_t(result.getOperand(i)) == expected);
            }
          }
        }
      }
    }
  }
}

void testIrUtils() {
  RUN_TEST(testIrUtilsFloatConstants);
  RUN_TEST(testIrUtilsConstantCasts);
}

}
