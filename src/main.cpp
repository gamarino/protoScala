/*
 * protoscala — command-line entry point: runs a Scala 3 script or starts the
 * REPL. Evaluation runs on a dedicated large-stack thread so deep recursion
 * raises StackOverflowError instead of crashing (StackGuard.h). The Session,
 * and with it the ProtoSpace, is created on that thread, so protoCore
 * registers it as the space's main thread (as tests/unit/EvalHarness.h does).
 */
#include "protoScala/Version.h"
#include "repl/Repl.h"
#include "runtime/Mailbox.h"
#include "repl/Session.h"
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
    } catch (const std::exception& e) {
        std::fflush(stdout);
        std::fprintf(stderr, "protoscala: internal error: %s\n", e.what());
        return 1;
    }
}
