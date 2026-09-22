#include "compiler/BytecodeModule.h"

#include "protoCore.h"

#include <cstdio>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace protoScala {

const char* opName(Op op) {
    switch (op) {
        case Op::NOP:           return "NOP";
        case Op::EXTEND:        return "EXTEND";
        case Op::PUSH_CONST:    return "PUSH_CONST";
        case Op::PUSH_UNIT:     return "PUSH_UNIT";
        case Op::PUSH_NULL:     return "PUSH_NULL";
        case Op::PUSH_TRUE:     return "PUSH_TRUE";
        case Op::PUSH_FALSE:    return "PUSH_FALSE";
        case Op::POP:           return "POP";
        case Op::DUP:           return "DUP";
        case Op::PUSH_LOCAL:    return "PUSH_LOCAL";
        case Op::STORE_LOCAL:   return "STORE_LOCAL";
        case Op::MAKE_CELL:     return "MAKE_CELL";
        case Op::PUSH_CELL:     return "PUSH_CELL";
        case Op::STORE_CELL:    return "STORE_CELL";
        case Op::PUSH_GLOBAL:   return "PUSH_GLOBAL";
        case Op::STORE_GLOBAL:  return "STORE_GLOBAL";
        case Op::MAKE_FN:       return "MAKE_FN";
        case Op::CALL:          return "CALL";
        case Op::CALL_SPREAD:   return "CALL_SPREAD";
        case Op::SEND:          return "SEND";
        case Op::RETURN:        return "RETURN";
        case Op::MAKE_LAZY:     return "MAKE_LAZY";
        case Op::FORCE:         return "FORCE";
        case Op::JUMP:          return "JUMP";
        case Op::JUMP_IF_FALSE: return "JUMP_IF_FALSE";
        case Op::JUMP_IF_TRUE:  return "JUMP_IF_TRUE";
        case Op::JUMP_BACK:     return "JUMP_BACK";
        case Op::ADD:           return "ADD";
        case Op::SUB:           return "SUB";
        case Op::MUL:           return "MUL";
        case Op::LT:            return "LT";
        case Op::LE:            return "LE";
        case Op::GT:            return "GT";
        case Op::GE:            return "GE";
        case Op::EQ:            return "EQ";
        case Op::NE:            return "NE";
        case Op::NEG:           return "NEG";
        case Op::NOT:           return "NOT";
    }
    return "?";
}

std::size_t BytecodeModule::addBlock(std::unique_ptr<BytecodeModule> sub) {
    blocks_.push_back(std::move(sub));
    return blocks_.size() - 1;
}

namespace {

// The index of the constant `key` names in `index`, appending `c` to the
// pool first when the key is new.
template <typename Key>
std::size_t findOrAdd(std::unordered_map<Key, std::size_t>& index,
                       const Key& key,
                       std::vector<BytecodeModule::Const>& consts,
                       BytecodeModule::Const c) {
    const auto [it, inserted] = index.try_emplace(key, consts.size());
    if (inserted) consts.push_back(std::move(c));
    return it->second;
}

} // namespace

std::size_t BytecodeModule::addInt(long long v) {
    return findOrAdd(intIndex_, v, consts_, Const{ConstKind::Int, v, 0.0, {}});
}

std::size_t BytecodeModule::addBigInt(const std::string& digits, int base) {
    const std::string key = std::to_string(base) + ":" + digits;
    Const c{ConstKind::BigInt, 0, 0.0, digits};
    c.base = base;
    return findOrAdd(bigIntIndex_, key, consts_, std::move(c));
}

std::size_t BytecodeModule::addDouble(double v) {
    // By bit pattern: 0.0 and -0.0 are two distinct constants, and a NaN
    // literal reuses the entry of an identical NaN.
    std::uint64_t bits = 0;
    std::memcpy(&bits, &v, sizeof bits);
    return findOrAdd(doubleIndex_, bits, consts_, Const{ConstKind::Double, 0, v, {}});
}

std::size_t BytecodeModule::addString(const std::string& s) {
    return findOrAdd(stringIndex_, s, consts_, Const{ConstKind::String, 0, 0.0, s});
}

std::size_t BytecodeModule::addChar(char32_t c) {
    const long long key = static_cast<long long>(c);
    return findOrAdd(charIndex_, key, consts_, Const{ConstKind::Char, key, 0.0, {}});
}

std::size_t BytecodeModule::addSymbol(const std::string& name) {
    return findOrAdd(symbolIndex_, name, consts_, Const{ConstKind::Symbol, 0, 0.0, name});
}

std::size_t BytecodeModule::addSendSite(const std::string& name, std::uint32_t argc) {
    const std::string key = name + "/" + std::to_string(argc);
    Const c{ConstKind::SendSite, 0, 0.0, name};
    c.argc = argc;
    return findOrAdd(sendIndex_, key, consts_, std::move(c));
}

std::size_t BytecodeModule::emit(Op op, std::uint64_t operand, int line) {
    if (operand > kMaxExtendedOperand)
        throw std::length_error(std::string(opName(op)) + " operand " +
                                std::to_string(operand) + " exceeds the bytecode limit");
    if (operand > kMaxOperand) {
        code_.push_back(static_cast<Instr>((operand >> 24) << kOperandShift) |
                        static_cast<Instr>(Op::EXTEND));
        lines_.push_back(line);
        operand &= kMaxOperand;
    }
    code_.push_back((static_cast<Instr>(operand) << kOperandShift) | static_cast<Instr>(op));
    lines_.push_back(line);
    return code_.size() - 1;
}

