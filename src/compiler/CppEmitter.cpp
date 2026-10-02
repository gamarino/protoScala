#include "compiler/CppEmitter.h"

#include "compiler/GlobalTable.h"

#include <algorithm>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace protoScala {
namespace {

constexpr int kTerminal = -1000000;   // the instruction does not fall through

// The opcodes this cut emits. Everything else is refused by name, at transpile
// time, with the position the source line table gives -- never mistranslated
// (§D5). The reason each group is out is in `refusalFor`.
bool isSupported(Op op) {
    switch (op) {
        case Op::NOP: case Op::EXTEND:
        case Op::PUSH_CONST: case Op::PUSH_UNIT: case Op::PUSH_NULL:
        case Op::PUSH_TRUE: case Op::PUSH_FALSE:
        case Op::POP: case Op::DUP:
        case Op::PUSH_LOCAL: case Op::STORE_LOCAL:
        case Op::MAKE_CELL: case Op::PUSH_CELL: case Op::STORE_CELL:
        case Op::PUSH_GLOBAL: case Op::STORE_GLOBAL:
        case Op::MAKE_FN: case Op::CALL: case Op::CALL_SPREAD:
        case Op::SEND: case Op::SEND_APPLY: case Op::RETURN:
        case Op::MAKE_LAZY: case Op::FORCE: case Op::FORCE_THUNK:
        case Op::JUMP: case Op::JUMP_IF_FALSE: case Op::JUMP_IF_TRUE: case Op::JUMP_BACK:
        case Op::ADD: case Op::SUB: case Op::MUL:
        case Op::LT: case Op::LE: case Op::GT: case Op::GE:
        case Op::EQ: case Op::NE: case Op::NEG: case Op::NOT:
        case Op::CONCAT:
        case Op::TEST_TYPE: case Op::TEST_PROTO: case Op::UNAPPLY_FIELDS: case Op::UNCONS:
        case Op::MATCH_ERROR: case Op::CAST_FAIL: case Op::MAKE_TUPLE:
        case Op::THROW: case Op::RETHROW:
        case Op::MAKE_CLASS: case Op::NEW: case Op::NEW_SPREAD: case Op::INVOKE_INIT:
        case Op::STORE_FIELD: case Op::STORE_FIELD_IF_NEW: case Op::SET_FIELD:
        case Op::SEND_SUPER:
            return true;
        default:
            return false;
    }
}

std::string refusalFor(Op op) {
    switch (op) {
        case Op::SEND_KW: case Op::CALL_KW:
            return "a named or default argument is not supported by protoscalac yet (D121): "
                   "keyword binding for a transpiled callee is not implemented";
        default:
            return std::string("opcode ") + std::to_string(static_cast<unsigned>(op)) + " (" +
                   opName(op) + ") is not supported by protoscalac";
    }
}

// Can this opcode raise? Only a frame with a protected region asks, and only to decide
// whether to emit a `pc` assignment before it. The list below is the opcodes that CANNOT
// -- pure slot moves, the constant pushes, and an unconditional jump -- so anything added
// to the instruction set later is treated as throwing until someone proves otherwise.
// That is the safe direction: an unnecessary `pc =` is dead, a missing one leaves a STALE
// pc and the handler search reads the wrong region.
//
// Two that look pure and are not: JUMP_IF_FALSE / JUMP_IF_TRUE call `truthy`, which
// raises ClassCastException on a non-Boolean, and JUMP_BACK takes a GC safepoint.
bool canThrow(Op op) {
    switch (op) {
        case Op::NOP: case Op::EXTEND:
        case Op::PUSH_UNIT: case Op::PUSH_NULL: case Op::PUSH_TRUE: case Op::PUSH_FALSE:
        case Op::POP: case Op::DUP:
        case Op::PUSH_LOCAL: case Op::STORE_LOCAL:
        case Op::JUMP:
        case Op::RETURN:
            return false;
        default:
            return true;
    }
}

}  // namespace

CppEmitter::CppEmitter(std::ostream& out, EmitOptions opts) : out_(out), opts_(std::move(opts)) {}

// --- decoding --------------------------------------------------------------

std::vector<CppEmitter::Decoded> CppEmitter::decode(const BytecodeModule& mod) {
    const std::vector<Instr>& code = mod.code();
    std::vector<Decoded> out;
    std::size_t pc = 0;
    while (pc < code.size()) {
        const std::size_t at = pc;
        Instr word = code[pc++];
        Op op = static_cast<Op>(word & 0xFF);
        std::uint64_t operand = word >> kOperandShift;
        if (op == Op::EXTEND) {
            if (pc >= code.size())
                throw std::logic_error("EXTEND at the end of " + mod.name());
            const Instr next = code[pc++];
            operand = (operand << 24) | (next >> kOperandShift);
            op = static_cast<Op>(next & 0xFF);
        }
        out.push_back(Decoded{op, operand, at, pc});
    }
    return out;
}

