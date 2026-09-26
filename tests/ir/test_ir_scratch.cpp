#include "../../ir/passes/ir_pass_scratch.h"

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


void testIrScratch() {
  RUN_TEST(testIrScratchCbvOffsets);
}

}
