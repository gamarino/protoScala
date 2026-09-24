/*
 * protoscala — command-line entry point: runs a Scala 3 script or starts the
 * REPL. Evaluation runs on a dedicated large-stack thread so deep recursion
 * raises StackOverflowError instead of crashing (StackGuard.h). The Session,
 * and with it the ProtoSpace, is created on that thread, so protoCore
 * registers it as the space's main thread (as tests/unit/EvalHarness.h does).
 */
#include "protoScala/Version.h"
#include "repl/Repl.h"
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
    // Which prelude path this binary takes. A user who measures start-up needs
    // to know whether the image is in it, and a binary built by cross-compiling
    // honestly says it is not.
#if defined(PROTOSCALA_HAVE_PRELUDE_IMAGE)
    std::printf("prelude: precompiled image (set PROTOSCALA_PRELUDE_NO_IMAGE=1 to compile "
                "lib/prelude.scala at start-up instead)\n");
#else
    std::printf("prelude: compiled at start-up (no precompiled image in this build)\n");
#endif
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
        "\n"
        "With a script: runs its top-level statements, then its @main method\n"
        "(arguments after the script are passed to @main).\n"
        "Without arguments: starts the interactive REPL.\n");
}

struct ScriptJob {
    std::string path;
    std::vector<std::string> args;
    bool disassemble = false;
};

int runJob(void* p) {
    auto* job = static_cast<ScriptJob*>(p);
    protoScala::Session session;
    return job->disassemble ? session.disassemble(job->path)
                            : session.runScript(job->path, job->args);
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
