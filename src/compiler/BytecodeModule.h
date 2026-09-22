/*
 * BytecodeModule — one compiled function body (DESIGN §3.5): code words and a
 * parallel source-line table, a constant pool de-duplicated by kind, nested
 * function bodies (blocks), capture specs and frame metadata.
 *
 * Ownership: a module and its blocks are C++ objects owned by the session
 * (std::unique_ptr). A function object points at its module through a
 * SmallInteger holding the address (MAKE_FN), exactly as protoClojure's
 * wrappers do (src/runtime/ExecutionEngine.cpp:895-910); the session keeps
 * every module alive until the ProtoSpace is destroyed.
 *
 * P1 boundary: the only ProtoObject pointers a module holds are interned
 * symbols (Const::symbol), filled by linkSymbols. createSymbol symbols are
 * strong and never collected, so no GC reference from here is needed; a
 * module therefore runs only in the ProtoSpace it was linked against.
 */
#pragma once
#include "compiler/Opcodes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace proto {
class ProtoContext;
class ProtoString;
}

namespace protoScala {

class BytecodeModule {
public:
    enum class ConstKind : uint8_t { Int, BigInt, Double, String, Char, Symbol, SendSite };

    struct Const {
        ConstKind kind;
        long long ival = 0;         // Int; Char (code point)
        double dval = 0.0;          // Double
        std::string sval;           // String bytes; BigInt digits; Symbol/SendSite name
        int base = 10;              // BigInt
        std::uint32_t argc = 0;     // SendSite
        const proto::ProtoString* symbol = nullptr;  // Symbol/SendSite, after linkSymbols
    };

    struct CaptureSpec {
        int parentSlot;  // slot read in the enclosing frame at MAKE_FN
        int localSlot;   // slot written in this frame on entry
    };

    std::size_t addInt(long long v);
    std::size_t addBigInt(const std::string& digits, int base);
    std::size_t addDouble(double v);
    std::size_t addString(const std::string& s);
    std::size_t addChar(char32_t c);
    std::size_t addSymbol(const std::string& name);
    std::size_t addSendSite(const std::string& name, std::uint32_t argc);

    // Emits `op operand` (with an EXTEND prefix when operand > kMaxOperand) and
    // returns the position of the `op` word. Throws std::length_error beyond
    // kMaxExtendedOperand.
    std::size_t emit(Op op, std::uint64_t operand, int line);
    // A forward jump with a placeholder operand; never extended.
    std::size_t emitJump(Op op, int line);
    // Sets the jump at `jumpAt` to land on `target` (target >= jumpAt + 1).
    void patchJumpTo(std::size_t jumpAt, std::size_t target);
    // JUMP_BACK to `target` (an earlier position).
    std::size_t emitJumpBack(std::size_t target, int line);

    std::size_t pos() const { return code_.size(); }
    const std::vector<Instr>& code() const { return code_; }
    int lineAt(std::size_t pc) const { return pc < lines_.size() ? lines_[pc] : 0; }
    const Const& constAt(std::size_t i) const { return consts_[i]; }
    std::size_t constCount() const { return consts_.size(); }

    const std::string& name() const { return name_; }
    void setName(std::string n) { name_ = std::move(n); }
    int arity() const { return arity_; }
    void setArity(int n) { arity_ = n; }
    bool isVariadic() const { return variadic_; }
    void setVariadic(bool v) { variadic_ = v; }
    int localCount() const { return localCount_; }
    void setLocalCount(int n) { localCount_ = n; }
    int maxStack() const { return maxStack_; }
    void setMaxStack(int n) { maxStack_ = n; }

    void addCapture(int parentSlot, int localSlot) { captures_.push_back({parentSlot, localSlot}); }
    const std::vector<CaptureSpec>& captureSpecs() const { return captures_; }
    int captureCount() const { return static_cast<int>(captures_.size()); }

    std::size_t addBlock(std::unique_ptr<BytecodeModule> sub);
    const BytecodeModule& block(std::size_t i) const { return *blocks_[i]; }
    std::size_t blockCount() const { return blocks_.size(); }

    // Interns every Symbol and SendSite name, recursively through blocks.
    void linkSymbols(proto::ProtoContext* ctx);

    // Human-readable listing of this module and its blocks (tests, --disassemble).
    std::string disassemble() const;

private:
    std::vector<Instr> code_;
    std::vector<int> lines_;  // one entry per code word
    std::vector<Const> consts_;
    std::unordered_map<long long, std::size_t> intIndex_;
    std::unordered_map<std::uint64_t, std::size_t> doubleIndex_;  // by bit pattern
    std::unordered_map<std::string, std::size_t> bigIntIndex_;    // base + ":" + digits
    std::unordered_map<std::string, std::size_t> stringIndex_;
    std::unordered_map<long long, std::size_t> charIndex_;
    std::unordered_map<std::string, std::size_t> symbolIndex_;
    std::unordered_map<std::string, std::size_t> sendIndex_;      // name + "/" + argc
    std::vector<std::unique_ptr<BytecodeModule>> blocks_;
    std::vector<CaptureSpec> captures_;
    std::string name_ = "<top>";
    int arity_ = 0;
    bool variadic_ = false;
    int localCount_ = 0;
    int maxStack_ = 0;
};

} // namespace protoScala
