#include "compiler/BytecodeModule.h"
#include "protoCore.h"

#include <gtest/gtest.h>

#include <cmath>

using protoScala::BytecodeModule;
using protoScala::Op;

TEST(Bytecode, ConstantPoolDeduplicatesByKind) {
    BytecodeModule m;
    const auto a = m.addInt(1);
    EXPECT_EQ(m.addInt(1), a);
    EXPECT_NE(m.addDouble(1.0), a);
    EXPECT_NE(m.addString("1"), a);
    EXPECT_EQ(m.addString("x"), m.addString("x"));
    EXPECT_NE(m.addDouble(0.0), m.addDouble(-0.0));  // by bit pattern
    EXPECT_EQ(m.addSymbol("println"), m.addSymbol("println"));
    EXPECT_NE(m.addSymbol("x"), m.addString("x"));
    EXPECT_EQ(m.addSendSite("+", 1), m.addSendSite("+", 1));
    EXPECT_NE(m.addSendSite("+", 1), m.addSendSite("+", 2));
    EXPECT_EQ(m.addBigInt("123456789012345678901234567890", 10),
              m.addBigInt("123456789012345678901234567890", 10));
    EXPECT_EQ(m.addChar(U'a'), m.addChar(U'a'));
}

TEST(Bytecode, EmitEncodesOpcodeOperandAndLine) {
    BytecodeModule m;
    const auto at = m.emit(Op::PUSH_CONST, 7, 3);
    EXPECT_EQ(at, 0u);
    EXPECT_EQ(m.code()[0] & 0xFF, static_cast<unsigned>(Op::PUSH_CONST));
    EXPECT_EQ(m.code()[0] >> 8, 7u);
    EXPECT_EQ(m.lineAt(0), 3);
}

TEST(Bytecode, WideOperandsUseExtend) {
    BytecodeModule m;
    const std::uint64_t wide = (1ull << 24) + 5;
    const auto at = m.emit(Op::PUSH_LOCAL, wide, 1);
    ASSERT_EQ(m.code().size(), 2u);
    EXPECT_EQ(at, 1u);  // position of the real instruction
    EXPECT_EQ(m.code()[0] & 0xFF, static_cast<unsigned>(Op::EXTEND));
    EXPECT_EQ(m.code()[0] >> 8, 1u);
    EXPECT_EQ(m.code()[1] >> 8, 5u);
    EXPECT_NE(m.disassemble().find("PUSH_LOCAL 16777221"), std::string::npos);
    EXPECT_THROW(m.emit(Op::PUSH_LOCAL, 1ull << 48, 1), std::length_error);
}

TEST(Bytecode, ForwardAndBackwardJumps) {
    BytecodeModule m;
    const auto top = m.pos();
    m.emit(Op::PUSH_TRUE, 0, 1);
    const auto j = m.emitJump(Op::JUMP_IF_FALSE, 1);
    m.emit(Op::PUSH_UNIT, 0, 1);
    m.emit(Op::POP, 0, 1);
    m.emitJumpBack(top, 1);
    m.patchJumpTo(j, m.pos());
    EXPECT_EQ(m.code()[j] >> 8, 3u);          // skips PUSH_UNIT, POP, JUMP_BACK
    EXPECT_EQ(m.code()[4] >> 8, 5u);          // back over 5 words to `top`
    EXPECT_EQ(m.code()[4] & 0xFF, static_cast<unsigned>(Op::JUMP_BACK));
}

TEST(Bytecode, MetadataCapturesAndBlocks) {
    BytecodeModule m;
    m.setName("f");
    m.setArity(2);
    m.setVariadic(true);
    m.setLocalCount(3);
    m.setMaxStack(4);
    m.addCapture(1, 5);
    auto sub = std::make_unique<BytecodeModule>();
    sub->setName("<lambda>");
    EXPECT_EQ(m.addBlock(std::move(sub)), 0u);
    EXPECT_EQ(m.block(0).name(), "<lambda>");
    EXPECT_EQ(m.captureCount(), 1);
    EXPECT_EQ(m.captureSpecs()[0].parentSlot, 1);
    EXPECT_EQ(m.captureSpecs()[0].localSlot, 5);
    EXPECT_EQ(m.arity(), 2);
    EXPECT_TRUE(m.isVariadic());
}

TEST(Bytecode, DisassemblyNamesConstants) {
    BytecodeModule m;
    m.setName("<top>");
    m.emit(Op::PUSH_GLOBAL, m.addSymbol("println"), 1);
    m.emit(Op::PUSH_CONST, m.addString("hi"), 1);
    m.emit(Op::CALL, 1, 1);
    m.emit(Op::RETURN, 0, 2);
    const std::string text = m.disassemble();
    EXPECT_NE(text.find("function <top> arity=0"), std::string::npos);
    EXPECT_NE(text.find("PUSH_GLOBAL 0 ; println"), std::string::npos);
    EXPECT_NE(text.find("PUSH_CONST 1 ; \"hi\""), std::string::npos);
    EXPECT_NE(text.find("L2  RETURN"), std::string::npos);
}

TEST(Bytecode, LinkSymbolsInternsInTheSpace) {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    BytecodeModule m;
    const auto s = m.addSymbol("answer");
    const auto site = m.addSendSite("length", 0);
    auto sub = std::make_unique<BytecodeModule>();
    const auto inner = sub->addSymbol("inner");
    m.addBlock(std::move(sub));
    m.linkSymbols(ctx);
    EXPECT_EQ(m.constAt(s).symbol, proto::ProtoString::createSymbol(ctx, "answer"));
    EXPECT_EQ(m.constAt(site).symbol, proto::ProtoString::createSymbol(ctx, "length"));
    EXPECT_EQ(m.block(0).constAt(inner).symbol, proto::ProtoString::createSymbol(ctx, "inner"));
}
