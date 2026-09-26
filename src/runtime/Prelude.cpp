#include "runtime/Prelude.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"
#include "support/PreludeImage.h"

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

PreludePath preludePath() {
    // Deliberately duplicates loadPrelude's conditions rather than reading a flag
    // it sets: `--version` runs before any session exists, so there is nothing to
    // read yet. The two are kept adjacent so a change to one is visible from the
    // other, and the enum makes the four states distinguishable instead of the
    // single boolean that was wrong.
#if defined(PROTOSCALA_HAVE_PRELUDE_IMAGE)
    const bool ok = preludeImageData().format == kPreludeImageFormat &&
                    preludeImageData().sourceHash == preludeSourceHash();
    if (!ok) return PreludePath::ImageStale;
    const char* noImage = std::getenv("PROTOSCALA_PRELUDE_NO_IMAGE");
    if (noImage && *noImage) return PreludePath::ImageDisabled;
    return PreludePath::Image;
#else
    return PreludePath::Source;
#endif
}

void loadPrelude(proto::ProtoContext* parent, ExecutionEngine& engine, GlobalTable& globals,
                 std::vector<std::unique_ptr<BytecodeModule>>& modules) {
    auto where = [](SourcePos p) {
        return " (lib/prelude.scala:" + std::to_string(p.line) + ":" + std::to_string(p.column) + ")";
    };
    const auto tStart = Clock::now();
    g_timing = PreludeTiming{};
#if defined(PROTOSCALA_HAVE_PRELUDE_IMAGE)
    // PROTOSCALA_PRELUDE_NO_IMAGE takes the source path with no message: the
    // image tests and the cold-start comparison need both paths in one binary,
    // and a flag that only disables an optimisation cannot make a program wrong.
    const char* noImage = std::getenv("PROTOSCALA_PRELUDE_NO_IMAGE");
    if (!(noImage && *noImage)) {
        const bool formatOk = preludeImageData().format == kPreludeImageFormat;
        const bool hashOk = preludeImageData().sourceHash == preludeSourceHash();
        if (formatOk && hashOk) {
            auto t0 = Clock::now();
            const BytecodeModule& mod = buildPreludeImage(globals, modules);
            g_timing.compileUs = usSince(t0);  // the image replaces parse+desugar+compile

            t0 = Clock::now();
            const_cast<BytecodeModule&>(mod).linkSymbols(parent);
            g_timing.linkUs = usSince(t0);

            t0 = Clock::now();
            engine.run(parent, mod);
            g_timing.runUs = usSince(t0);

            g_timing.fromImage = true;
            g_timing.totalUs = usSince(tStart);
            reportTiming();
            return;
        }
        // A hand-copied or hand-edited image. Never silently wrong: say so and
        // compile the source that ships in the same binary.
        std::fprintf(stderr,
                     "protoscala: the precompiled prelude image does not match this binary "
                     "(%s); compiling lib/prelude.scala instead\n",
                     !formatOk ? "format version" : "source hash");
    }
#endif
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
