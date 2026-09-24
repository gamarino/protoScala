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
        case Op::FORCE_THUNK:   return "FORCE_THUNK";
        case Op::CONCAT:        return "CONCAT";
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
        case Op::MAKE_CLASS:     return "MAKE_CLASS";
        case Op::NEW:            return "NEW";
        case Op::INVOKE_INIT:    return "INVOKE_INIT";
        case Op::STORE_FIELD:    return "STORE_FIELD";
        case Op::SET_FIELD:      return "SET_FIELD";
        case Op::SEND_SUPER:     return "SEND_SUPER";
        case Op::TEST_TYPE:      return "TEST_TYPE";
        case Op::TEST_PROTO:     return "TEST_PROTO";
        case Op::UNAPPLY_FIELDS: return "UNAPPLY_FIELDS";
        case Op::UNCONS:         return "UNCONS";
        case Op::MATCH_ERROR:    return "MATCH_ERROR";
        case Op::CAST_FAIL:      return "CAST_FAIL";
        case Op::MAKE_TUPLE:     return "MAKE_TUPLE";
        case Op::SEND_KW:        return "SEND_KW";
        case Op::NEW_SPREAD:     return "NEW_SPREAD";
        case Op::SEND_APPLY:     return "SEND_APPLY";
        case Op::CALL_KW:        return "CALL_KW";
        case Op::THROW:          return "THROW";
        case Op::RETHROW:        return "RETHROW";
    }
    return "?";
}

const char* typeCodeName(TypeCode code) {
    switch (code) {
        case TypeCode::Integer: return "Int";      case TypeCode::Double: return "Double";
        case TypeCode::Boolean: return "Boolean";  case TypeCode::Char: return "Char";
        case TypeCode::String: return "String";    case TypeCode::Unit: return "Unit";
        case TypeCode::List: return "List";        case TypeCode::ConsList: return "::";
        case TypeCode::Function: return "Function"; case TypeCode::AnyRef: return "AnyRef";
        case TypeCode::AnyVal: return "AnyVal";    case TypeCode::Null: return "Null";
        case TypeCode::NonNull: return "Any";      case TypeCode::Nothing: return "Nothing";
    }
    return "?";
}

std::size_t BytecodeModule::addBlock(std::unique_ptr<BytecodeModule> sub) {
    blocks_.push_back(std::move(sub));
    return blocks_.size() - 1;
}

std::size_t BytecodeModule::addHandler(const Handler& h) {
    handlers_.push_back(h);
    return handlers_.size() - 1;
}

void BytecodeModule::patchHandlerBody(std::size_t index, std::size_t handlerPc) {
    handlers_[index].handlerPc = handlerPc;
}

