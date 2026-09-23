#include "../../spirv/spirv_builder.h"

#include "../api/test_api_resources.h"
#include "../test_common.h"

namespace dxbc_spv::tests::spirv {

using namespace dxbc_spv::ir;
using namespace dxbc_spv::spirv;

using ResourceTest = Builder (*)();

Builder makeDescriptorTest(ResourceTest test, uint32_t descriptorCount, bool nonUniform) {
  auto builder = test();

  for (auto op : builder) {
    switch (op.getOpCode()) {
      case OpCode::eDclCbv:
      case OpCode::eDclSrv:
      case OpCode::eDclUav:
        op.setOperand(op.getFirstLiteralOperandIndex() + 2u, descriptorCount);
        builder.rewriteOp(op.getDef(), op);
        break;

      case OpCode::eDescriptorLoad:
        op.setFlags(nonUniform ? OpFlags(OpFlag::eNonUniform) : OpFlags());
        builder.rewriteOp(op.getDef(), op);
        break;

      default:
        break;
    }
  }

  return builder;
}


std::vector<uint32_t> buildSpirv(const Builder& builder, const SpirvBuilder::Options& options) {
  BasicResourceMapping mapping;
  SpirvBuilder spirv(builder, mapping, options);
  spirv.buildSpirvBinary();
  return spirv.getSpirvBinary();
}


void testSpirvNonUniform() {
  const ResourceTest tests[] = {
    test_api::test_resources_cbv_indexed_nonuniform,
    test_api::test_resources_srv_indexed_buffer_raw_load,
    test_api::test_resources_srv_indexed_buffer_typed_load,
    test_api::test_resources_uav_indexed_buffer_raw_load,
    test_api::test_resources_uav_indexed_buffer_typed_load,
  };

  for (auto test : tests) {
    for (uint32_t count : { 1u, 2u, 0u }) {
      for (bool nonUniform : { false, true }) {
        for (bool raw : { false, true }) {
          auto builder = makeDescriptorTest(test, count, nonUniform);
          SpirvBuilder::Options options = { };
          options.nvRawAccessChains = raw;
          auto binary = buildSpirv(builder, options);

          bool capability = false;
          bool decoration = false;

          for (size_t i = 5u; i < binary.size(); i += binary[i] >> 16u) {
            auto op = spv::Op(binary[i] & 0xffffu);

            if (op == spv::OpCapability && binary[i + 1u] == spv::CapabilityShaderNonUniform)
              capability = true;
            if (op == spv::OpDecorate && binary[i + 2u] == spv::DecorationNonUniform)
              decoration = true;
          }

          /* A one-descriptor binding still inherits the IR's NonUniform
           * decoration and must declare the capability that it requires. */
          ok(decoration == nonUniform);
          ok(capability == nonUniform);
        }
      }
    }
  }
}

}