int CppEmitter::effect(const BytecodeModule& mod, const Decoded& d) {
    const auto n = static_cast<int>(d.operand);
    switch (d.op) {
        case Op::NOP: case Op::EXTEND: return 0;
        case Op::PUSH_CONST: case Op::PUSH_UNIT: case Op::PUSH_NULL: case Op::PUSH_TRUE:
        case Op::PUSH_FALSE: case Op::PUSH_LOCAL: case Op::PUSH_CELL: case Op::PUSH_GLOBAL:
        case Op::DUP:
            return 1;
        case Op::POP: case Op::STORE_LOCAL: case Op::STORE_CELL: case Op::STORE_GLOBAL:
        case Op::STORE_FIELD: case Op::STORE_FIELD_IF_NEW:
        case Op::JUMP_IF_FALSE: case Op::JUMP_IF_TRUE:
        case Op::ADD: case Op::SUB: case Op::MUL: case Op::LT: case Op::LE: case Op::GT:
        case Op::GE: case Op::EQ: case Op::NE:
            return -1;
        case Op::SET_FIELD: return -2;
        case Op::MAKE_CELL: case Op::MAKE_LAZY: case Op::FORCE: case Op::FORCE_THUNK:
        case Op::NEG: case Op::NOT: case Op::TEST_TYPE: case Op::TEST_PROTO:
            return 0;
        case Op::MAKE_FN: return 1 - mod.block(d.operand).captureCount();
        case Op::CALL: return -n;
        case Op::CALL_SPREAD: return -(n + 1);
        case Op::SEND: case Op::SEND_APPLY:
            return -static_cast<int>(mod.constAt(d.operand).argc);
        case Op::SEND_SUPER: return -static_cast<int>(mod.constAt(d.operand).argc);
        case Op::CONCAT: case Op::MAKE_TUPLE: return 1 - n;
        case Op::UNCONS: return 1;
        case Op::UNAPPLY_FIELDS:
            return static_cast<int>(mod.constAt(d.operand).names.size()) - 1;
        case Op::MAKE_CLASS: {
            const BytecodeModule::Const& c = mod.constAt(d.operand);
            return 1 - static_cast<int>(c.argc + c.names.size());
        }
        case Op::NEW: {
            const BytecodeModule::Const& c = mod.constAt(d.operand);
            return -static_cast<int>(c.argc + c.names.size());
        }
        case Op::NEW_SPREAD: return -static_cast<int>(mod.constAt(d.operand).argc) - 1;
        case Op::INVOKE_INIT: return -static_cast<int>(mod.constAt(d.operand).argc) - 1;
        case Op::SEND_KW: case Op::CALL_KW: {
            const BytecodeModule::Const& c = mod.constAt(d.operand);
            return -static_cast<int>(c.argc + c.names.size());
        }
        case Op::JUMP: case Op::JUMP_BACK: return 0;
        case Op::RETURN: case Op::MATCH_ERROR: case Op::CAST_FAIL: case Op::THROW:
        case Op::RETHROW:
            return kTerminal;
    }
    return kTerminal;
}

std::set<std::size_t> CppEmitter::labelTargets(const BytecodeModule& mod) {
    std::set<std::size_t> out;
    for (const Decoded& d : decode(mod)) {
        switch (d.op) {
            case Op::JUMP: case Op::JUMP_IF_FALSE: case Op::JUMP_IF_TRUE:
                out.insert(d.next + d.operand);
                break;
            case Op::JUMP_BACK:
                out.insert(d.next - d.operand);
                break;
            default: break;
        }
    }
    for (const BytecodeModule::Handler& h : mod.handlers()) out.insert(h.handlerPc);
    return out;
}

std::vector<int> CppEmitter::depths(const BytecodeModule& mod) {
    const std::vector<Decoded> ins = decode(mod);
    std::map<std::size_t, std::size_t> indexOf;
    for (std::size_t i = 0; i < ins.size(); ++i) indexOf[ins[i].pc] = i;

    constexpr int kUnset = -1;
    std::vector<int> depth(ins.size(), kUnset);
    std::vector<std::size_t> work;
    if (!ins.empty()) { depth[0] = 0; work.push_back(0); }
    // The handler bodies are reached from the retry loop, not by a jump, at the
    // try's entry depth. They are seeded here so a handler body's depth is known
    // even though nothing branches to it.
    for (const BytecodeModule::Handler& h : mod.handlers()) {
        auto it = indexOf.find(h.handlerPc);
        if (it == indexOf.end()) continue;
        if (depth[it->second] == kUnset) {
            depth[it->second] = h.stackDepth;
            work.push_back(it->second);
        }
    }

    auto reach = [&](std::size_t targetPc, int d, const char* how) {
        auto it = indexOf.find(targetPc);
        if (it == indexOf.end())
            throw std::logic_error("a " + std::string(how) + " in " + mod.name() +
                                   " targets word " + std::to_string(targetPc) +
                                   ", which is not an instruction boundary");
        const std::size_t i = it->second;
        if (depth[i] == kUnset) { depth[i] = d; work.push_back(i); return; }
        if (depth[i] != d)
            throw std::logic_error("two paths reach word " + std::to_string(targetPc) + " of " +
                                   mod.name() + " at operand-stack depths " +
                                   std::to_string(depth[i]) + " and " + std::to_string(d));
    };

    while (!work.empty()) {
        const std::size_t i = work.back();
        work.pop_back();
        const Decoded& d = ins[i];
        const int e = effect(mod, d);
        if (e == kTerminal) continue;
        const int after = depth[i] + e;
        if (after < 0)
            throw std::logic_error("the operand stack of " + mod.name() + " goes negative at word " +
                                   std::to_string(d.pc));
        switch (d.op) {
            case Op::JUMP:
                reach(d.next + d.operand, after, "JUMP");
                break;
            case Op::JUMP_BACK:
                reach(d.next - d.operand, after, "JUMP_BACK");
                break;
            case Op::JUMP_IF_FALSE: case Op::JUMP_IF_TRUE:
                reach(d.next + d.operand, after, "conditional jump");
                reach(d.next, after, "fall-through");
                break;
            default:
                if (d.next < mod.code().size()) reach(d.next, after, "fall-through");
                break;
        }
    }
    // An instruction nothing reaches is dead code the compiler emitted; it is
    // given depth 0 so the emitter can still write it, and it cannot matter.
    for (int& x : depth) if (x == kUnset) x = 0;
    return depth;
}

