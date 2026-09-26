#include "../../sm3/sm3_converter.h"
#include "../../ir/passes/ir_pass_arithmetic.h"
#include "../../ir/passes/ir_pass_lower_consume.h"
#include "../../ir/passes/ir_pass_scalarize.h"

#include "../test_common.h"

namespace dxbc_spv::tests::sm3 {

void testSm3Lit() {
  for (float x : { -1.0f, -0.0f, 0.0f, 0.5f, 1.0f }) {
    for (float y : { -1.0f, -0.0f, 0.0f, 0.5f, 1.0f }) {
      for (float power : { -2.0f, 0.0f, 2.0f }) {
        /* vs_2_0: def c0, x, y, 0, power; lit oPos, c0; end. */
        const uint32_t code[] = {
          0xfffe0200u, 0x05000051u, 0xa00f0000u,
          uint32_t(ir::Operand(x)), uint32_t(ir::Operand(y)), 0u, uint32_t(ir::Operand(power)),
          0x02000010u, 0xc00f0000u, 0xa0e40000u, 0x0000ffffu,
        };
        ir::Builder builder;
        dxbc_spv::sm3::Converter converter(util::ByteReader(code, sizeof(code)), { });
        ok(converter.convertShader(builder));

        ir::SsaDef specular;
        for (const auto& op : builder) {
          if (op.getOpCode() == ir::OpCode::eSelect &&
              builder.getOpForOperand(op, 1u).getOpCode() == ir::OpCode::eFPow)
            specular = op.getDef();
        }
        ok(bool(specular));
        if (!specular)
          continue;

        /* Check the translated predicate without relying on the particular
         * approximation used to implement pow on positive inputs. */
        auto condition = ir::SsaDef(builder.getOp(specular).getOperand(0u));
        auto conditionSink = builder.add(ir::Op::Drain(ir::ScalarType::eBool, condition));
        auto resultSink = builder.add(ir::Op::Drain(ir::ScalarType::eF32, specular));
        ir::LowerConsumePass::runLowerConsumePass(builder);
        ir::ScalarizePass::runResolveRedundantCompositesPass(builder);
        while (ir::ArithmeticPass::runPass(builder, { })) { }

        bool positive = x > 0.0f && y > 0.0f;
        const auto& predicate = builder.getOpForOperand(conditionSink, 0u);
        ok(predicate.isConstant());
        if (predicate.isConstant())
          ok(bool(predicate.getOperand(0u)) == positive);

        if (!positive) {
          const auto& result = builder.getOpForOperand(resultSink, 0u);
          ok(result.isConstant());
          if (result.isConstant())
            ok(float(result.getOperand(0u)) == 0.0f);
        }
      }
    }
  }
}

}