const BytecodeModule::Handler* BytecodeModule::handlerFor(std::size_t pc) const {
    // Table order is search order: the compiler appends nested try blocks before
    // enclosing ones, and each try's Catch entry before its Finally entry
    // (plan A0-4), so the first containing entry is the innermost applicable one.
    for (const Handler& h : handlers_)
        if (pc >= h.startPc && pc < h.endPc) return &h;
    return nullptr;
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

std::size_t BytecodeModule::addSendSite(const std::string& name, std::uint32_t argc,
                                        const std::string& fallback) {
    const std::string key = name + "/" + std::to_string(argc) + "/" + fallback;
    Const c{ConstKind::SendSite, 0, 0.0, name};
    c.argc = argc;
    c.key = fallback;
    return findOrAdd(sendIndex_, key, consts_, std::move(c));
}

namespace {
std::string joined(const std::vector<std::string>& v) {
    std::string out;
    for (const auto& s : v) out += (out.empty() ? "" : ",") + s;
    return out;
}
} // namespace

std::size_t BytecodeModule::addNames(const std::vector<std::string>& names) {
    Const c{ConstKind::Names, 0, 0.0, {}};
    c.names = names;
    return findOrAdd(namesIndex_, joined(names), consts_, std::move(c));
}

std::size_t BytecodeModule::addClassSpec(const ClassSpecData& spec) {
    Const c{ConstKind::ClassSpec, 0, 0.0, spec.displayName};
    c.argc = spec.parentCount;
    c.names = spec.memberKeys;
    c.fields = spec.fields;
    c.key = spec.key;
    c.flags = spec.flags;
    consts_.push_back(std::move(c));
    return consts_.size() - 1;
}

std::size_t BytecodeModule::addSuperSite(const std::string& name, std::uint32_t argc,
                                         const std::string& ownerKey) {
    Const c{ConstKind::SuperSite, 0, 0.0, name};
    c.argc = argc;
    c.key = ownerKey;
    return findOrAdd(superIndex_, ownerKey + "/" + name + "/" + std::to_string(argc), consts_,
                     std::move(c));
}

std::size_t BytecodeModule::addKwSendSite(const std::string& name, std::uint32_t positional,
                                          const std::vector<std::string>& keywords,
                                          const std::string& fallback) {
    Const c{ConstKind::KwSendSite, 0, 0.0, name};
    c.argc = positional;
    c.names = keywords;
    c.key = fallback;
    return findOrAdd(kwIndex_,
                     name + "/" + std::to_string(positional) + "/" + joined(keywords) + "/" +
                         fallback,
                     consts_, std::move(c));
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

void BytecodeModule::setParamNames(std::vector<std::string> names) {
    paramNames_ = std::move(names);
    defaultBlocks_.assign(paramNames_.size(), kNoDefault);
    minArity_ = arity_;
}

void BytecodeModule::setDefaultBlock(std::size_t param, std::size_t blockIndex) {
    if (defaultBlocks_.size() <= param) defaultBlocks_.resize(param + 1, kNoDefault);
    defaultBlocks_[param] = blockIndex;
    hasDefaults_ = true;
    // The lowest positional count still acceptable: every parameter from the
    // first one carrying a default onwards may be filled by the prologue.
    if (static_cast<int>(param) < minArity_) minArity_ = static_cast<int>(param);
}

void BytecodeModule::linkSymbols(proto::ProtoContext* ctx) {
    // createSymbol takes a C string: a name with an embedded NUL cannot occur —
    // identifiers never contain one.
    auto intern = [ctx](const std::string& s) { return proto::ProtoString::createSymbol(ctx, s.c_str()); };
    for (Const& c : consts_) {
        switch (c.kind) {
            case ConstKind::Symbol: case ConstKind::SendSite: case ConstKind::SuperSite:
            case ConstKind::KwSendSite: case ConstKind::ClassSpec:
                c.symbol = intern(c.sval);
                break;
            default:
                break;
        }
        c.nameSymbols.clear();
        for (const auto& n : c.names) c.nameSymbols.push_back(intern(n));
        c.fieldSymbols.clear();
        for (const auto& f : c.fields) c.fieldSymbols.push_back(intern(f));
        if (!c.key.empty()) c.keySymbol = intern(c.key);
    }
    // Every keyword-argument key in this phase is derived from createSymbol,
    // which INTERNS. ProtoString::fromUTF8String, fromUTF8 and fromStdString
    // return a different pointer for the same text, so a key built from one of
    // them matches nothing — and the failure is SILENT: the argument simply
    // never binds. protoJS hit this class of bug more than once.
    paramSymbols_.clear();
    for (const auto& p : paramNames_) paramSymbols_.push_back(intern(p));
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

std::string nameList(const std::vector<std::string>& v) {
    std::string out = "[";
    for (std::size_t i = 0; i < v.size(); ++i) {
        if (i) out += ",";
        out += v[i];
    }
    return out + "]";
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
        case BytecodeModule::ConstKind::Names:    return nameList(c.names);
        case BytecodeModule::ConstKind::ClassSpec: {
            std::string out = "class " + c.sval + " " + c.key + " parents=" +
                              std::to_string(c.argc) + " members=" + nameList(c.names);
            if (c.flags & BytecodeModule::kClassCase) out += " fields=" + nameList(c.fields);
            return out;
        }
        case BytecodeModule::ConstKind::SuperSite:
            return "super." + c.sval + "/" + std::to_string(c.argc) + " in " + c.key;
        case BytecodeModule::ConstKind::KwSendSite: {
            std::string out = c.sval + "/" + std::to_string(c.argc) + "(";
            for (std::size_t i = 0; i < c.names.size(); ++i) {
                if (i) out += ",";
                out += c.names[i] + "=";
            }
            return out + ")" + (c.key.empty() ? "" : " or " + c.key);
        }
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
        case Op::SEND:
        case Op::SEND_APPLY: {
            const auto& c = m.constAt(operand);
            return " ; " + c.sval + "/" + std::to_string(c.argc) +
                   (c.key.empty() ? "" : " or " + c.key);
        }
        case Op::MAKE_CLASS:
        case Op::NEW:
        case Op::NEW_SPREAD:
        case Op::INVOKE_INIT:
        case Op::SEND_SUPER:
        case Op::UNAPPLY_FIELDS:
        case Op::SEND_KW:
            return " ; " + formatConst(m.constAt(operand));
        case Op::STORE_FIELD:
        case Op::SET_FIELD:
        case Op::TEST_PROTO:
        case Op::CAST_FAIL:
            return " ; " + m.constAt(operand).sval;
        case Op::TEST_TYPE:
            return " ; " + std::string(typeCodeName(static_cast<TypeCode>(operand)));
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
            if (m.method_) out += " method";
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

            // The handler table, in search order (plan A0-4): this listing is
            // how the exception tasks read the table back out of a module.
            for (const Handler& h : m.handlers_)
                out += pad + "  handler [" + std::to_string(h.startPc) + ", " +
                       std::to_string(h.endPc) + ") -> " + std::to_string(h.handlerPc) +
                       "  depth=" + std::to_string(h.stackDepth) +
                       " slot=" + std::to_string(h.slot) + " " +
                       (h.kind == HandlerKind::Catch ? "catch" : "finally") + "\n";

            for (const auto& block : m.blocks_) {
                out += "\n";
                go(*block, indent + 1);
            }
        };
    go(*this, 0);
    return out;
}

} // namespace protoScala
