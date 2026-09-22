/*
 * protoscala — command-line entry point.
 *
 * Current scope: --version and --help. Running a .scala script and the
 * interactive REPL arrive with the frontend, compiler and VM
 * (docs/ROADMAP.md, Phase 1).
 */
#include "protoScala/Version.h"

#include <cstdio>
#include <cstring>

namespace {

void printVersion() {
    std::printf("protoScala %s\n", protoScala::versionString());
}

void printHelp() {
    std::printf(
        "Usage: protoscala [options] [script.scala]\n"
        "\n"
        "Options:\n"
        "  --version       Print version and exit.\n"
        "  --help, -h      Print this help and exit.\n"
        "\n"
        "Not yet implemented:\n"
        "  script.scala    Run a Scala 3 program.\n"
        "  (no args)       Start the interactive REPL.\n");
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::strcmp(argv[1], "--version") == 0) {
        printVersion();
        return 0;
    }
    if (argc == 2 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        printHelp();
        return 0;
    }
    if (argc >= 2 && argv[1][0] == '-') {
        std::fprintf(stderr, "protoscala: unknown option '%s'\n", argv[1]);
        printHelp();
        return 2;
    }
    std::fprintf(stderr,
        "protoscala: running Scala programs is not implemented yet\n");
    return 1;
}
