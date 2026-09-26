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


void testIrArithmeticNegatedCompare() {
  const OpCode comparisons[] = {
    OpCode::eIEq, OpCode::eINe,
    OpCode::eSLt, OpCode::eSLe, OpCode::eSGt, OpCode::eSGe,
    OpCode::eULt, OpCode::eULe, OpCode::eUGt, OpCode::eUGe,
  };

  for (auto code : comparisons) {
    for (uint32_t input : { 0u, 1u, 0x7fffffffu, 0x80000000u, 0xffffffffu }) {
      for (uint32_t constant : { 0u, 1u, 5u, 0x7fffffffu, 0x80000000u, 0xffffffffu }) {
        Builder builder;
        test_api::setupTestFunction(builder, ShaderStage::eCompute);
        builder.add(Op::Label());
        auto value = builder.add(Op::Drain(ScalarType::eI32, builder.makeConstant(int32_t(input))));
        auto neg = builder.add(Op::INeg(ScalarType::eI32, value));
        auto cmp = builder.add(Op(code, ScalarType::eBool)
          .addOperands(neg, builder.makeConstant(int32_t(constant))));
        auto sink = builder.add(Op::Drain(ScalarType::eBool, cmp));
        /* Exercise the rewrite's multiple-use condition. */
        builder.add(Op::Drain(ScalarType::eI32, value));
        builder.add(Op::Return());

        ArithmeticPass::runPass(builder, { });
        builder.rewriteDef(value, builder.makeConstant(int32_t(input)));
        while (ArithmeticPass::runPass(builder, { })) { }

        uint32_t lhs = 0u - input;
        bool expected = false;
        switch (code) {
          case OpCode::eIEq: expected = lhs == constant; break;
          case OpCode::eINe: expected = lhs != constant; break;
          case OpCode::eSLt: expected = int32_t(lhs) <  int32_t(constant); break;
          case OpCode::eSLe: expected = int32_t(lhs) <= int32_t(constant); break;
          case OpCode::eSGt: expected = int32_t(lhs) >  int32_t(constant); break;
          case OpCode::eSGe: expected = int32_t(lhs) >= int32_t(constant); break;
          case OpCode::eULt: expected = lhs <  constant; break;
          case OpCode::eULe: expected = lhs <= constant; break;
          case OpCode::eUGt: expected = lhs >  constant; break;
          case OpCode::eUGe: expected = lhs >= constant; break;
          default: break;
        }
        const auto& result = builder.getOpForOperand(builder.getOp(sink), 0u);
        ok(result.isConstant());
        ok(bool(result.getOperand(0u)) == expected);
      }
    }
  }
}


void testIrArithmeticIntegerWidths() {
  for (auto type : { ScalarType::eU8, ScalarType::eU16, ScalarType::eU32, ScalarType::eU64 }) {
    uint32_t bits = bitWidth(type);
    uint64_t mask = ~uint64_t(0u) >> (64u - bits);
    uint64_t sign = uint64_t(1u) << (bits - 1u);

    auto check = [=] (OpCode code, std::initializer_list<uint64_t> args, uint64_t expected) {
      Builder builder;
      test_api::setupTestFunction(builder, ShaderStage::eCompute);
      builder.add(Op::Label());
      Op op(code, type);
      for (auto value : args)
        op.addOperand(builder.add(Op(OpCode::eConstant, type).addOperand(value & mask)));
      auto sink = builder.add(Op::Drain(type, builder.add(std::move(op))));
      builder.add(Op::Return());
      while (ArithmeticPass::runPass(builder, { })) { }
      const auto& result = builder.getOpForOperand(builder.getOp(sink), 0u);
      ok(result.isConstant());
      ok((uint64_t(result.getOperand(0u)) & mask) == (expected & mask));
    };

    for (uint64_t value : { uint64_t(0u), uint64_t(1u), sign - 1u, sign, mask }) {
      check(OpCode::eIAdd, { value, sign }, value + sign);
      check(OpCode::eISub, { value, sign }, value - sign);
      check(OpCode::eIMul, { value, mask }, value * mask);
      check(OpCode::eINeg, { value }, 0u - value);
      check(OpCode::eIAbs, { value }, value & sign ? 0u - value : value);

      for (uint32_t shift = 0u; shift < bits; shift++) {
        check(OpCode::eIShl, { value, shift }, value << shift);
        check(OpCode::eUShr, { value, shift }, value >> shift);
        auto shifted = value >> shift;
        if ((value & sign) && shift)
          shifted |= mask ^ (mask >> shift);
        check(OpCode::eSShr, { value, shift }, shifted);
      }

      for (uint32_t offset = 0u; offset <= bits; offset++) {
        for (uint32_t count : { 0u, bits - offset }) {
          uint64_t extracted = 0u, inserted = value;
          /* A bit-by-bit oracle avoids duplicating the folder's mask logic. */
          for (uint32_t bit = 0u; bit < count; bit++) {
            extracted |= ((value >> (offset + bit)) & 1u) << bit;
            inserted = (inserted & ~(uint64_t(1u) << (offset + bit))) |
              (((~value >> bit) & 1u) << (offset + bit));
          }
          check(OpCode::eUBitExtract, { value, offset, count }, extracted);
          auto signedValue = extracted;
          if (count && (extracted & (uint64_t(1u) << (count - 1u))))
            signedValue |= ~(mask >> (bits - count));
          check(OpCode::eSBitExtract, { value, offset, count }, signedValue);
          check(OpCode::eIBitInsert, { value, ~value, offset, count }, inserted);
        }
      }
    }
  }
}


