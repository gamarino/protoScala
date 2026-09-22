/*
 * Compiler — core AST (after Desugar) to BytecodeModule (DESIGN §3.5).
 *
 * Pure C++: it never touches protoCore. Constants are recorded in the module
 * pools and materialised by the VM; symbol names are interned later by
 * BytecodeModule::linkSymbols. Per-function state lives in FunctionState
 * objects on the C++ stack (one per nested function being compiled), and
 * block scopes are a std::deque so references to outer scopes survive
 * pushes (protoST D29).
 */
#pragma once
#include "compiler/BytecodeModule.h"
#include "compiler/GlobalTable.h"
#include "frontend/AST.h"

#include <deque>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace protoScala {

struct CompileError : std::runtime_error {
    CompileError(const std::string& msg, SourcePos p) : std::runtime_error(msg), pos(p) {}
    SourcePos pos;
};

enum class UnitMode { Script, Repl };

// A top-level definition of a REPL input, for the echo.
struct ReplDefinition {
    std::string text;  // "val x", "var y", "lazy val z", "def f"
    std::string key;   // its global key (GlobalTable.h)
};

struct CompiledUnit {
    std::unique_ptr<BytecodeModule> module;  // arity 0: the top-level code
    std::string mainName;                    // empty when the unit has no @main
    std::string mainKey;                     // global key of the @main method
    bool mainTakesArgs = false;              // @main def f(args: String*)
    std::string resultName;                  // Repl: "res<N>" or empty
    std::string resultKey;                   // global key of resultName
    std::vector<ReplDefinition> definitions; // Repl echo, in source order
};

class Compiler {
public:
    explicit Compiler(GlobalTable& globals) : globals_(globals) {}

    CompiledUnit compileUnit(const CompilationUnit& unit, UnitMode mode,
                             int replResultIndex = 0);

private:
    struct LocalInfo {
        int slot = 0;
        BindingKind kind = BindingKind::Val;
        bool boxed = false;
        bool captured = false;  // reached through a capture of an enclosing function
    };
    struct FunctionState {
        BytecodeModule* mod = nullptr;
        FunctionState* parent = nullptr;
        std::deque<std::unordered_map<std::string, LocalInfo>> scopes;
        int nextSlot = 0;
        int depth = 0;
        int maxDepth = 0;
        bool allowsReturn = false;  // def bodies
        bool isTopLevel = false;
    };
    enum class RefKind { Local, Global };
    struct Resolution {
        RefKind ref;
        LocalInfo local;    // RefKind::Local
        BindingKind kind;   // binding kind in both cases
        std::string key;    // RefKind::Global: the global's key
    };

    GlobalTable& globals_;
    FunctionState* fn_ = nullptr;
    std::unordered_set<const Node*> boxed_;  // declarations kept in Cells

    // Emission with stack-depth accounting.
    void emit(Op op, std::uint64_t operand, SourcePos pos, int stackEffect);
    std::size_t emitJump(Op op, SourcePos pos, int stackEffect);
    void adjust(int delta);

    // Scopes and names.
    int newSlot() { return fn_->nextSlot++; }
    LocalInfo declareLocal(const std::string& name, BindingKind kind, bool boxed);
    Resolution resolve(const std::string& name, SourcePos pos);
    const std::string& globalKey(const std::string& name) const;
    const LocalInfo* findInFunction(FunctionState* f, const std::string& name);
    LocalInfo captureInto(FunctionState* f, const std::string& name, SourcePos pos, bool* found);

    // Code generation.
    void compileExpr(const Node& n);
    void compileIdent(const Ident& id);
    void compileApply(const Apply& a);
    void compileSelect(const Select& s);
    void compileAssign(const Assign& a);
    void compileIf(const If& i);
    void compileWhile(const While& w);
    void compileBlock(const Block& b);
    void compileReturn(const Return& r);
    void compileShortCircuit(const Node& lhs, const Node& rhs, bool isAnd, SourcePos pos);
    void compileArgsAndCall(const std::vector<NodePtr>& args, SourcePos pos);
    // Compiles a function body into a new block of the current module and emits
    // the capture pushes and MAKE_FN. `isDef` enables `return`.
    void compileFunction(const std::string& name, const std::vector<Param>& params,
                         const Node& body, bool isDef, SourcePos pos);
    void compileLazyThunk(const Node& rhs, SourcePos pos);  // thunk + MAKE_LAZY
    void storeLocal(const LocalInfo& info, SourcePos pos);
    void loadLocal(const LocalInfo& info, SourcePos pos);

    // Pre-pass: fills boxed_ for the declarations of one function body.
    void analyseCaptures(const std::vector<Param>& params, const Node& body);
};

} // namespace protoScala
