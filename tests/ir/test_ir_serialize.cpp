#include <unordered_map>

#include "../../ir/ir_builder.h"
#include "../../ir/ir_serialize.h"

#include "../test_common.h"

namespace dxbc_spv::tests::ir {

using namespace dxbc_spv::ir;

void testIrSerializeBuilder(const Builder& srcBuilder, bool fresh) {
  Serializer serializer(srcBuilder);

  std::vector<uint8_t> data(serializer.computeSerializedSize());
  ok(serializer.serialize(data.data(), data.size()));

  Builder newBuilder;

  Deserializer deserializer(data.data(), data.size());
  ok(fresh ? deserializer.deserializeFresh(newBuilder) : deserializer.deserialize(newBuilder));
  ok(deserializer.atEnd());

  /* Verify that instructions are in the same order and operands are the same. */
  std::unordered_map<SsaDef, SsaDef> defMap;

  auto a = srcBuilder.begin();
  auto b = newBuilder.begin();

  while (a != srcBuilder.end() && b != newBuilder.end()) {
    ok(*a && a->getDef());
    ok(*b && b->getDef());

    defMap.insert({ (b++)->getDef(), (a++)->getDef() });
  }

  ok(a == srcBuilder.end());
  ok(b == newBuilder.end());

  a = srcBuilder.begin();
  b = newBuilder.begin();

  while (a != srcBuilder.end() && b != newBuilder.end()) {
    ok(a->getOpCode() == b->getOpCode());
    ok(a->getType() == b->getType());
    ok(a->getFlags() == b->getFlags());
    ok(a->getOperandCount() == b->getOperandCount());
    ok(a->getFirstLiteralOperandIndex() == b->getFirstLiteralOperandIndex());

    for (uint32_t i = 0u; i < a->getFirstLiteralOperandIndex(); i++) {
      auto aDef = SsaDef(a->getOperand(i));
      auto bDef = SsaDef(b->getOperand(i));

      if (aDef && bDef) {
        auto e = defMap.find(bDef);
        ok(e != defMap.end());

        if (e != defMap.end())
          ok(e->second == aDef);
      } else {
        ok(!aDef && !bDef);
      }
    }

    for (uint32_t i = a->getFirstLiteralOperandIndex(); i < a->getOperandCount(); i++)
      ok(uint64_t(a->getOperand(i)) == uint64_t(b->getOperand(i)));

    a++;
    b++;
  }
}


std::vector<uint8_t> serializeBuilder(const Builder& builder) {
  Serializer serializer(builder);
  std::vector<uint8_t> data(serializer.computeSerializedSize());
  ok(serializer.serialize(data.data(), data.size()));
  return data;
}


void testIrDeserializeReset() {
  Builder source;
  auto one = source.makeConstant(1u);
  auto two = source.makeConstant(2u);
  auto sum = source.add(Op::IAdd(ScalarType::eU32, one, two));
  source.add(Op::Drain(ScalarType::eU32, sum, one));
  auto data = serializeBuilder(source);

  /* The fresh path keeps the existing allocation. */
  Builder fresh;
  const auto* nullOp = &fresh.getOp(SsaDef());
  ok(Deserializer(data.data(), data.size()).deserializeFresh(fresh));
  ok(&fresh.getOp(SsaDef()) == nullOp);
  ok(serializeBuilder(fresh) == data);
  ok(fresh.getUseCount(one) == 2u);

  /* Reset constants, use lists, cursor and multiple pages of old storage.
   * A copy sharing the old storage must remain unchanged. */
  Builder reused;
  for (uint32_t i = 0u; i < 5000u; i++)
    reused.makeConstant(i);
  reused.setCursor(reused.add(Op::Label()));
  Builder shared = reused;
  auto sharedData = serializeBuilder(shared);

  ok(Deserializer(data.data(), data.size()).deserialize(reused));
  ok(serializeBuilder(reused) == data);
  ok(serializeBuilder(shared) == sharedData);
  ok(reused.getDefCount() == source.getDefCount());
  ok(reused.getUseCount(one) == 2u);
  ok(!reused.setCursor(SsaDef()));
  ok(reused.makeConstant(1u) == one);

  /* An empty instruction list can still have a free list and old SSA IDs. */
  Builder removed;
  auto a = removed.makeConstant(10u);
  auto b = removed.makeConstant(20u);
  removed.remove(a);
  removed.remove(b);
  ok(removed.begin() == removed.end());
  ok(removed.getDefCount() > 1u);
  ok(Deserializer(data.data(), data.size()).deserialize(removed));
  ok(serializeBuilder(removed) == data);

  /* Even a never-populated builder may share its initial allocation. */
  Builder empty;
  Builder emptyCopy = empty;
  ok(Deserializer(data.data(), data.size()).deserialize(emptyCopy));
  auto constant = empty.makeConstant(99u);
  ok(constant == one);
  ok(serializeBuilder(emptyCopy) == data);

  /* Failure resets the destination as before. Retrying after a partial
   * decode must not retain instructions or constants from that attempt. */
  ok(!Deserializer(nullptr, 0u).deserialize(reused));
  ok(reused.getDefCount() == 1u);
  ok(reused.begin() == reused.end());
  ok(!Deserializer(data.data(), data.size() - 1u).deserialize(reused));
  ok(Deserializer(data.data(), data.size()).deserialize(reused));
  ok(serializeBuilder(reused) == data);

  Builder failedFresh;
  ok(!Deserializer(data.data(), data.size() - 1u).deserializeFresh(failedFresh));
  ok(Deserializer(data.data(), data.size()).deserialize(failedFresh));
  ok(serializeBuilder(failedFresh) == data);

  auto emptyData = serializeBuilder(Builder());
  ok(Deserializer(emptyData.data(), emptyData.size()).deserialize(reused));
  ok(reused.getDefCount() == 1u);
  ok(reused.begin() == reused.end());
}


void testIrSerialize() {
  testIrSerializeBuilder(Builder(), false);
  testIrSerializeBuilder(Builder(), true);
  RUN_TEST(testIrDeserializeReset);

  Builder builder;

  auto funcDef = builder.add(Op::Function(Type()));
  builder.add(Op::DebugName(funcDef, "main"));

  auto entryPointDef = builder.add(
    Op::EntryPoint(funcDef, ShaderStage::eVertex));
  builder.add(Op::DebugName(entryPointDef, "vertex_shader_123"));

  auto vertexIdDef = builder.add(
    Op::DclInputBuiltIn(ScalarType::eU32, entryPointDef, BuiltIn::eVertexId));
  builder.add(Op::DebugName(vertexIdDef, "v0"));
  builder.add(Op::Semantic(vertexIdDef, 0, "SV_VERTEXID"));

  auto positionDef = builder.add(
    Op::DclOutputBuiltIn(BasicType(ScalarType::eF32, 4), entryPointDef, BuiltIn::ePosition));
  builder.add(Op::DebugName(positionDef, "o0"));
  builder.add(Op::Semantic(positionDef, 0, "SV_POSITION"));

  builder.add(Op::Label());

  auto vidDef = builder.add(Op::InputLoad(ScalarType::eU32, SsaDef(), vertexIdDef));
  auto xDef = builder.add(Op::Select(ScalarType::eF32,
    builder.add(Op::INe(ScalarType::eBool, builder.makeConstant(0u),
      builder.add(Op::IAnd(ScalarType::eU32, vidDef, builder.makeConstant(1u))))),
    builder.makeConstant(3.0f),
    builder.makeConstant(-1.0f)));
  auto yDef = builder.add(Op::Select(ScalarType::eF32,
    builder.add(Op::INe(ScalarType::eBool, builder.makeConstant(0u),
      builder.add(Op::IAnd(ScalarType::eU32, vidDef, builder.makeConstant(2u)).setFlags(OpFlag::ePrecise)))),
    builder.makeConstant(3.0f),
    builder.makeConstant(-1.0f)));
  auto zDef = builder.makeConstant(0.0f);
  auto wDef = builder.makeConstant(1.0f);

  auto vecDef = builder.add(Op::CompositeConstruct(BasicType(ScalarType::eF32, 4), xDef, yDef, zDef, wDef));
  builder.add(Op::OutputStore(positionDef, SsaDef(), vecDef));
  builder.add(Op::Return());
  builder.add(Op::FunctionEnd());

  testIrSerializeBuilder(builder, false);
  testIrSerializeBuilder(builder, true);
}

}