std::size_t BytecodeModule::emitJump(Op op, int line) { return emit(op, 0, line); }

void BytecodeModule::patchJumpTo(std::size_t jumpAt, std::size_t target) {
    if (jumpAt >= code_.size() || target <= jumpAt)
        throw std::out_of_range("patchJumpTo: bad jump or target");
    const std::size_t offset = target - (jumpAt + 1);
    if (offset > kMaxOperand) throw std::length_error("function body too large");
    const Instr op = code_[jumpAt] & 0xFF;
    code_[jumpAt] = (static_cast<Instr>(offset) << kOperandShift) | op;
}

std::size_t BytecodeModule::emitJumpBack(std::size_t target, int line) {
    const std::size_t offset = (code_.size() + 1) - target;
    if (offset > kMaxOperand) throw std::length_error("function body too large");
    return emit(Op::JUMP_BACK, offset, line);
}

void BytecodeModule::linkSymbols(proto::ProtoContext* ctx) {
    for (Const& c : consts_)
        if (c.kind == ConstKind::Symbol || c.kind == ConstKind::SendSite)
            c.symbol = proto::ProtoString::createSymbol(ctx, c.sval);
    for (auto& b : blocks_) b->linkSymbols(ctx);
}

namespace {

std::string escapeString(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (const char ch : s) {
        switch (ch) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            case '\r': out += "\\r"; break;
            default:   out.push_back(ch);
        }
    }
    out.push_back('"');
    return out;
}

std::string formatDouble(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return std::string(buf);
}

std::string formatChar(long long codepoint) {
    std::string out = "'";
    if (codepoint >= 0x20 && codepoint < 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else {
        char buf[16];
        std::snprintf(buf, sizeof buf, "U+%llX", static_cast<unsigned long long>(codepoint));
        out += buf;
    }
    out.push_back('\'');
    return out;
}

std::string formatConst(const BytecodeModule::Const& c) {
    switch (c.kind) {
        case BytecodeModule::ConstKind::Int:      return std::to_string(c.ival);
        case BytecodeModule::ConstKind::BigInt:   return c.sval;
        case BytecodeModule::ConstKind::Double:   return formatDouble(c.dval);
        case BytecodeModule::ConstKind::String:   return escapeString(c.sval);
        case BytecodeModule::ConstKind::Char:     return formatChar(c.ival);
        case BytecodeModule::ConstKind::Symbol:   return c.sval;
        case BytecodeModule::ConstKind::SendSite: return c.sval + "/" + std::to_string(c.argc);
    }
    return "?";
}

std::string fourDigits(std::size_t pc) {
    char buf[24];
    std::snprintf(buf, sizeof buf, "%04zu", pc);
    return std::string(buf);
}

// Comment appended after the operand, including its leading " ; ", or "" when
// the opcode carries no annotation.
std::string commentFor(const BytecodeModule& m, Op op, std::uint64_t operand, std::size_t instrPc) {
    switch (op) {
        case Op::PUSH_CONST:
            return " ; " + formatConst(m.constAt(operand));
        case Op::PUSH_GLOBAL:
        case Op::STORE_GLOBAL:
            return " ; " + m.constAt(operand).sval;
        case Op::SEND: {
            const auto& c = m.constAt(operand);
            return " ; " + c.sval + "/" + std::to_string(c.argc);
        }
        case Op::MAKE_FN:
            return " ; -> block " + std::to_string(operand);
        case Op::JUMP:
        case Op::JUMP_IF_FALSE:
        case Op::JUMP_IF_TRUE:
            return " ; -> " + std::to_string(instrPc + 1 + operand);
        case Op::JUMP_BACK:
            return " ; -> " + std::to_string(instrPc + 1 - operand);
        default:
            return "";
    }
}

} // namespace

std::string BytecodeModule::disassemble() const {
    std::string out;
    std::function<void(const BytecodeModule&, int)> go =
        [&](const BytecodeModule& m, int indent) {
            const std::string pad(static_cast<std::size_t>(indent) * 2, ' ');
            out += pad + "function " + m.name_ + " arity=" + std::to_string(m.arity_);
            if (m.variadic_) out += " variadic";
            out += " locals=" + std::to_string(m.localCount_) +
                   " stack=" + std::to_string(m.maxStack_) +
                   " captures=" + std::to_string(m.captures_.size()) + "\n";

            std::size_t pc = 0;
            while (pc < m.code_.size()) {
                const Instr word = m.code_[pc];
                const Op word0p = static_cast<Op>(word & 0xFF);
                std::uint64_t operand = word >> kOperandShift;
                if (word0p == Op::EXTEND) {
                    ++pc;
                    if (pc >= m.code_.size()) break;  // malformed; defensive only
                    const Instr next = m.code_[pc];
                    const Op realOp = static_cast<Op>(next & 0xFF);
                    const std::uint64_t low = next >> kOperandShift;
                    operand = (operand << 24) | low;
                    out += pad + "  " + fourDigits(pc) + "  L" + std::to_string(m.lines_[pc]) +
                           "  " + opName(realOp) + " " + std::to_string(operand) +
                           commentFor(m, realOp, operand, pc) + "\n";
                    ++pc;
                    continue;
                }
                out += pad + "  " + fourDigits(pc) + "  L" + std::to_string(m.lines_[pc]) +
                       "  " + opName(word0p) + " " + std::to_string(operand) +
                       commentFor(m, word0p, operand, pc) + "\n";
                ++pc;
            }

            for (const auto& block : m.blocks_) {
                out += "\n";
                go(*block, indent + 1);
            }
        };
    go(*this, 0);
    return out;
}

} // namespace protoScala
