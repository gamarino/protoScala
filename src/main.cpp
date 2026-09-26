/*
 * protoscala — command-line entry point: runs a Scala 3 script or starts the
 * REPL. Evaluation runs on a dedicated large-stack thread so deep recursion
 * raises StackOverflowError instead of crashing (StackGuard.h). The Session,
 * and with it the ProtoSpace, is created on that thread, so protoCore
 * registers it as the space's main thread (as tests/unit/EvalHarness.h does).
 */
#include "protoScala/Version.h"
#include "repl/Repl.h"
#include "runtime/Prelude.h"
#include "runtime/Errors.h"
#include "runtime/Mailbox.h"
#include "repl/Session.h"
#include "umd/ProviderPlugins.h"
#include "runtime/StackGuard.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace {

// The mailbox backend is part of the version line so a benchmark report can
// never misattribute its numbers to the wrong queue (DESIGN §8.5).
void printVersion() {
    std::printf("protoScala %s (actor mailboxes: %s)\n", protoScala::versionString(),
                protoScala::Mailbox::implementationName());
    // Which prelude path this binary takes, asked of the LIBRARY. It used to be a
    // #if on PROTOSCALA_HAVE_PRELUDE_IMAGE here, which stopped reaching this
    // translation unit when protoscala began linking the shared protoScala target
    // (Phase 7): the image stayed in use and this line started denying it. A
    // `--version` that is quietly wrong is worse than one that is missing, because
    // it is what a user checks. tests/cli/version.sh now compares this line against
    // PROTOSCALA_PRELUDE_TIMING's own `image=` flag, which is ground truth.
    switch (protoScala::preludePath()) {
        case protoScala::PreludePath::Image:
            std::printf("prelude: precompiled image (set PROTOSCALA_PRELUDE_NO_IMAGE=1 to "
                        "compile lib/prelude.scala at start-up instead)\n");
            break;
        case protoScala::PreludePath::ImageDisabled:
            std::printf("prelude: compiled at start-up — a precompiled image is in this build "
                        "but PROTOSCALA_PRELUDE_NO_IMAGE is set\n");
            break;
        case protoScala::PreludePath::ImageStale:
            std::printf("prelude: compiled at start-up — the precompiled image in this build "
                        "does not match lib/prelude.scala and was rejected\n");
            break;
        case protoScala::PreludePath::Source:
            std::printf("prelude: compiled at start-up (no precompiled image in this build)\n");
            break;
    }
    // Which providers this binary can reach. protoScala ships no plug-in, so a
    // stock install prints only the directory it looks in -- which is the honest
    // answer to "can I `import py.numpy` yet?".
    const std::vector<std::string> plugins = protoScala::describeProviderPlugins();
    std::printf("provider plug-ins: %s\n",
                plugins.empty() ? "none found" : "");
    for (const std::string& line : plugins) std::printf("  %s\n", line.c_str());
    std::printf("  searched: PROTOSCALA_PROVIDERS, then %s\n",
                protoScala::providerPluginDirectory().c_str());
    std::printf("import prefixes routed: py, js, st, clj (a prefix with no "
                "registered provider reports it)\n");
}

void printHelp() {
    std::printf(
        "Usage: protoscala [options] [script.scala [args...]]\n"
        "\n"
        "Options:\n"
        "  --version            Print version and exit.\n"
        "  --help, -h           Print this help and exit.\n"
        "  --disassemble FILE   Print the compiled bytecode of FILE and exit.\n"
        "  --run-module FILE.so [args...]\n"
        "                       Load a compiled module and run it: proto_module_init,\n"
        "                       then proto_module_main when it exports one.\n"
        "\n"
        "With a script: runs its top-level statements, then its @main method\n"
        "(arguments after the script are passed to @main).\n"
        "Without arguments: starts the interactive REPL.\n");
}

struct ScriptJob {
    std::string path;
    std::vector<std::string> args;
    bool disassemble = false;
    bool runModule = false;
};

int runJob(void* p) {
    auto* job = static_cast<ScriptJob*>(p);
    protoScala::Session session;
    if (job->disassemble) return session.disassemble(job->path);
    if (job->runModule) return session.runModule(job->path, job->args);
    return session.runScript(job->path, job->args);
}

} // namespace

int main(int argc, char** argv) {
    protoScala::configureThreadStacks();
    try {
        if (argc >= 2) {
            const std::string a = argv[1];
            if (a == "--version") { printVersion(); return 0; }
            if (a == "--help" || a == "-h") { printHelp(); return 0; }
            ScriptJob job;
            if (a == "--disassemble") {
                if (argc != 3) {
                    std::fprintf(stderr, "protoscala: --disassemble takes one file\n");
                    return 2;
                }
                job.path = argv[2];
                job.disassemble = true;
                return protoScala::runOnEvaluatorThread(&runJob, &job);
            }
            if (a == "--run-module") {
                if (argc < 3) {
                    std::fprintf(stderr, "protoscala: --run-module takes a .so path\n");
                    return 2;
                }
                job.path = argv[2];
                job.args.assign(argv + 3, argv + argc);
                job.runModule = true;
                return protoScala::runOnEvaluatorThread(&runJob, &job);
            }
            if (!a.empty() && a[0] == '-') {
                std::fprintf(stderr, "protoscala: unknown option '%s'\n", argv[1]);
                printHelp();
                return 2;
            }
            job.path = a;
            job.args.assign(argv + 2, argv + argc);
            return protoScala::runOnEvaluatorThread(&runJob, &job);
        }
        // No arguments: start the interactive REPL.
        return protoScala::runOnEvaluatorThread([](void*) { return protoScala::runRepl(); },
                                                nullptr);
    } catch (const protoScala::ScalaThrow&) {
        // An uncaught Scala exception that escaped the session's own report (a
        // throw from a thread body, say): the session prints the class and
        // message, so this arm only has to distinguish it from a VM defect.
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: uncaught exception\n");
        return 1;
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: internal error: %s\n", e.what());
        return 1;
    }
}
