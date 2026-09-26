#include "../../util/util_vle.h"

#include "../test_common.h"

#include <array>

namespace dxbc_spv::tests::util {

using namespace dxbc_spv::util;

void testVariableLengthEncodingBasic() {
  std::array<uint8_t, 9u> data = { };

  for (int32_t i = -16384; i <= 16384; i++) {
    size_t s = vle::encode(uint64_t(i), data.data(), data.size());

    ok(s);
    ok(s == vle::encodedSize(uint64_t(i)));

    uint64_t decoded = 0u;
    s = vle::decode(decoded, data.data(), s);

    ok(i == int64_t(decoded));
    ok(s == vle::encodedSize(uint64_t(i)));

    decoded = ~decoded;

    /* Ensure that passing in a larger size works too */
    s = vle::decode(decoded, data.data(), data.size());
    ok(i == int64_t(decoded));
    ok(s == vle::encodedSize(uint64_t(i)));
  }


}


void testVariableLengthEncodingInvalid() {
  std::array<uint8_t, 9> data = { 0xff, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

  uint64_t sym = 0xdeadbeefu;

  ok(!vle::decode(sym, data.data(), 0)); ok(sym == 0xdeadbeefu);
  ok(!vle::decode(sym, data.data(), 8)); ok(sym == 0xdeadbeefu);

  ok(!vle::encode(sym, data.data(), 0));
  ok(!vle::encode(sym, data.data(), 1));
  ok(!vle::encode(sym, data.data(), 2));
  ok(!vle::encode(sym, data.data(), 3));
  ok(!vle::encode(sym, data.data(), 4));

  ok(vle::decode(sym, data.data(), 9));
}


void testVariableLengthEncodingWide() {
  std::array<uint8_t, 9u> data = { };
  for (uint32_t bit = 0u; bit < 64u; bit++) {
    auto value = uint64_t(1u) << bit;
    for (auto symbol : { value, 0u - value, value - 1u }) {
      auto size = vle::encodedSize(symbol);
      ok(vle::encode(symbol, data.data(), size) == size);
      uint64_t decoded = ~symbol;
      ok(vle::decode(decoded, data.data(), size) == size);
      ok(decoded == symbol);
      ok(!vle::encode(symbol, data.data(), size - 1u));
      if (size == 9u) {
        ok(data[0u] == 0xffu);
        for (uint32_t i = 1u; i < 9u; i++)
          ok(data[i] == uint8_t(symbol >> (8u * (8u - i))));
      }
    }
  }
}


void testVariableLengthEncoding() {
  RUN_TEST(testVariableLengthEncodingBasic);
  RUN_TEST(testVariableLengthEncodingInvalid);
  RUN_TEST(testVariableLengthEncodingWide);
}

}
