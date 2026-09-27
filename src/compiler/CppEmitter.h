/*
 * CppEmitter — the transpiler's back end (Phase 7 Tasks 6–9).
 *
 * It consumes the compiled `BytecodeModule` tree (§D1(c)), so everything
 * Scala-specific — trait linearization, pattern compilation, template synthesis,
 * slot and capture assignment, by-name masks, D5 key qualification, `@main`
 * detection — has already been decided by the code the conformance suite
 * validates. The emitter's whole job is the 60 opcodes.
 *
 * Two properties are structural rather than advisory:
 *
 *  - **No generated C++ local ever holds a `const proto::ProtoObject*`** (P1).
 *    The emitter tracks the operand-stack depth at emit time, so every stack
 *    access is a constant index into the frame's traced slots — `S[7]`, never
 *    `*sp++` and never a temporary. There is no expression-temporary mechanism at
 *    all, which is why a generated function cannot commit P4 rule 3 by accident.
 *  - **`check()` runs first and emits nothing.** It collects every unsupported
 *    construct and the caller reports all of them; a partially written `.cpp`
 *    that a later `make` compiles into something is worse than no output (§D5).
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/Compiler.h"
#include "compiler/CppTables.h"

#include <iosfwd>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace protoScala {

class GlobalTable;

struct EmitOptions {
    std::string sourcePath;      // absolute, for #line
    std::string logicalPath;     // the module's declared logical path
    std::string moduleVersion;   // "" when none (D4)
    bool asScript = false;       // emit proto_module_main (D8)
    // `object Main extends App` (D104): the object's initialisation is the program,
    // so the entry point forces this global instead of calling an @main. Empty when
    // the unit declares none.
    std::string appKey;
};

/** One construct the first cut refuses, with the position to report it at. */
struct Refusal {
    std::string message;
    int line = 0;
};

class CppEmitter {
public:
    CppEmitter(std::ostream& out, EmitOptions opts);

    /** Collects every Refusal first; emits nothing and returns them if any. */
    std::vector<Refusal> check(const CompiledUnit& unit) const;

    /** Emits the whole translation unit. Precondition: check() returned empty. */
    bool emit(const CompiledUnit& unit, const GlobalTable& globals);

    /**
     * §D2's analysis: the reasons this unit needs protoScala, and the first
     * eligibility condition it violates. Empty means protoCore-pure.
     */
    static std::vector<std::string> purityReport(const CompiledUnit& unit);

private:
    struct Decoded {
        Op op;
        std::uint64_t operand;
        std::size_t pc;        // word index of the instruction (of its EXTEND, if any)
        std::size_t next;      // word index after it
    };

    std::ostream& out_;
    EmitOptions opts_;
    std::vector<tables::FlatBlock> flat_;
    int lastLine_ = -1;

    static std::vector<Decoded> decode(const BytecodeModule& mod);
    /** Jump targets and handler bodies: the word indices that become C++ labels. */
    static std::set<std::size_t> labelTargets(const BytecodeModule& mod);
    /**
     * The operand-stack depth at the start of every instruction, by forward
     * propagation from pc 0 with a worklist over the jump targets. Throws
     * std::logic_error when two paths reach one instruction at different depths,
     * which would be a compiler defect and would otherwise surface as a wrong
     * value.
     */
    static std::vector<int> depths(const BytecodeModule& mod);
    /** The stack effect of one decoded instruction, or kTerminal. */
    static int effect(const BytecodeModule& mod, const Decoded& d);

    /** One exported top-level function: its Scala name and its flat block index. */
    struct Export {
        std::string name;
        std::size_t block;
    };
    /**
     * The top-level `def`s a foreign runtime may call: every `MAKE_FN b;
     * STORE_GLOBAL k` pair in the root block whose block `b` captures nothing.
     *
     * A capturing block reads its captures from the `self` argument, and a bare
     * `proto::ProtoMethod` cell is called with no `self`, so exporting one would
     * publish a function that reads a null environment. Excluding it is a
     * correctness requirement, not a simplification; a top-level `def` captures
     * nothing by construction, so nothing a reader would expect is lost.
     */
    std::vector<Export> exportsOf(const BytecodeModule& root) const;

    void collect(const BytecodeModule& mod, std::vector<Refusal>& out) const;
    bool emitBlock(std::size_t index, const GlobalTable& globals);
    void emitTables(std::size_t index);
    void line(const BytecodeModule& mod, std::size_t pc);
};

}  // namespace protoScala
