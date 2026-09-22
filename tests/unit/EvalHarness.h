// Compiles and runs Scala snippets in one session for unit tests. Each
// eval() is one REPL-style unit: the value of its last expression is shown
// with Scala's toString, or "error: <Class>: <message>" is returned.
#pragma once
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "runtime/Primitives.h"
#include "runtime/Runtime.h"
#include "runtime/Values.h"
#include "protoCore.h"

#include <memory>
#include <string>
#include <vector>

namespace protoScala::test {

class EvalHarness {
public:
    EvalHarness() : runtime_(space_), engine_(runtime_.layout()) {
        installPrimitives(runtime_.rootContext(), runtime_.layout());
        for (const auto& n : builtinGlobalNames()) globals_.declare(n, BindingKind::Builtin);
        for (ClassInfo& t : builtinTypes()) globals_.defineBuiltinType(std::move(t));
    }

    Runtime& runtime() { return runtime_; }
    proto::ProtoSpace& space() { return space_; }
    GlobalTable& globals() { return globals_; }

    std::string eval(const std::string& src) {
        auto unit = parseSource(src);
        desugar(*unit);
        Compiler compiler(globals_);
        CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Repl, counter_++);
        proto::ProtoContext ctx(&space_, runtime_.rootContext());
        cu.module->linkSymbols(&ctx);
        const BytecodeModule& mod = *cu.module;
        modules_.push_back(std::move(cu.module));
        try {
            engine_.run(&ctx, mod);
            if (cu.resultName.empty()) return "";
            const auto* key = proto::ProtoString::createSymbol(&ctx, cu.resultKey.c_str());
            const proto::ProtoObject* v = runtime_.layout().globals->getOwnAttributeDirect(&ctx, key);
            return engine_.showTopLevel(&ctx, v ? v : PROTO_NONE);  // a Scala toString may throw
        } catch (const ScalaError& e) {
            return std::string("error: ") + e.what();
        }
    }

    // Runs a hand-assembled top-level module (engine tests of opcodes the
    // compiler does not emit yet). The value of its RETURN is shown.
    std::string runModule(std::unique_ptr<BytecodeModule> mod) {
        proto::ProtoContext ctx(&space_, runtime_.rootContext());
        mod->linkSymbols(&ctx);
        const BytecodeModule& m = *mod;
        modules_.push_back(std::move(mod));
        try {
            const proto::ProtoObject* v = engine_.run(&ctx, m);
            return engine_.showTopLevel(&ctx, v);
        } catch (const ScalaError& e) {
            return std::string("error: ") + e.what();
        }
    }

private:
    proto::ProtoSpace space_;  // first member: destroyed last
    Runtime runtime_;
    ExecutionEngine engine_;
    GlobalTable globals_;
    std::vector<std::unique_ptr<BytecodeModule>> modules_;  // functions point into them
    int counter_ = 0;
};

} // namespace protoScala::test
