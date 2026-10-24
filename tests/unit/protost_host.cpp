/*
 * `protoscala <file.scala>` with a protoST runtime in the process: the host the
 * tutorial's protoST fixture runs under (tests/CMakeLists.txt). The shipped
 * `protoscala` links no runtime of the family but protoCore, so an `import st.`
 * needs an embedder that constructs an STRuntime; this is the smallest one. It
 * is built only by the test tree, with -DPROTOSCALA_PROTOST_INTEROP=ON, and is
 * never installed.
 *
 * protoST finds its Smalltalk-source kernel through PROTOST_LIB and its modules
 * through STPATH; the test sets both.
 */
#include "repl/Session.h"
#include "protoST/STRuntime.h"

#include <cstdio>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s <file.scala> [args...]\n", argv[0]);
        return 2;
    }
    protoScala::Session session;
    protoST::STRuntime st(session.space());  // a guest in protoScala's space; registers provider:st
    const std::vector<std::string> args(argv + 2, argv + argc);
    return session.runScript(argv[1], args);
}
