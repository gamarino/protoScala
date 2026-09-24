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
#include "compiler/ClassInfo.h"
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

// --- by-name parameters (D47) ----------------------------------------------
// Bit k of a mask marks parameter k of one parameter list as `x: => T`. Only the
// first 32 parameters of a list can carry one.
constexpr std::size_t kMaxByNameParams = 32;
std::uint32_t byNameMaskOfParams(const std::vector<Param>& params);
// One mask per parameter list of a def. Desugar rewrote the extra lists into
// nested lambdas, so the chain is read back off the body.
std::vector<std::uint32_t> byNameMasksOfDef(const DefDef& d);

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
        // A local `def`: one by-name mask per parameter list (D47).
        std::vector<std::uint32_t> byNameMasks;
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
        // The enclosing `finally` bodies of the code being compiled, innermost
        // last. A `return` emits them innermost-first before its RETURN, and a
        // `try`'s normal exit emits its own inline (plan A0-4). `holes` records
        // the [from, to) spans of those inlined copies, which must be EXCLUDED
        // from this try's own handler ranges: a cleanup emitted at a `return`
        // has already left the try, so an exception from it must not re-enter
        // the same handler (which would RETHROW an unset slot).
        struct ActiveFinally {
            const Node* body;
            int slot;
            std::vector<std::pair<std::size_t, std::size_t>> holes;
        };
        std::vector<ActiveFinally> finallys;
    };
    // Method: `this` in slot 0 and no enclosing function (Design note 9).
    enum class FnShape { Lambda, Def, Method };
    enum class RefKind { Local, Member, Global };
    struct Resolution {
        RefKind ref;
        LocalInfo local;                     // RefKind::Local
        BindingKind kind;                    // Local / Global
        std::string key;                     // Global: the global's key; Member: the attribute key
        const MemberInfo* member = nullptr;  // RefKind::Member
        // One by-name mask per parameter list of the resolved declaration (D47).
        std::vector<std::uint32_t> byNameMasks;
    };

    // The template whose members are being compiled (CompileTemplates.cpp).
    struct TemplateScope {
        const ClassInfo* info;       // the class, trait or the class of an object
        const ClassInfo* companion;  // its companion (private access, D5), or nullptr
        std::string selfName;        // `self =>` alias of `this`, or empty
    };

    GlobalTable& globals_;
    FunctionState* fn_ = nullptr;
    const TemplateScope* tmpl_ = nullptr;
    std::unordered_set<const Node*> boxed_;  // declarations kept in Cells

    // Emission with stack-depth accounting.
    void emit(Op op, std::uint64_t operand, SourcePos pos, int stackEffect);
    std::size_t emitJump(Op op, SourcePos pos, int stackEffect);
    void adjust(int delta);

    // Scopes and names.
    int newSlot() { return fn_->nextSlot++; }
    LocalInfo declareLocal(const std::string& name, BindingKind kind, bool boxed,
                           std::vector<std::uint32_t> byNameMasks = {});
    Resolution resolve(const std::string& name, SourcePos pos);
    // The names `name` may denote, as seen from the template being compiled: a
    // template nested in an `object` is lifted to a top-level definition with a
    // dotted name, and Scala's scoping sees the enclosing object's members
    // without a qualifier. Innermost prefix first, the bare name last.
    std::vector<std::string> scopedNames(const std::string& name) const;
    // `O.C` / `O.P.C` when the whole Select chain is a plain path that names a
    // lifted top-level definition, else "". Empty when a local or a member
    // shadows the head of the path.
    std::string liftedGlobalPath(const Node& n);
    const std::string& globalKey(const std::string& name) const;
    const LocalInfo* findInFunction(FunctionState* f, const std::string& name);
    LocalInfo captureInto(FunctionState* f, const std::string& name, SourcePos pos, bool* found);

    // Code generation.
    void compileExpr(const Node& n);
    // s"..." / raw"..." -> CONCAT over the pieces; f"..." -> a call of __fmt.
    void compileInterp(const InterpString& n);
    void compileFormat(const InterpString& n);
    void compileIdent(const Ident& id);
    void compileApply(const Apply& a);
    void compileSelect(const Select& s);
    void compileAssign(const Assign& a);
    void compileIf(const If& i);
    void compileWhile(const While& w);
    void compileBlock(const Block& b);
    void compileReturn(const Return& r);
    void compileShortCircuit(const Node& lhs, const Node& rhs, bool isAnd, SourcePos pos);
    void compileArgsAndCall(const std::vector<NodePtr>& args, SourcePos pos,
                            std::uint32_t byNameMask = 0);
    void compileByNameArgument(const Node& arg);
    // Compiles a function body into a new block of the current module and emits
    // the capture pushes and MAKE_FN. Def and Method bodies allow `return`.
    // `allowByName`: this callee is named at its call sites, so by-name
    // parameters can be honoured there (D47).
    // `selfAlias`: an extra name bound to slot 0 of a Method, which is how an
    // extension's receiver (`extension (x: T) def m = x`) reaches `this`.
    void compileFunction(const std::string& name, const std::vector<Param>& params,
                         const Node& body, FnShape shape, SourcePos pos,
                         bool paramless = false, bool allowByName = false,
                         const std::string& selfAlias = {});
    // Bit k set: argument k of the application whose callee expression is `fn`
    // is by-name (D47). Zero unless the compiler resolves `fn` to a declaration
    // that says so; an unresolved callee evaluates its arguments (D53).
    std::uint32_t byNameMaskOfCallee(const Node& fn);
    // The by-name masks of the declaration `fn` names, or nullptr.
    const std::vector<std::uint32_t>* byNameMasksOfCalleeHead(const Node& head);
    const std::vector<std::uint32_t>* byNameMasksOfMember(const std::string& name) const;
    const std::vector<std::uint32_t>* byNameMasksOfObjectMember(const std::string& objectName,
                                                                const std::string& member) const;
    // --- Exceptions (Phase 4, DESIGN §7) ---------------------------------
    void compileThrow(const Throw& n);
    void compileTry(const Try& n);
    // The `catch` cascade: compileMatch's machinery with RETHROW instead of
    // MATCH_ERROR when no case matches, because Scala propagates an exception
    // the handler does not match rather than replacing it. `slot` holds the
    // caught value, which the handler entry has already written there.
    void compileCatchCases(const std::vector<CaseDef>& cases, int slot, SourcePos pos);
    // Emits every enclosing finally body, innermost first, recording each span
    // so the owning try can exclude it from its handler range. Called by
    // compileReturn AFTER the returned value has been evaluated.
    void emitEnclosingFinallys(SourcePos pos);
    // Appends one handler entry per piece of [start, end) that no hole covers.
    // Returns the indices, for patchHandlerBody.
    std::vector<std::size_t> addHandlerRanges(
        std::size_t start, std::size_t end, int entryDepth, int slot,
        BytecodeModule::HandlerKind kind,
        const std::vector<std::pair<std::size_t, std::size_t>>& holes);

    // --- Named and default arguments (Phase 4, DESIGN §5.2) ---------------
    // Records the callee's parameter names (index 0 is `this` for a method, so
    // the names and the slots line up) and compiles one default block per
    // parameter that has an initialiser. Binding happens in the CALLEE, so this
    // is all a call site needs to know nothing about.
    // Called AFTER the callee's body, so `mod.captureCount()` is final.
    // `outerScope` is the callee's own parameter scope, which by then also holds
    // every value it captured.
    void recordParamsAndDefaults(BytecodeModule& mod, const std::vector<Param>& params,
                                 bool method, const std::string& name,
                                 const std::unordered_map<std::string, LocalInfo>& outerScope);
    // A default's value is computed by a block of the callee's own module whose
    // parameters are the parameters declared BEFORE it, so a default may read
    // them (`def f(a: Int, b: Int = a + 1)`); `execute` runs it with the frame's
    // slots as its arguments.
    void compileDefaultBlock(BytecodeModule& owner, const std::vector<Param>& params,
                             std::size_t index, bool method, const std::string& name,
                             const std::unordered_map<std::string, LocalInfo>& outerScope);
    // Splits `args` into positional and named, in SOURCE order, rejecting a
    // duplicate name and a positional argument after a named one. Returns the
    // keyword names; `positional` gets the count before the first named one.
    std::vector<std::string> splitNamedArgs(const std::vector<NodePtr>& args,
                                            std::size_t* positional);

    // --- Extension methods (Phase 4, D6) ---------------------------------
    // Installs each member as an attribute of the receiver type's PROTOTYPE, so
    // dispatch is the ordinary prototype walk and the installation is global and
    // session-wide (D82, D83).
    void compileExtension(const ExtensionDef& n);
    // The prototype target `__installExtension` resolves: a type key `@C` for a
    // type the compiler knows, or a builtin value type's name (`Int`, `String`,
    // `List`, ...). Empty when the type is not extendable.
    std::string extensionTarget(const std::string& typeName) const;

    void compileLazyThunk(const Node& rhs, SourcePos pos);  // thunk + MAKE_LAZY
    void compileStats(const std::vector<NodePtr>& stats, std::size_t from, SourcePos pos);
    void storeLocal(const LocalInfo& info, SourcePos pos);
    void loadLocal(const LocalInfo& info, SourcePos pos);

    // Pre-pass: fills boxed_ for the declarations of one function body.
    void analyseCaptures(const std::vector<Param>& params, const Node& body);

    // --- Templates (CompileTemplates.cpp) ---------------------------------
    const MemberInfo* memberOf(const std::string& name) const;
    std::string selectKey(const std::string& name) const;
    // A SendSite constant for `recv.name(...)`: the private key when `name` is a
    // private member here, with the plain name as the runtime fallback (D5).
    std::size_t sendSite(const std::string& name, std::uint32_t argc);
    std::size_t kwSendSite(const std::string& name, std::uint32_t positional,
                           const std::vector<std::string>& keywords);
    void loadThis(SourcePos pos);
    std::vector<const TemplateDef*> sortTemplates(const std::vector<const TemplateDef*>& ts) const;
    ClassInfo buildClassInfo(const TemplateDef& t, const std::string& typeKey) const;
    void linkCompanions(const std::vector<const TemplateDef*>& ts);
    const ClassInfo& resolveType(const TypeTree& t, SourcePos pos) const;
    const ClassInfo* superclassOf(const ClassInfo& info) const;  // nullptr: AnyRef
    std::vector<std::string> runtimeChain(const ClassInfo& info) const;
    std::string ctorKeyFor(const ClassInfo& info, std::size_t argc, SourcePos pos) const;
    void compileTemplate(const TemplateDef& t, const ClassInfo& info);
    void compileConstructor(const TemplateDef& t, const ClassInfo& info);
    void compileAuxConstructor(const DefDef& d, const ClassInfo& info);
    void compileSetter(const std::string& fieldKey, const std::string& name, SourcePos pos);
    void compileObjectHolder(const ClassInfo& info, const std::string& termKey, SourcePos pos);
    void compileInitCall(const ClassInfo& target, const std::vector<NodePtr>& args, SourcePos pos);
    void compileNew(const New& n);
    void compileNewOf(const ClassInfo& info, const std::vector<NodePtr>& args, SourcePos pos);
    void compileTuple(const Tuple& t);
    // `qualifier` empty: plain `super.m`. Non-empty: `super[T].m`.
    void compileSuperSend(const std::string& name, const std::vector<NodePtr>& args,
                          const std::string& qualifier, SourcePos pos);
    void compileNamedSend(const Select& sel, const std::vector<NodePtr>& args, SourcePos pos);

    // --- Pattern matching (CompilePatterns.cpp) ---------------------------
    void compileMatch(const Match& m);
    void compilePattern(const Pattern& p, int slot, std::vector<std::size_t>& fail);
    void compileExtractor(const Pattern& p, int slot, std::vector<std::size_t>& fail);
    void compileListPattern(const Pattern& p, int slot, std::vector<std::size_t>& fail);
    void extractInto(const std::vector<std::string>& keys, int slot, const std::vector<PatternPtr>& subs,
                     std::vector<std::size_t>& fail, SourcePos pos);
    void emitProtoTest(const std::string& typeKey, int slot, SourcePos pos,
                       std::vector<std::size_t>& fail);
    void bindPattern(const std::string& name, int slot, SourcePos pos);
    void checkNoVariables(const Pattern& p) const;
    void checkDistinctVariables(const Pattern& p) const;
    // Pushes a Boolean: is the value in `slot` a T? Returns false (emitting
    // nothing) when every value matches, which only `Any` in a pattern does.
    bool compileTypeTest(const TypeTree& t, int slot, SourcePos pos, bool inPattern);
    void compileInstanceOf(const Node& value, const TypeTree& t, bool cast, SourcePos pos);
};

} // namespace protoScala
