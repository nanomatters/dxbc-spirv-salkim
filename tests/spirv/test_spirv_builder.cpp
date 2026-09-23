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


void testSpirvCbvStorageClass() {
  for (uint32_t count : { 1u, 2u, 0u }) {
    for (bool nonUniform : { false, true }) {
      for (bool raw : { false, true }) {
        for (uint32_t limit = 0u; limit < 3u; limit++) {
          auto builder = makeDescriptorTest(test_api::test_resources_cbv_indexed_nonuniform, count, nonUniform);
          SpirvBuilder::Options options = { };
          options.nvRawAccessChains = raw;

          /* Exercise both ways to demote a CBV, as well as the UBO path. */
          if (limit == 1u)
            options.maxCbvCount = 0;
          if (limit == 2u)
            options.maxCbvSize = 16u;

          auto binary = buildSpirv(builder, options);
          auto expected = limit ? spv::StorageClassStorageBuffer : spv::StorageClassUniform;

          std::vector<uint32_t> types(binary[3u]);
          std::vector<spv::StorageClass> storageClasses(binary[3u], spv::StorageClassMax);
          uint32_t accessChains = 0u;
          uint32_t descriptors = 0u;

          for (size_t i = 5u; i < binary.size(); i += binary[i] >> 16u) {
            auto op = spv::Op(binary[i] & 0xffffu);

            if (op == spv::OpTypePointer) {
              storageClasses.at(binary[i + 1u]) = spv::StorageClass(binary[i + 2u]);
            } else if (op == spv::OpVariable) {
              types.at(binary[i + 2u]) = binary[i + 1u];

              auto storage = spv::StorageClass(binary[i + 3u]);
              if (storage == spv::StorageClassUniform || storage == spv::StorageClassStorageBuffer) {
                descriptors++;
                ok(storage == expected);
              }
            } else if (op == spv::OpAccessChain || op == spv::OpInBoundsAccessChain) {
              auto resultType = binary[i + 1u];
              auto baseType = types.at(binary[i + 3u]);
              types.at(binary[i + 2u]) = resultType;
              accessChains++;

              /* The descriptor-array pointer and element pointers must all
               * use the storage class chosen for the variable declaration. */
              ok(storageClasses.at(baseType) == expected);
              ok(storageClasses.at(resultType) == expected);
            }
          }

          ok(descriptors == 1u);
          ok(accessChains == (count == 1u ? 1u : 2u));
        }
      }
    }
  }
}

}
