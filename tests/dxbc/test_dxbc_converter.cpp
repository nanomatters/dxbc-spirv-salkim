#include "../../dxbc/dxbc_converter.h"
#include "../../ir/passes/ir_pass_arithmetic.h"

#include "../test_common.h"

namespace dxbc_spv::tests::dxbc {

using namespace dxbc_spv::dxbc;

void testDxbcBitInsertRange() {
  for (uint32_t width : { 0u, 1u, 16u, 31u, 32u, 63u, 0xffffffffu }) {
    for (uint32_t offset : { 0u, 1u, 16u, 31u, 32u, 63u, 0xffffffffu }) {
      /* cs_5_0: dcl_thread_group 1, 1, 1;
       * bfi r0.x, width, offset, 0x12345678, 0xabcdef01; ret.
       * The converter does not require signatures or a container checksum. */
      const uint32_t binary[] = {
        0x43425844u, 0u, 0u, 0u, 0u, 1u, 116u, 1u, 36u,
        0x58454853u, 72u, 0x00050050u, 18u,
        (4u << 24u) | uint32_t(OpCode::eDclThreadGroup), 1u, 1u, 1u,
        (11u << 24u) | uint32_t(OpCode::eBfi), 0x00100012u, 0u,
        0x00004001u, width, 0x00004001u, offset,
        0x00004001u, 0x12345678u, 0x00004001u, 0xabcdef01u,
        (1u << 24u) | uint32_t(OpCode::eRet),
      };
      ir::Builder builder;
      Converter converter(Container(binary, sizeof(binary)), { });
      ok(converter.convertShader(builder));

      ir::SsaDef count;
      for (const auto& op : builder) {
        if (op.getOpCode() == ir::OpCode::eIBitInsert)
          count = ir::SsaDef(op.getOperand(3u));
      }
      ok(bool(count));
      if (!count)
        continue;

      /* Keep the translated count alive while folding the frontend's mask
       * and clamp. The SPIR-V instruction must never cross the word boundary. */
      auto sink = builder.add(ir::Op::Drain(builder.getOp(count).getType(), count));
      while (ir::ArithmeticPass::runPass(builder, { })) { }
      const auto& result = builder.getOpForOperand(builder.getOp(sink), 0u);
      ok(result.isConstant());
      ok(uint32_t(result.getOperand(0u)) == std::min(width & 31u, 32u - (offset & 31u)));
    }
  }
}

}