// --- check() ---------------------------------------------------------------

void CppEmitter::collect(const BytecodeModule& mod, std::vector<Refusal>& out) const {
    if (mod.hasDefaults())
        // A default value is bound in the CALLEE, by execute()'s prologue, which a
        // native block does not run: the generated frame has no
        // bindKeywordsAndDefaults. A unit whose def has a default would therefore
        // silently see an unbound parameter -- a wrong answer, not an error -- so it
        // is refused (§D5).
        out.push_back(Refusal{"a parameter with a default value is not supported by protoscalac "
                              "yet (D121): defaults are bound by the callee's prologue, which a "
                              "transpiled frame does not run",
                              mod.lineAt(0)});
    for (const Decoded& d : decode(mod)) {
        if (d.op == Op::SEND || d.op == Op::SEND_APPLY) {
            // `await` is detected by SEND-site name, which is an
            // over-approximation: a user method named `await` is refused too.
            // That is the safe direction (D113).
            const BytecodeModule::Const& c = mod.constAt(d.operand);
            if (c.sval == "await")
                out.push_back(Refusal{"await is not supported in a transpiled module (D113): "
                                      "cooperative suspension snapshots a bytecode frame, and a "
                                      "transpiled frame has no ip to record",
                                      mod.lineAt(d.pc)});
            continue;
        }
        if (d.op == Op::NEW && !mod.constAt(d.operand).names.empty()) {
            // `new C(x = 1)`: the constructor's own prologue binds the keywords, and a
            // transpiled frame does not run that prologue (D121).
            out.push_back(Refusal{"a named argument in `new` is not supported by protoscalac yet "
                                  "(D121): keywords are bound by the callee's prologue, which a "
                                  "transpiled frame does not run",
                                  mod.lineAt(d.pc)});
            continue;
        }
        if (!isSupported(d.op)) out.push_back(Refusal{refusalFor(d.op), mod.lineAt(d.pc)});
    }
    for (std::size_t i = 0; i < mod.blockCount(); ++i) collect(mod.block(i), out);
}

std::vector<Refusal> CppEmitter::check(const CompiledUnit& unit) const {
    std::vector<Refusal> out;
    collect(*unit.module, out);
    // Stable and de-duplicated: one message per (line, text), so a loop that
    // refuses the same opcode fifty times reports it once.
    std::sort(out.begin(), out.end(), [](const Refusal& a, const Refusal& b) {
        return a.line != b.line ? a.line < b.line : a.message < b.message;
    });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const Refusal& a, const Refusal& b) {
                              return a.line == b.line && a.message == b.message;
                          }),
              out.end());
    return out;
}

