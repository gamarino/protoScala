#include "runtime/Prelude.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace {

// A file-scope object holding doubles only. The per-ProtoSpace interning rule
// that forbids function-local statics applies to protoCore symbols; this holds
// no ProtoObject* and no symbol, so it is safe.
protoScala::PreludeTiming g_timing;

using Clock = std::chrono::steady_clock;

double usSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

void reportTiming() {
    if (const char* v = std::getenv("PROTOSCALA_PRELUDE_TIMING"); v && *v)
        std::fprintf(stderr,
                     "prelude: parse=%.0fus desugar=%.0fus compile=%.0fus link=%.0fus "
                     "run=%.0fus total=%.0fus image=%d\n",
                     g_timing.parseUs, g_timing.desugarUs, g_timing.compileUs, g_timing.linkUs,
                     g_timing.runUs, g_timing.totalUs, g_timing.fromImage ? 1 : 0);
}

}  // namespace

namespace protoScala {

const PreludeTiming& preludeTiming() { return g_timing; }

void loadPrelude(proto::ProtoContext* parent, ExecutionEngine& engine, GlobalTable& globals,
                 std::vector<std::unique_ptr<BytecodeModule>>& modules) {
    auto where = [](SourcePos p) {
        return " (lib/prelude.scala:" + std::to_string(p.line) + ":" + std::to_string(p.column) + ")";
    };
    const auto tStart = Clock::now();
    g_timing = PreludeTiming{};
    try {
        auto t0 = Clock::now();
        auto unit = parseSource(preludeSource());
        g_timing.parseUs = usSince(t0);

        t0 = Clock::now();
        desugar(*unit);
        g_timing.desugarUs = usSince(t0);

        t0 = Clock::now();
        Compiler compiler(globals);
        CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        g_timing.compileUs = usSince(t0);

        t0 = Clock::now();
        cu.module->linkSymbols(parent);
        g_timing.linkUs = usSince(t0);

        const BytecodeModule& mod = *cu.module;
        modules.push_back(std::move(cu.module));

        t0 = Clock::now();
        engine.run(parent, mod);
        g_timing.runUs = usSince(t0);
    } catch (const ParseError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what() + where(e.pos));
    } catch (const CompileError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what() + where(e.pos));
    } catch (const ScalaError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what());
    }
    g_timing.totalUs = usSince(tStart);
    reportTiming();
}

} // namespace protoScala