void testIrArithmeticBitExtractBounds() {
  for (auto type : { ScalarType::eU32, ScalarType::eU64 }) {
    auto bits = bitWidth(type);
    auto mask = ~uint64_t(0u) >> (64u - bits);
    for (uint32_t count : { 0u, 1u, 31u, 32u, bits - 1u, bits }) {
      auto extracted = count ? mask >> (bits - count) : 0u;
      for (auto limit : { uint64_t(0u), uint64_t(1u), mask >> 1u, mask }) {
        for (bool compare : { false, true }) {
          Builder builder;
          test_api::setupTestFunction(builder, ShaderStage::eCompute);
          builder.add(Op::Label());
          auto input = builder.add(Op(OpCode::eConstant, type).addOperand(mask));
          auto value = builder.add(Op::Drain(type, input));
          auto field = builder.add(Op::UBitExtract(type, value,
            builder.makeConstant(0u), builder.makeConstant(count)));
          auto bound = builder.add(Op(OpCode::eConstant, type).addOperand(limit));
          auto resultType = compare ? ScalarType::eBool : type;
          auto result = builder.add(Op(compare ? OpCode::eULt : OpCode::eUMin, resultType)
            .addOperands(field, bound));
          auto sink = builder.add(Op::Drain(resultType, result));
          builder.add(Op::Return());
          ArithmeticPass::runPass(builder, { });
          builder.rewriteDef(value, input);
          while (ArithmeticPass::runPass(builder, { })) { }
          const auto& folded = builder.getOpForOperand(builder.getOp(sink), 0u);
          ok(folded.isConstant());
          ok(uint64_t(folded.getOperand(0u)) ==
            (compare ? uint64_t(extracted < limit) : std::min(extracted, limit)));
        }
      }
    }
  }
}


void testIrArithmeticInvalidBitRanges() {
  for (auto type : { ScalarType::eU16, ScalarType::eU32, ScalarType::eU64 }) {
    auto bits = bitWidth(type);
    for (auto code : { OpCode::eIShl, OpCode::eSShr, OpCode::eUShr,
                      OpCode::eUBitExtract, OpCode::eSBitExtract, OpCode::eIBitInsert }) {
      Builder builder;
      test_api::setupTestFunction(builder, ShaderStage::eCompute);
      builder.add(Op::Label());
      Op op(code, type);
      op.addOperand(builder.add(Op(OpCode::eConstant, type).addOperand(1u)));
      if (code == OpCode::eIBitInsert)
        op.addOperand(builder.add(Op(OpCode::eConstant, type).addOperand(2u)));
      op.addOperand(builder.makeConstant(bits));
      if (code == OpCode::eUBitExtract || code == OpCode::eSBitExtract || code == OpCode::eIBitInsert)
        op.addOperand(builder.makeConstant(1u));
      auto sink = builder.add(Op::Drain(type, builder.add(std::move(op))));
      builder.add(Op::Return());
      while (ArithmeticPass::runPass(builder, { })) { }
      /* Undefined shader inputs must not trigger undefined host arithmetic. */
      ok(builder.getOpForOperand(builder.getOp(sink), 0u).getOpCode() == code);
    }
  }
}


void testIrArithmetic() {
  /* Matching selects, non-select on either side, mismatched conditions,
   * shared selects, and shared selects with a constant branch. */
  for (uint32_t variant = 0u; variant < 6u; variant++)
    RUN_TEST(testIrArithmeticSelectMerge, variant);
  RUN_TEST(testIrArithmeticNegatedCompare);
  RUN_TEST(testIrArithmeticIntegerWidths);
  RUN_TEST(testIrArithmeticBitExtractBounds);
  RUN_TEST(testIrArithmeticInvalidBitRanges);
}

}