std::vector<std::string> CppEmitter::purityReport(const CompiledUnit& unit) {
    // §D2's four conditions, in order, naming the FIRST thing that makes the unit
    // need protoScala. The emitted opcode set must be a subset of the pure list,
    // it must reference no global, allocate no ClassSpec, and its pool must hold
    // no name constant.
    std::vector<std::string> out;
    std::vector<const BytecodeModule*> todo{unit.module.get()};
    while (!todo.empty()) {
        const BytecodeModule& mod = *todo.back();
        todo.pop_back();
        for (std::size_t i = 0; i < mod.blockCount(); ++i) todo.push_back(&mod.block(i));
        for (const Decoded& d : decode(mod)) {
            std::string why;
            switch (d.op) {
                case Op::ADD: case Op::SUB: case Op::MUL: case Op::NEG:
                    why = "protoScala numeric semantics (Int = Long = BigInt, D1)";
                    break;
                case Op::LT: case Op::LE: case Op::GT: case Op::GE: case Op::EQ: case Op::NE:
                    why = "protoScala comparison semantics (D1/D2)";
                    break;
                case Op::PUSH_GLOBAL: case Op::STORE_GLOBAL:
                    why = "a prelude or module global";
                    break;
                case Op::MAKE_CLASS:
                    why = "a linearized class";
                    break;
                case Op::SEND: case Op::SEND_APPLY: case Op::SEND_KW: case Op::SEND_SUPER:
                    why = "a dynamic send through the prototype chain";
                    break;
                case Op::CONCAT: case Op::MAKE_TUPLE: case Op::UNCONS: case Op::UNAPPLY_FIELDS:
                case Op::MATCH_ERROR: case Op::CAST_FAIL: case Op::THROW: case Op::RETHROW:
                case Op::MAKE_LAZY: case Op::FORCE: case Op::FORCE_THUNK:
                case Op::NEW: case Op::NEW_SPREAD: case Op::INVOKE_INIT:
                case Op::CALL: case Op::CALL_SPREAD: case Op::CALL_KW: case Op::MAKE_FN:
                case Op::MAKE_CELL: case Op::PUSH_CELL: case Op::STORE_CELL:
                case Op::STORE_FIELD: case Op::STORE_FIELD_IF_NEW: case Op::SET_FIELD:
                case Op::TEST_TYPE: case Op::TEST_PROTO:
                    why = "a protoScala runtime operation";
                    break;
                default:
                    break;
            }
            if (why.empty()) continue;
            std::ostringstream s;
            s << "  " << opName(d.op);
            if (d.op == Op::PUSH_GLOBAL || d.op == Op::STORE_GLOBAL ||
                d.op == Op::SEND || d.op == Op::SEND_APPLY)
                s << ' ' << GlobalTable::nameOfKey(mod.constAt(d.operand).sval);
            s << " at line " << mod.lineAt(d.pc) << "    " << why;
            out.push_back(s.str());
        }
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

// --- emission --------------------------------------------------------------

void CppEmitter::line(const BytecodeModule& mod, std::size_t pc) {
    const int l = mod.lineAt(pc);
    if (l <= 0 || l == lastLine_) return;
    lastLine_ = l;
    out_ << "#line " << l << ' ' << tables::quoted(opts_.sourcePath) << '\n';
}

void CppEmitter::emitTables(std::size_t index) {
    const tables::FlatBlock& blk = flat_[index];
    const BytecodeModule& mod = *blk.mod;
    tables::StringPool pool;
    tables::emitConsts(out_, blk.cppName + "_consts", mod, pool);
    tables::emitHandlers(out_, blk.cppName + "_handlers", mod);
    tables::emitStrings(out_, blk.cppName + "_strings", pool);
    tables::emitSymbolArrays(out_, blk.cppName, mod.constCount(), pool.size());
    std::vector<std::size_t> children;
    {
        std::size_t child = index + 1;
        for (std::size_t b = 0; b < mod.blockCount(); ++b) {
            children.push_back(child);
            std::size_t k = child + 1;
            while (k < flat_.size() && flat_[k].parent >= static_cast<int>(child)) ++k;
            child = k;
        }
    }
    tables::emitBlockRec(out_, blk, index, children, pool.size());
    out_ << '\n';
}

bool CppEmitter::emitBlock(std::size_t index, const GlobalTable&) {
    const tables::FlatBlock& fb = flat_[index];
    const BytecodeModule& mod = *fb.mod;
    const std::string n = fb.cppName;
    const std::vector<Decoded> ins = decode(mod);
    const std::set<std::size_t> labels = labelTargets(mod);
    const std::vector<int> depth = depths(mod);
    // Step 5's assertion. `depths()` seeds each handler body at its entry's stackDepth
    // and throws when two paths disagree, so this is the second half: that the depth the
    // emitter will USE at handlerPc is the one `enterHandler` would set. A disagreement
    // is a generator bug that would otherwise surface as a wrong value, not an error.
    for (const BytecodeModule::Handler& h : mod.handlers()) {
        std::size_t at = ins.size();
        for (std::size_t i = 0; i < ins.size(); ++i)
            if (ins[i].pc == h.handlerPc) { at = i; break; }
        if (at == ins.size())
            throw std::logic_error("a handler of " + mod.name() + " enters at word " +
                                   std::to_string(h.handlerPc) +
                                   ", which is not an instruction boundary");
        if (depth[at] != h.stackDepth)
            throw std::logic_error("the handler entering " + mod.name() + " at word " +
                                   std::to_string(h.handlerPc) + " expects operand-stack depth " +
                                   std::to_string(h.stackDepth) + ", but the emitter reaches it at " +
                                   std::to_string(depth[at]));
    }

    // Two functions per block: a thunk that is the proto::ProtoMethod stored in
    // the tables and reached by every caller, and a body that carries the code.
    // The thunk is the ONLY place the D6 site-2 boundary appears, and it is one
    // line, because a constructor cannot wrap the code that follows it.
    out_ << "static const proto::ProtoObject* " << n
         << "(proto::ProtoContext* ctx, const proto::ProtoObject* self,\n"
         << "        const proto::ParentLink* pl, const proto::ProtoList* args,\n"
         << "        const proto::ProtoSparseList* kwargs) {\n"
         << "    return gen::enterMethod(&" << n << "_body, ctx, self, pl, args, kwargs);\n"
         << "}\n\n";

    lastLine_ = -1;
    line(mod, 0);
    out_ << "static const proto::ProtoObject* " << n
         << "_body(proto::ProtoContext* ctx, const proto::ProtoObject* self,\n"
         << "        const proto::ParentLink*, const proto::ProtoList* args,\n"
         << "        const proto::ProtoSparseList* kwargs) {\n"
         << "    gen::Frame F(ctx, " << n << "_rec, self, args, kwargs);\n"
         << "    proto::ProtoContext* C = F.ctx();\n"
         << "    const proto::ProtoObject** S = F.slots();\n"
         << "    const unsigned B = F.stackBase();\n"
         << "    (void)C; (void)S; (void)B;\n";

    const std::string rec = n + "_rec";
    // The frame's retry loop, and ONE per block rather than one per `try`. The shape is
    // `ExecutionEngine::runFrame`'s, and the reason it is that shape is load-bearing: a
    // handler body must run with NO live C++ handler, so that it suspends like any other
    // code and so that the frame can catch a SECOND exception -- raised by the handler
    // body itself, by a non-matching cascade's RETHROW, or by a `finally`. Entering the
    // handler from inside the catch would abandon the loop and never consult the table
    // again.
    //
    // A block with no protected region emits none of this: no `pc`, no loop, no `catch`.
    // That keeps the zero-cost property, and `tests/cli/transpiler-cli.sh` asserts that
    // such a file contains no `catch` at all.
    const bool guarded = !mod.handlers().empty();
    if (guarded) {
        // The switch is INSIDE the try, deliberately: C++ forbids jumping into a try
        // block and permits jumping within one, so the dispatch and every label it
        // reaches must share the try -- which is exactly what re-protects the handler
        // body under the same table.
        out_ << "    std::size_t pc = 0;\n"
             << "    std::size_t resumePc = kEntry;\n"
             << "    for (;;) {\n"
             << "      try {\n"
             << "        switch (resumePc) {\n"
             << "            case kEntry: goto L_entry;\n";
        std::set<std::size_t> seen;
        for (const BytecodeModule::Handler& h : mod.handlers())
            if (seen.insert(h.handlerPc).second)
                out_ << "            case " << h.handlerPc << ": goto L" << h.handlerPc << ";\n";
        out_ << "            default: throw std::logic_error(\"" << n
             << ": unreachable resume pc\");\n"
             << "        }\n"
             << "      L_entry:\n";
    }
    for (std::size_t i = 0; i < ins.size(); ++i) {
        const Decoded& d = ins[i];
        const int dep = depth[i];
        // `S[B + k]` is the operand stack; `S[k]` below B is a parameter or a
        // local. Nothing is ever held in a C++ local across a call.
        auto st = [&](int k) { return "S[B + " + std::to_string(k) + "]"; };
        if (labels.count(d.pc)) out_ << "  L" << d.pc << ":\n";
        line(mod, d.pc);
        // The handler search reads `pc`, so it must hold the word index of the
        // instruction in flight -- and only for an instruction that can raise. A `pc`
        // assignment before an opcode that cannot throw is dead, and is not emitted.
        if (guarded && canThrow(d.op)) out_ << "    pc = " << d.pc << ";\n";
        out_ << "    ";
        switch (d.op) {
            case Op::NOP: case Op::EXTEND:
                out_ << ";";
                break;
            case Op::PUSH_CONST:
                out_ << st(dep) << " = gen::constant(C, " << rec << ", " << d.operand << ");";
                break;
            case Op::PUSH_UNIT:  out_ << st(dep) << " = gen::unitValue(C);"; break;
            case Op::PUSH_NULL:  out_ << st(dep) << " = PROTO_NONE;"; break;
            case Op::PUSH_TRUE:  out_ << st(dep) << " = PROTO_TRUE;"; break;
            case Op::PUSH_FALSE: out_ << st(dep) << " = PROTO_FALSE;"; break;
            case Op::POP:        out_ << ";  // POP"; break;
            case Op::DUP:        out_ << st(dep) << " = " << st(dep - 1) << ";"; break;
            case Op::PUSH_LOCAL:
                out_ << st(dep) << " = S[" << d.operand << "];";
                break;
            case Op::STORE_LOCAL:
                out_ << "S[" << d.operand << "] = " << st(dep - 1) << ";";
                break;
            case Op::MAKE_CELL:
                out_ << "S[" << d.operand << "] = gen::makeCell(C);";
                break;
            case Op::PUSH_CELL:
                out_ << st(dep) << " = gen::cellGet(C, S[" << d.operand << "]);";
                break;
            case Op::STORE_CELL:
                out_ << "S[" << d.operand << "] = gen::cellSet(C, S[" << d.operand << "], "
                     << st(dep - 1) << ");";
                break;
            case Op::PUSH_GLOBAL:
                out_ << st(dep) << " = gen::pushGlobal(C, " << rec << ", " << d.operand << ");";
                break;
            case Op::STORE_GLOBAL:
                out_ << "gen::storeGlobal(C, " << rec << ", " << d.operand << ", " << st(dep - 1)
                     << ");";
                break;
            case Op::MAKE_FN: {
                const int nc = mod.block(d.operand).captureCount();
                out_ << st(dep - nc) << " = gen::makeFn(C, " << rec << ", " << d.operand << ", "
                     << (nc ? "&" + st(dep - nc) : "nullptr") << ", " << nc << ");";
                break;
            }
            case Op::CALL: {
                const int nn = static_cast<int>(d.operand);
                out_ << st(dep - nn - 1) << " = gen::call(C, " << st(dep - nn - 1) << ", &"
                     << st(dep - nn) << ", " << nn << ");";
                break;
            }
            case Op::CALL_SPREAD: {
                const int nn = static_cast<int>(d.operand);
                out_ << st(dep - nn - 2) << " = gen::callSpread(C, " << st(dep - nn - 2) << ", &"
                     << st(dep - nn - 1) << ", " << nn << ", " << st(dep - 1) << ");";
                break;
            }
            case Op::SEND: case Op::SEND_APPLY: {
                const int argc = static_cast<int>(mod.constAt(d.operand).argc);
                out_ << st(dep - argc - 1) << " = gen::"
                     << (d.op == Op::SEND ? "send" : "sendApply") << "(C, " << rec << ", "
                     << d.operand << ", &" << st(dep - argc - 1) << ", " << argc << ");";
                break;
            }
            case Op::RETURN:
                out_ << "return F.finish(" << st(dep - 1) << ");";
                break;
            case Op::MAKE_LAZY:
                out_ << st(dep - 1) << " = gen::makeLazy(C, " << st(dep - 1) << ");";
                break;
            case Op::FORCE:
                out_ << st(dep - 1) << " = gen::force(C, " << st(dep - 1) << ");";
                break;
            case Op::FORCE_THUNK:
                out_ << st(dep - 1) << " = gen::forceThunk(C, " << st(dep - 1) << ");";
                break;
            case Op::JUMP:
                out_ << "goto L" << (d.next + d.operand) << ";";
                break;
            case Op::JUMP_IF_FALSE:
                out_ << "if (!gen::truthy(C, " << st(dep - 1) << ")) goto L"
                     << (d.next + d.operand) << ";";
                break;
            case Op::JUMP_IF_TRUE:
                out_ << "if (gen::truthy(C, " << st(dep - 1) << ")) goto L"
                     << (d.next + d.operand) << ";";
                break;
            case Op::JUMP_BACK:
                // The safepoint is not optional: ProtoContext::safepoint() is the
                // only place a context's young chain reaches the collector, an
                // unsubmitted chain is live by construction, and a generated loop
                // without it reclaims nothing while looking healthy (P4 rule 1).
                out_ << "gen::safepoint(C); goto L" << (d.next - d.operand) << ";";
                break;
            case Op::ADD: case Op::SUB: case Op::MUL:
            case Op::LT: case Op::LE: case Op::GT: case Op::GE:
            case Op::EQ: case Op::NE: {
                const char* f = d.op == Op::ADD ? "add" : d.op == Op::SUB ? "sub"
                              : d.op == Op::MUL ? "mul" : d.op == Op::LT ? "lt"
                              : d.op == Op::LE  ? "le"  : d.op == Op::GT ? "gt"
                              : d.op == Op::GE  ? "ge"  : d.op == Op::EQ ? "eq" : "ne";
                out_ << st(dep - 2) << " = gen::" << f << "(C, " << st(dep - 2) << ", "
                     << st(dep - 1) << ");";
                break;
            }
            case Op::NEG:
                out_ << st(dep - 1) << " = gen::neg(C, " << st(dep - 1) << ");";
                break;
            case Op::NOT:
                out_ << st(dep - 1) << " = gen::notOp(C, " << st(dep - 1) << ");";
                break;
            case Op::CONCAT: {
                const int nn = static_cast<int>(d.operand);
                // `&S[B + d - n]` is a run of traced slots, not a C++ array of
                // pointers, which is what keeps this P1-safe.
                out_ << st(dep - nn) << " = gen::concat(C, &" << st(dep - nn) << ", " << nn << ");";
                break;
            }
            case Op::TEST_TYPE:
                out_ << st(dep - 1) << " = gen::testType(C, " << st(dep - 1) << ", " << d.operand
                     << ") ? PROTO_TRUE : PROTO_FALSE;";
                break;
            case Op::TEST_PROTO:
                out_ << st(dep - 1) << " = gen::testProto(C, " << rec << ", " << d.operand << ", "
                     << st(dep - 1) << ") ? PROTO_TRUE : PROTO_FALSE;";
                break;
            case Op::UNAPPLY_FIELDS:
                out_ << "gen::unapplyFields(C, " << rec << ", " << d.operand << ", "
                     << st(dep - 1) << ", &" << st(dep - 1) << ");";
                break;
            case Op::UNCONS:
                out_ << "gen::uncons(C, " << st(dep - 1) << ", &" << st(dep - 1) << ", &"
                     << st(dep) << ");";
                break;
            case Op::MATCH_ERROR:
                out_ << "gen::matchError(C, " << st(dep - 1) << ");";
                break;
            case Op::CAST_FAIL:
                out_ << "gen::castFail(C, " << rec << ", " << d.operand << ", " << st(dep - 1)
                     << ");";
                break;
            case Op::MAKE_TUPLE: {
                const int nn = static_cast<int>(d.operand);
                out_ << st(dep - nn) << " = gen::makeTuple(C, &" << st(dep - nn) << ", " << nn
                     << ");";
                break;
            }
            case Op::THROW:
                out_ << "gen::throwValue(C, " << st(dep - 1) << ");";
                break;
            case Op::RETHROW:
                // A non-matching `catch` cascade, and the end of every `finally`. The
                // operand is a LOCAL slot -- the one the handler's entry named -- not a
                // stack offset, so it is `S[n]` and not `st(...)`.
                out_ << "gen::rethrow(C, " << rec << ", S[" << d.operand << "]);";
                break;
            case Op::MAKE_CLASS: {
                const BytecodeModule::Const& c = mod.constAt(d.operand);
                const int n = static_cast<int>(c.argc + c.names.size());
                out_ << st(dep - n) << " = gen::makeClass(C, " << rec << ", " << d.operand << ", &"
                     << st(dep - n) << ");";
                break;
            }
            case Op::NEW: {
                const BytecodeModule::Const& c = mod.constAt(d.operand);
                const int n = static_cast<int>(c.argc + c.names.size()) + 1;  // [cls a.. k..]
                out_ << st(dep - n) << " = gen::construct(C, " << rec << ", " << d.operand << ", &"
                     << st(dep - n) << ");";
                break;
            }
            case Op::NEW_SPREAD: {
                const int n = static_cast<int>(mod.constAt(d.operand).argc) + 2;  // [cls a.. list]
                out_ << st(dep - n) << " = gen::constructSpread(C, " << rec << ", " << d.operand
                     << ", &" << st(dep - n) << ", " << st(dep - 1) << ");";
                break;
            }
            case Op::INVOKE_INIT: {
                const int n = static_cast<int>(mod.constAt(d.operand).argc) + 2;  // [cls this a..]
                out_ << st(dep - n) << " = gen::invokeInit(C, " << rec << ", " << d.operand
                     << ", &" << st(dep - n) << ");";
                break;
            }
            case Op::STORE_FIELD:
                // In a constructor `this` is slot 0, and the store yields a new `this`.
                out_ << "S[0] = gen::storeField(C, " << rec << ", " << d.operand << ", S[0], "
                     << st(dep - 1) << ");";
                break;
            case Op::STORE_FIELD_IF_NEW:
                out_ << "S[0] = gen::storeFieldIfNew(C, " << rec << ", " << d.operand << ", S[0], "
                     << st(dep - 1) << ");";
                break;
            case Op::SET_FIELD:
                out_ << "gen::setField(C, " << rec << ", " << d.operand << ", " << st(dep - 2)
                     << ", " << st(dep - 1) << ");";
                break;
            case Op::SEND_SUPER: {
                const int argc = static_cast<int>(mod.constAt(d.operand).argc);
                out_ << st(dep - argc - 1) << " = gen::sendSuper(C, " << rec << ", " << d.operand
                     << ", &" << st(dep - argc - 1) << ");";
                break;
            }
            default:
                // check() refused it, so reaching here is a generator defect.
                throw std::logic_error("CppEmitter: opcode " + std::string(opName(d.op)) +
                                       " passed check() but has no emitter arm");
        }
        out_ << "   // " << d.pc << ' ' << opName(d.op) << " d=" << dep << '\n';
    }
    // The compiler always ends a block with RETURN, but a block whose last
    // instruction is a jump would otherwise fall off the end of a function that
    // must return a value. `unitValue` is what `PUSH_UNIT; RETURN` would have
    // produced, and reaching it is not an error.
    out_ << "    return F.finish(gen::unitValue(C));\n";
    if (guarded)
        // handleCaught rethrows when no entry covers `pc`, so reaching `continue` means
        // an entry matched and its body is now re-protected by the same table.
        out_ << "      } catch (...) {\n"
             << "        const gen::HandlerRec* h =\n"
             << "            gen::handleCaught(C, " << rec << ", pc, S, F.pendingSlot());\n"
             << "        resumePc = h->handlerPc;\n"
             << "        continue;\n"
             << "      }\n"
             << "    }\n";
    out_ << "}\n\n";
    return true;
}

std::vector<CppEmitter::Export> CppEmitter::exportsOf(const BytecodeModule& root) const {
    std::vector<Export> out;
    const std::vector<Decoded> code = decode(root);
    for (std::size_t i = 0; i + 1 < code.size(); ++i) {
        if (code[i].op != Op::MAKE_FN || code[i + 1].op != Op::STORE_GLOBAL) continue;
        const BytecodeModule& fn = root.block(code[i].operand);
        if (fn.captureCount() != 0) continue;   // see the declaration
        // The flat index is what linkModule and the thunk table are keyed by, and
        // flatten() has already checked that the order round-trips.
        std::size_t flatIdx = 0;
        for (; flatIdx < flat_.size(); ++flatIdx)
            if (flat_[flatIdx].mod == &fn) break;
        if (flatIdx == flat_.size())
            throw std::logic_error("CppEmitter: a MAKE_FN target is not in the flattened tree");
        out.push_back({GlobalTable::nameOfKey(root.constAt(code[i + 1].operand).sval), flatIdx});
    }
    return out;
}

bool CppEmitter::emit(const CompiledUnit& unit, const GlobalTable& globals) {
    flat_ = tables::flatten(*unit.module);

    out_ << "// Generated by protoscalac from " << opts_.sourcePath << "; do not edit.\n"
         << "//\n"
         << "// Every value lives in a gen::Frame slot: no C++ local here holds a\n"
         << "// const proto::ProtoObject* across a call, which is P1 made structural\n"
         << "// rather than remembered. The operand stack is the frame's traced\n"
         << "// automatic locals, indexed by a constant the emitter knew.\n"
         << "//\n"
         << "// This file contains no exception handler of its own. The two boundary\n"
         << "// sites live in libprotoScala.so (gen::runModuleBody and\n"
         << "// gen::enterMethod), so a generator bug cannot drop a clause and\n"
         << "// retire D74 in a file no human wrote. tests/cli/transpiler-cli.sh\n"
         << "// greps the generated output for one, which is why this comment does\n"
         << "// not spell the keyword.\n"
         << "#include <protoScala/GeneratedModule.h>\n"
         << "#include <protoCore.h>\n\n"
         << "#include <cstddef>\n"
         << "#include <stdexcept>\n\n"
         << "namespace gen = protoScala::gen;\n\n"
         // The retry loop's "run the body from the top" sentinel. A distinct value
         // rather than 0, because 0 is a valid handlerPc and `case 0:` would then
         // appear twice.
         << "static constexpr std::size_t kEntry = static_cast<std::size_t>(-1);\n"
         << "[[maybe_unused]] static const std::size_t kEntryUsed = kEntry;\n\n";

    // Forward declarations first: a block's record names its own thunk and its
    // children's, and a body names its own record.
    for (const tables::FlatBlock& b : flat_) {
        out_ << "static const proto::ProtoObject* " << b.cppName
             << "(proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,\n"
             << "        const proto::ProtoList*, const proto::ProtoSparseList*);\n"
             << "static const proto::ProtoObject* " << b.cppName
             << "_body(proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,\n"
             << "        const proto::ProtoList*, const proto::ProtoSparseList*);\n";
    }
    out_ << '\n';

    // Tables in REVERSE flatten order, so a child's record is defined before the
    // parent's `blocks[]` names it. flatten is depth-first with children after
    // their parent, so reversing puts every child first; a forward declaration
    // would not work, because `static const BlockRec x;` is a definition.
    for (std::size_t i = flat_.size(); i-- > 0;) emitTables(i);

    out_ << "static const protoScala::gen::BlockRec* const kAllBlocks[] = {";
    for (const tables::FlatBlock& b : flat_) out_ << " &" << b.cppName << "_rec,";
    out_ << " };\n"
         << "static const std::size_t kAllBlockCount = " << flat_.size() << ";\n\n";

    // The exports: the module object publishes each of these as a bare
    // proto::ProtoMethod cell, which is what a foreign runtime holding nothing but
    // protoCore can call. The thunk is the entry, not the body, because the thunk is
    // where the D6 boundary and the frame prologue are.
    // A zero-length array is ill-formed in C++, and a unit with no top-level def is
    // ordinary (a script), so the empty case is a null pointer rather than an array.
    const std::vector<Export> exports = exportsOf(*unit.module);
    if (exports.empty()) {
        out_ << "static const protoScala::gen::ExportRec* const kExports = nullptr;\n";
    } else {
        out_ << "static const protoScala::gen::ExportRec kExports[] = {";
        for (const Export& e : exports)
            out_ << "\n    { " << tables::quoted(e.name) << ", &" << flat_[e.block].cppName
                 << " },";
        out_ << "\n};\n";
    }
    out_ << "static const std::size_t kExportCount = " << exports.size() << ";\n\n";

    // The extern "C" entry points come BEFORE the bodies, and deliberately: the
    // bodies carry `#line` directives pointing into the .scala file, and a
    // `#line` cannot be un-set (a directive of 0 is not valid). Emitting the
    // entry points first means a diagnostic about one of them points at the
    // generated file, where it belongs, with no reset needed.
    out_ << "PROTOSCALA_MODULE_EXPORT const char* proto_module_version_v1() { return "
         << tables::quoted(opts_.moduleVersion) << "; }\n"
         << "PROTOSCALA_MODULE_EXPORT const char* proto_module_language_v1() { return \"protoScala\"; }\n\n"
         << "PROTOSCALA_MODULE_EXPORT void* proto_module_init() {\n"
         << "    proto::ProtoContext* ctx = gen::currentContext(\"proto_module_init\");\n"
         << "    gen::linkModule(ctx, kAllBlocks, kAllBlockCount);\n"
         << "    return const_cast<void*>(static_cast<const void*>(\n"
         << "        gen::runModuleBody(ctx, &blk0, " << tables::quoted(opts_.logicalPath) << ", "
         << tables::quoted(opts_.moduleVersion) << ", kExports, kExportCount)));\n"
         << "}\n";
    if (opts_.asScript) {
        out_ << "\nPROTOSCALA_MODULE_EXPORT int proto_module_main(int argc, char** argv) {\n"
             << "    proto::ProtoContext* ctx = gen::currentContext(\"proto_module_main\");\n";
        if (unit.mainKey.empty() && !opts_.appKey.empty()) {
            // `object Main extends App`: the program is the object's initialisation,
            // so the entry point forces the global rather than calling a method
            // (D104). Without this the module loaded, ran its top level and printed
            // nothing -- a wrong answer, which the differential harness caught on four
            // fixtures.
            out_ << "    (void)argc; (void)argv;\n"
                 << "    return gen::runApp(ctx, " << tables::quoted(opts_.appKey) << ");\n";
        } else {
            out_ << "    return gen::runMain(ctx, " << tables::quoted(unit.mainKey) << ", "
                 << (unit.mainTakesArgs ? "true" : "false") << ", argc, argv);\n";
        }
        out_ << "}\n";
    }
    out_ << '\n';

    for (std::size_t i = 0; i < flat_.size(); ++i)
        if (!emitBlock(i, globals)) return false;
    return out_.good();
}

}  // namespace protoScala
