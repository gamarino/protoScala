/*
 * Repl — interactive read-eval-print loop over Session (Session.h). Uses
 * libreadline for line editing and history on a terminal, and falls back to
 * plain stdin (with the prompt still printed) so piped sessions and the CLI
 * tests are scriptable and produce a readable transcript, as
 * protoClojure src/repl/Repl.cpp does.
 */
#include "repl/Repl.h"
#include "protoScala/Version.h"
#include "repl/Session.h"

#include <readline/history.h>
#include <readline/readline.h>
// readline/chardefs.h defines a RETURN macro that would break any later
// `Op::RETURN` (protoClojure src/repl/Repl.cpp:15-22).
#ifdef RETURN
#  undef RETURN
#endif

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace protoScala {

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string historyPath() {
    const char* home = std::getenv("HOME");
    return (home && *home) ? std::string(home) + "/.protoscala_history" : std::string();
}

// One line from readline on a terminal, from stdin otherwise (the prompt is
// still printed so transcripts are readable). Copy of protoClojure
// src/repl/Repl.cpp:104-127.
bool readLine(const char* prompt, bool interactive, std::string& out, bool* eof) {
    *eof = false;
    if (interactive) {
        char* line = ::readline(prompt);
        if (!line) { *eof = true; return false; }
        out.assign(line);
        std::free(line);
        return true;
    }
    std::fputs(prompt, stdout);
    std::fflush(stdout);
    std::string buf;
    int ch;
    bool any = false;
    while ((ch = std::getc(stdin)) != EOF) {
        any = true;
        if (ch == '\n') { out = buf; return true; }
        buf.push_back(static_cast<char>(ch));
    }
    if (any) { out = buf; return true; }
    *eof = true;
    return false;
}

void printHelp() {
    std::puts("Commands (at the primary prompt):");
    std::puts("  :help             Show this help");
    std::puts("  :quit, :q         Exit (also Ctrl-D)");
    std::puts("  :load <file>      Run a file in this session");
    std::puts("An input is evaluated as soon as it is complete. An empty line at the");
    std::puts("continuation prompt forces evaluation.");
}

void printOutcome(const EvalOutcome& o) {
    for (const std::string& line : o.echo) std::puts(line.c_str());
    std::fflush(stdout);
}

} // namespace

int runRepl() {
    const bool interactive = ::isatty(STDIN_FILENO) != 0;
    const std::string histPath = historyPath();
    if (interactive && !histPath.empty()) ::read_history(histPath.c_str());

    std::printf("protoScala %s REPL — :help for commands, :quit or Ctrl-D to exit\n",
                versionString());
    Session session;
    std::string buffer;
    for (;;) {
        std::string line;
        bool eof = false;
        const bool continuing = !buffer.empty();
        if (!readLine(continuing ? "     | " : "scala> ", interactive, line, &eof)) {
            if (eof) { std::puts(""); break; }
            continue;
        }
        const std::string trimmed = trim(line);
        if (!continuing && !trimmed.empty() && trimmed[0] == ':') {
            if (interactive) ::add_history(line.c_str());
            if (trimmed == ":quit" || trimmed == ":q") break;
            if (trimmed == ":help") { printHelp(); continue; }
            if (trimmed.rfind(":load ", 0) == 0) { session.loadFile(trim(trimmed.substr(6))); continue; }
            std::fprintf(stderr, "unknown command: %s (try :help)\n", trimmed.c_str());
            continue;
        }
        if (interactive && !trimmed.empty()) ::add_history(line.c_str());
        if (!continuing && trimmed.empty()) continue;
        const bool force = continuing && trimmed.empty();
        buffer += buffer.empty() ? line : "\n" + line;
        const EvalOutcome o = session.evalReplInput(buffer, force);
        if (o.status == EvalStatus::Incomplete) continue;
        printOutcome(o);
        buffer.clear();
    }
    if (interactive && !histPath.empty()) ::write_history(histPath.c_str());
    std::fflush(stdout);
    return 0;
}

} // namespace protoScala
