/*
 * Opcodes — the protoScala instruction set (DESIGN §3.5).
 *
 * One 32-bit word per instruction: opcode in the low 8 bits, unsigned 24-bit
 * operand in the high bits. EXTEND supplies bits 24..47 of the next
 * instruction's operand. The numbering is fixed; later phases only append in
 * their reserved ranges, and docs/STATUS.md mirrors this table.
 *
 * Stack effects are written as [before] -> [after], top of stack on the right.
 */
#pragma once
#include <cstdint>

namespace protoScala {

enum class Op : uint8_t {
    NOP          = 0,
    EXTEND       = 1,   // operand: high 24 bits of the next instruction's operand
    // Constants and stack
    PUSH_CONST   = 2,   // [] -> [consts[operand]]
    PUSH_UNIT    = 3,   // [] -> [()]
    PUSH_NULL    = 4,   // [] -> [null]
    PUSH_TRUE    = 5,
    PUSH_FALSE   = 6,
    POP          = 7,   // [v] -> []
    DUP          = 8,   // [v] -> [v v]
    // Locals and boxed cells (captured vars and local defs)
    PUSH_LOCAL   = 9,   // [] -> [slot[operand]]
    STORE_LOCAL  = 10,  // [v] -> []            slot[operand] = v
    MAKE_CELL    = 11,  // [] -> []             slot[operand] = new Cell(null)
    PUSH_CELL    = 12,  // [] -> [slot[operand].value]
    STORE_CELL   = 13,  // [v] -> []            slot[operand].value = v
    // Globals (operand: a Symbol constant)
    PUSH_GLOBAL  = 14,  // [] -> [globals.name]
    STORE_GLOBAL = 15,  // [v] -> []
    // Functions and calls
    MAKE_FN      = 16,  // [c1..cn] -> [fn]     operand: block index; n = its captureCount
    CALL         = 17,  // [f a1..an] -> [r]    operand: n
    CALL_SPREAD  = 18,  // [f a1..an list] -> [r]  operand: n; list elements follow a1..an
    SEND         = 19,  // [recv a1..an] -> [r] operand: SendSite constant (name, n)
    RETURN       = 20,  // [v] -> returns v
    MAKE_LAZY    = 21,  // [thunk] -> [lazy]
    FORCE        = 22,  // [v] -> [forced v]    evaluates a lazy once; other values pass
    // Control flow (offsets in words, from the next instruction)
    JUMP          = 23,
    JUMP_IF_FALSE = 24, // [b] -> []            b must be a Boolean
    JUMP_IF_TRUE  = 25, // [b] -> []
    JUMP_BACK     = 26, // backward; also a GC safepoint (Open question Q21)
    // SmallInteger fast paths with protoCore fallback (DESIGN §3.6)
    ADD = 27, SUB = 28, MUL = 29,          // [a b] -> [r]
    LT = 30, LE = 31, GT = 32, GE = 33,    // [a b] -> [Boolean]
    EQ = 34, NE = 35,                      // Scala == and !=
    NEG = 36,                              // [a] -> [-a]
    NOT = 37,                              // [b] -> [!b]
    // 38..63   reserved (Phase 1 additions)
    // Object model and pattern matching, Phase 2 (64..95); stack effects in
    // the Phase 2 plan's opcode table and in docs/STATUS.md.
    MAKE_CLASS     = 64,  // [p1..pk m1..mn] -> [cls]    operand: ClassSpec constant
    NEW            = 65,  // [cls a1..an] -> [obj]        operand: SendSite (constructor key, n)
    INVOKE_INIT    = 66,  // [cls this a1..an] -> [this'] operand: SendSite (constructor key, n)
    STORE_FIELD    = 67,  // [v] -> []   slot[0] = slot[0].setAttribute(key, v); operand: Symbol
    SET_FIELD      = 68,  // [obj v] -> [] obj must be mutable;              operand: Symbol
    SEND_SUPER     = 69,  // [this a1..an] -> [r]         operand: SuperSite constant
    TEST_TYPE      = 70,  // [v] -> [Boolean]             operand: TypeCode
    TEST_PROTO     = 71,  // [v] -> [Boolean]             operand: Symbol (type key: the class marker)
    UNAPPLY_FIELDS = 72,  // [v] -> [f1..fn]              operand: Names constant (attribute keys)
    UNCONS         = 73,  // [list] -> [head tail]        list must be non-empty
    MATCH_ERROR    = 74,  // [v] -> throws MatchError
    CAST_FAIL      = 75,  // [v] -> throws ClassCastException; operand: String constant (type name)
    MAKE_TUPLE     = 76,  // [a1..an] -> [tuple]          operand: n (2..22)
    SEND_KW        = 77,  // [recv a1..an v1..vm] -> [r]  operand: KwSendSite constant
    NEW_SPREAD     = 78,  // [cls a1..an list] -> [obj]   operand: SendSite (constructor key, n)
    SEND_APPLY     = 79,  // [recv a1..an] -> [r]         operand: SendSite (name, n)
                          // `recv.m(args)` written with an argument list: calls the
                          // member when it is a method, else applies its value.
    // 80..95   reserved (object model)
    // 96..127  exceptions, Phase 4: THROW (+ per-module handler table)
    // 128..159 actors, Phase 5: SEND_ASYNC, ASK, AWAIT
};

using Instr = std::uint32_t;
inline constexpr unsigned      kOperandShift       = 8;
inline constexpr std::uint32_t kMaxOperand         = (1u << 24) - 1;
inline constexpr std::uint64_t kMaxExtendedOperand = (1ull << 48) - 1;

// Operand of TEST_TYPE: the built-in types a type pattern or isInstanceOf can
// name (D29: Int = Long = Short = Byte = BigInt, Float = Double).
enum class TypeCode : uint8_t {
    Integer, Double, Boolean, Char, String, Unit, List, ConsList, Function,
    AnyRef, AnyVal, Null, NonNull, Nothing,
};
const char* typeCodeName(TypeCode code);

const char* opName(Op op);

} // namespace protoScala
