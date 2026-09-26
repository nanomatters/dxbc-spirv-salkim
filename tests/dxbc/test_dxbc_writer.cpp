#include "../../dxbc/dxbc_parser.h"

#include "../test_common.h"

#include <vector>

namespace dxbc_spv::tests::dxbc {

using namespace dxbc_spv::dxbc;

static void testInstructionRoundTrip(const std::vector<uint32_t>& words,
                                    const std::vector<uint32_t>& expected) {
  ShaderInfo info(ShaderType::eVertex, 5u, 0u, 2u);
  util::ByteWriter inputWriter;

  for (auto word : words)
    ok(inputWriter.write(word));

  auto input = std::move(inputWriter).extract();
  util::ByteReader inputReader(input.data(), input.size());
  Instruction instruction(inputReader, info);
  ok(instruction);
  ok(!inputReader.getRemaining());

  util::ByteWriter outputWriter;
  ok(instruction.write(outputWriter, info));
  ok(Instruction(OpCode::eRet).write(outputWriter, info));

  auto output = std::move(outputWriter).extract();
  ok(output.size() == (expected.size() + 1u) * sizeof(uint32_t));

  util::ByteReader wordReader(output.data(), output.size());
  for (auto word : expected) {
    uint32_t actual = 0u;
    ok(wordReader.read(actual));
    ok(actual == word);
  }

  util::ByteReader outputReader(output.data(), output.size());
  Instruction rewritten(outputReader, info);
  ok(rewritten);
  ok(outputReader.getRemaining() == sizeof(uint32_t));
  Instruction next(outputReader, info);
  ok(next && next.getOpToken().getOpCode() == OpCode::eRet);
  ok(!outputReader.getRemaining());
}


static void testInstructionRoundTrip(const std::vector<uint32_t>& words) {
  testInstructionRoundTrip(words, words);
}


void testDxbcWriteImmediateOperands() {
  /* Scalar and vector mov immediates, including raw NaN and signed-zero bits. */
  testInstructionRoundTrip({
    0x05000036u, 0x00100012u, 0u, 0x00004001u, 0x3f800000u });
  testInstructionRoundTrip({
    0x08000036u, 0x001000f2u, 0u, 0x00004002u,
    0x00000000u, 0x80000000u, 0x7fc12345u, 0xff800000u });

  /* Scalar and two-double dmov immediates. */
  testInstructionRoundTrip({
    0x060000c7u, 0x00100032u, 0u, 0x00005001u, 0u, 0x3ff00000u });
  testInstructionRoundTrip({
    0x080000c7u, 0x001000f2u, 0u, 0x00005002u,
    0x12345678u, 0x7ff81234u, 0u, 0x80000000u });

  /* Immediate payload follows any operand modifiers. */
  testInstructionRoundTrip({
    0x06000036u, 0x00100012u, 0u, 0x80004001u, 0x41u, 0x3f800000u });

  /* Literal declaration operands and indexed registers are unchanged. */
  testInstructionRoundTrip({ 0x02000068u, 7u });
  testInstructionRoundTrip({
    0x05000036u, 0x001000f2u, 0u, 0x00100e46u, 1u });
}


void testDxbcWriteCustomData() {
  /* A single immediate-constant-buffer entry. */
  testInstructionRoundTrip({ 0x00001835u, 6u, 1u, 2u, 3u, 4u });

  /* Empty comment and opaque debug-information payload. */
  testInstructionRoundTrip({ 0x00000035u, 2u });
  testInstructionRoundTrip({ 0x00000835u, 5u, 0u, 0xffffffffu, 0x0100003eu });

  /* Custom data uses a separate 32-bit length, not the 7-bit opcode field. */
  std::vector<uint32_t> large = { 0x00001835u, 258u };
  for (uint32_t i = 0u; i < 256u; i++)
    large.push_back(0x80000000u | i);
  testInstructionRoundTrip(large);
}


void testDxbcWriteExtraOperands() {
  /* dcl_function_table ft0 = { fb3, fb4 }. */
  testInstructionRoundTrip({ 0x05000091u, 0u, 2u, 3u, 4u });

  /* A longer table, including a zero-valued body index. */
  testInstructionRoundTrip({ 0x07000091u, 7u, 4u, 0u, 1u, 3u, 9u });

  /* Preserve padding retained by the parser as an extra word. */
  testInstructionRoundTrip({ 0x03000068u, 7u, 0u });
}

}
