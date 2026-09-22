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
    std::puts("An input is evaluated when it is complete. While a line is indented deeper");
    std::puts("than the input's first line, or opens a block (`=`, `then`, `do`, ...), the");
    std::puts("REPL keeps reading; an empty line, or a line back at the first line's column");
    std::puts("that does not continue the construct (`else`, `end`, ...), ends the input.");
}

void printOutcome(const EvalOutcome& o) {
    for (const std::string& line : o.echo) std::puts(line.c_str());
    std::fflush(stdout);
}

std::size_t indentOf(const std::string& line) {
    std::size_t k = 0;
    while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) ++k;
    return k;
}

bool isIdentChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

bool startsWithWord(const std::string& text, const char* word) {
    const std::string w(word);
    return text.compare(0, w.size(), w) == 0 && (text.size() == w.size() || !isIdentChar(text[w.size()]));
}

bool endsWithWord(const std::string& text, const char* word) {
    const std::string w(word);
    if (text.size() < w.size() || text.compare(text.size() - w.size(), w.size(), w) != 0) return false;
    return text.size() == w.size() || !isIdentChar(text[text.size() - w.size() - 1]);
}

// A line at the input's first column that belongs to the construct above it.
bool continuesConstruct(const std::string& trimmed) {
    static const char* const words[] = {"else", "then", "do", "end", "catch", "finally", "yield"};
    for (const char* w : words)
        if (startsWithWord(trimmed, w)) return true;
    const char c = trimmed.empty() ? '\0' : trimmed[0];
    return c == ')' || c == ']' || c == '}';
}

// A line whose last token opens an indentation region or needs an operand.
bool opensRegion(const std::string& trimmed) {
    if (trimmed.empty()) return false;
    const char last = trimmed.back();
    if (last == '=' || last == ':' || last == '{' || last == '(' || last == '[' || last == ',')
        return true;
    if (trimmed.size() >= 2 && trimmed.compare(trimmed.size() - 2, 2, "=>") == 0) return true;
    static const char* const words[] = {"then", "do", "else", "yield", "match", "return", "try",
                                        "finally"};
    for (const char* w : words)
        if (endsWithWord(trimmed, w)) return true;
    return false;
}

} // namespace

int runRepl() {
    const bool interactive = ::isatty(STDIN_FILENO) != 0;
    const std::string histPath = historyPath();
    if (interactive && !histPath.empty()) ::read_history(histPath.c_str());

    std::printf("protoScala %s REPL — :help for commands, :quit or Ctrl-D to exit\n",
                versionString());
    Session session;
    // The input being read: its lines, the indentation of its first line and
    // its last line. A line that ends the input without belonging to it is
    // kept in `pending` and read again as the first line of the next input.
    std::string buffer;
    std::size_t firstIndent = 0;
    std::string lastLine;
    std::string pending;
    bool havePending = false;

    // Evaluates the buffer; false (buffer kept) when it is incomplete and
    // `force` is not set.
    auto evaluate = [&](bool force) {
        const EvalOutcome o = session.evalReplInput(buffer, force);
        if (o.status == EvalStatus::Incomplete) return false;
        printOutcome(o);
        buffer.clear();
        return true;
    };
    // After a line was added: evaluate unless the construct may go on.
    auto lineAdded = [&](const std::string& line) {
        lastLine = line;
        if (indentOf(lastLine) > firstIndent || opensRegion(trim(lastLine))) return;
        evaluate(false);
    };

    for (;;) {
        std::string line;
        if (havePending) {
            line = std::move(pending);
            havePending = false;
        } else {
            bool eof = false;
            if (!readLine(buffer.empty() ? "scala> " : "     | ", interactive, line, &eof)) {
                if (eof) {
                    if (!buffer.empty()) evaluate(/*force=*/true);
                    std::puts("");
                    break;
                }
                continue;
            }
            if (interactive && !trim(line).empty()) ::add_history(line.c_str());
        }
        const std::string trimmed = trim(line);
        if (buffer.empty()) {
            if (trimmed.empty()) continue;
            if (trimmed[0] == ':') {
                if (trimmed == ":quit" || trimmed == ":q") break;
                if (trimmed == ":help") { printHelp(); continue; }
                if (trimmed.rfind(":load ", 0) == 0) { session.loadFile(trim(trimmed.substr(6))); continue; }
                std::fprintf(stderr, "unknown command: %s (try :help)\n", trimmed.c_str());
                continue;
            }
            buffer = line;
            firstIndent = indentOf(line);
            lineAdded(line);
            continue;
        }
        if (trimmed.empty()) {  // a blank line ends the input
            evaluate(/*force=*/true);
            continue;
        }
        if (indentOf(line) <= firstIndent && !continuesConstruct(trimmed) &&
            !session.needsMoreInput(buffer)) {
            // Back at the first column with a new statement: the input is
            // complete; this line starts the next one.
            evaluate(/*force=*/true);
            pending = line;
            havePending = true;
            continue;
        }
        buffer += "\n" + line;
        lineAdded(line);
    }
    if (interactive && !histPath.empty()) ::write_history(histPath.c_str());
    std::fflush(stdout);
    return 0;
}

} // namespace protoScala
