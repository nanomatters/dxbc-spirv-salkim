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

void testIrUtils() {
  RUN_TEST(testIrUtilsFloatConstants);
}

}
