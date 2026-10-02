/*
 * protoscalac — protoScala source to C++ that calls the runtime directly.
 *
 * It runs protoScala's OWN lexer, parser, desugarer and compiler, then emits C++
 * from the resulting BytecodeModule tree instead of running it (§D1(c)). Nothing
 * Scala-specific is reimplemented here: trait linearization, pattern compilation,
 * template synthesis, slot and capture assignment, by-name masks and `@main`
 * detection have already happened, in the code the conformance suite validates.
 *
 * It creates a Session (§D7), so an `import` behaves at transpile time exactly as
 * it does in the interpreter — D90 resolves an import by LOADING, which needs a
 * ProtoSpace — and so a compiled module keeps early type binding. That is the one
 * way it differs from protoscala-precompile, which deliberately creates neither.
 *
 * Usage: protoscalac <file.scala> [options]
 */
#include "compiler/CppEmitter.h"
#include "compiler/Compiler.h"
#include "compiler/CppTables.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "repl/Session.h"
#include "runtime/Errors.h"
#include "runtime/StackGuard.h"
#include "support/Platform.h"
#include <protoScala/Version.h>
#include "protoCore.h"

#include "protoscalac_paths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace protoScala;

namespace {

struct Toolchain {
    std::string cxx;
    std::vector<std::string> includeDirs;
    std::vector<std::string> libraryDirs;
};

// A list of directories, separated as PATH is: ';' on Windows (a drive letter
// contains ':'), ':' elsewhere.
std::vector<std::string> splitPathList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == kPathListSeparator) { if (!cur.empty()) out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

fs::path executableDir(const char* argv0) {
    std::error_code ec;
    const fs::path self = protoScala::currentExecutable(ec);
    if (!ec && !self.empty()) return self.parent_path();
    const fs::path fallback = fs::canonical(argv0, ec);
    if (!ec) return fallback.parent_path();
    return fs::current_path();
}

// The same build-tree-versus-installation resolution protopyc uses, so a
// relocated prefix keeps working: an installed entry is relative to the
// executable's directory, a build-tree entry is absolute.
Toolchain resolveToolchain(const char* argv0) {
    Toolchain tc;
#if defined(_WIN32)
    // The compiler of the Developer environment the module is built in, found on
    // PATH there, rather than the absolute path of the one that built protoscalac.
    tc.cxx = "cl";
#else
    tc.cxx = PROTOSCALAC_DEFAULT_CXX;
#endif
    const fs::path exeDir = executableDir(argv0);
    std::error_code ec;
    const bool inBuildTree = fs::equivalent(exeDir, fs::path(PROTOSCALAC_BUILD_BINDIR), ec) && !ec;
    if (inBuildTree) {
        tc.includeDirs = splitPathList(PROTOSCALAC_BUILD_INCLUDE_DIRS);
        tc.libraryDirs = splitPathList(PROTOSCALAC_BUILD_LIBRARY_DIRS);
    } else {
        for (const std::string& d : splitPathList(PROTOSCALAC_INSTALL_INCLUDE_DIRS))
            tc.includeDirs.push_back((exeDir / d).lexically_normal().string());
        for (const std::string& d : splitPathList(PROTOSCALAC_INSTALL_LIBRARY_DIRS))
            tc.libraryDirs.push_back((exeDir / d).lexically_normal().string());
    }
    if (const char* v = std::getenv("PROTOSCALAC_CXX")) tc.cxx = v;
    if (const char* v = std::getenv("PROTOSCALAC_INCLUDE_DIRS"))
        tc.includeDirs = splitPathList(v);
    if (const char* v = std::getenv("PROTOSCALAC_LIBRARY_DIRS"))
        tc.libraryDirs = splitPathList(v);
    return tc;
}

#if !defined(_WIN32)
bool hasWhitespace(const std::string& s) {
    return s.find_first_of(" \t\n\r") != std::string::npos;
}
#endif

// Both headers a generated module includes must be findable on OUR include list, and
// this is checked rather than assumed because the failure mode is a segfault.
//
// Measured: an installation whose prefix held no protoCore headers compiled a module
// against `/usr/local/include/protoCore.h` -- a copy from February, of a different
// protoCore -- because the compiler's default search path found it. The module linked,
// loaded, and crashed in `ProtoObject::newChild` with no diagnostic. Nothing in the
// generated Makefile can prevent that, because `-I` ADDS to the default path rather than
// replacing it; the only place to catch it is here, before any output is written.
//
// The message names the environment override, because that is the actionable answer for
// a prefix where protoCore's headers live somewhere else.
bool checkHeadersAreReachable(const Toolchain& tc) {
    for (const char* header : {"protoCore.h", "protoScala/GeneratedModule.h"}) {
        bool found = false;
        for (const std::string& d : tc.includeDirs) {
            std::error_code ec;
            if (fs::is_regular_file(fs::path(d) / header, ec)) { found = true; break; }
        }
        if (found) continue;
        std::cerr << "protoscalac: cannot find " << header << " in its include path:\n";
        for (const std::string& d : tc.includeDirs) std::cerr << "    " << d << '\n';
        std::cerr << "  A generated module includes both protoCore.h and "
                     "protoScala/GeneratedModule.h.\n"
                     "  Compiling against a DIFFERENT protoCore.h that happens to be on the "
                     "compiler's\n"
                     "  default path produces a module that links and then crashes, so this is "
                     "refused\n"
                     "  here. Install protoCore's headers into the same prefix, or set "
                     "PROTOSCALAC_INCLUDE_DIRS\n"
                     "  ('"
                  << kPathListSeparator << "'-separated) to the directories that hold them.\n";
        return false;
    }
    return true;
}

#if defined(_WIN32)
// The NMake makefile of a module, for the Microsoft C++ toolset (cl and link from
// a Developer environment). The same contents as the POSIX Makefile below: one
// object per generated source, linked into module.dll against protoScala's and
// protoCore's import libraries. Every path is quoted, so a directory with spaces
// in it (C:\Program Files\...) needs no refusal here.
//
//  - /MD or /MDd: the C runtime protoScala.dll itself was built with. A module
//    and the runtime it calls must share one, or a std::string or an exception
//    crossing between them is undefined behaviour.
//  - /EHsc: C++ exceptions, which carry Scala exceptions through a module.
//  - /utf-8 /bigobj: as protoScala's own sources; a generated module is one large
//    function per block.
//  - /O2, as -O2 elsewhere.
bool writeMakefile(std::ostream& o, const std::vector<fs::path>& sources, const Toolchain& tc) {
    auto q = [](const std::string& p) { return "\"" + p + "\""; };
    o << "# Generated by protoscalac; do not edit. NMake syntax: run `nmake` in a\n"
      << "# Developer Command Prompt for Visual Studio.\n"
      << "CXX = " << tc.cxx << "\n"
      << "CXXFLAGS = /nologo /O2 /std:c++20 /EHsc /utf-8 /bigobj " PROTOSCALAC_MSVC_RUNTIME "\n"
      << "INCLUDES =";
    for (const std::string& d : tc.includeDirs) o << " /I" << q(d);
    o << "\nLDFLAGS  = /nologo /DLL";
    for (const std::string& d : tc.libraryDirs) o << " /LIBPATH:" << q(d);
    o << "\nLIBS     = protoScala.lib protoCore.lib\n"
      << "OBJS =";
    for (const fs::path& s : sources) o << ' ' << q(fs::path(s.filename()).replace_extension(".obj").string());
    o << "\nTARGET = module.dll\n\n"
      << "all: $(TARGET)\n\n"
      << "$(TARGET): $(OBJS)\n"
      << "\tlink $(LDFLAGS) /OUT:$(TARGET) $(OBJS) $(LIBS)\n\n";
    for (const fs::path& s : sources) {
        const std::string src = s.filename().string();
        const std::string obj = fs::path(s.filename()).replace_extension(".obj").string();
        o << q(obj) << ": " << q(src) << "\n"
          << "\t$(CXX) $(CXXFLAGS) $(INCLUDES) /c /Fo" << q(obj) << ' ' << q(src) << "\n\n";
    }
    o << "clean:\n"
      << "\t-del /q $(OBJS) $(TARGET) module.lib module.exp 2>nul\n";
    return o.good();
}
#else
bool writeMakefile(std::ostream& o, const std::vector<fs::path>& sources, const Toolchain& tc) {
    o << "# Generated by protoscalac; do not edit.\n"
      << "ifeq ($(origin CXX),default)\n"
      << "CXX = " << tc.cxx << "\nendif\n"
      // -O2, not -O3: a generated module is one long function per block, and
      // -O3's inlining makes compile time superlinear in block size.
      << "CXXFLAGS = -O2 -fPIC -std=c++20\n"
      << "INCLUDES =";
    for (const std::string& d : tc.includeDirs) o << " -I" << d;
    o << "\nLDFLAGS  =";
    for (const std::string& d : tc.libraryDirs) o << " -L" << d << " -Wl,-rpath," << d;
    o << "\nLIBS     = -lprotoScala -lprotoCore\n"
      << "SRCS =";
    for (const fs::path& s : sources) o << ' ' << s.filename().string();
    o << "\nOBJS = $(SRCS:.cpp=.o)\n"
      << "TARGET = module.so\n"
      << "all: $(TARGET)\n"
      << "$(TARGET): $(OBJS)\n"
      << "\t$(CXX) -shared $(LDFLAGS) -o $@ $^ $(LIBS)\n"
      << "%.o: %.cpp\n"
      << "\t$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<\n"
      << "clean:\n"
      << "\trm -f $(OBJS) $(TARGET)\n";
    return o.good();
}
#endif

bool generateMakefile(const fs::path& outRoot, const std::vector<fs::path>& sources,
                      const Toolchain& tc) {
#if !defined(_WIN32)
    // make splits words on whitespace and has no quoting; NMake's paths are quoted.
    for (const std::string& d : tc.includeDirs)
        if (hasWhitespace(d)) {
            std::cerr << "protoscalac: directory contains whitespace, which make cannot handle: \""
                      << d << "\" (set PROTOSCALAC_INCLUDE_DIRS / PROTOSCALAC_LIBRARY_DIRS to a "
                      << "path without spaces)\n";
            return false;
        }
    for (const std::string& d : tc.libraryDirs)
        if (hasWhitespace(d)) {
            std::cerr << "protoscalac: directory contains whitespace, which make cannot handle: \""
                      << d << "\" (set PROTOSCALAC_INCLUDE_DIRS / PROTOSCALAC_LIBRARY_DIRS to a "
                      << "path without spaces)\n";
            return false;
        }
#endif
    // After the whitespace check, because for a path list like "/a b" the whitespace
    // message is the clearer of the two, and before anything is written, because a
    // Makefile that would compile against the wrong protoCore.h must not exist.
    if (!checkHeadersAreReachable(tc)) return false;
    std::ofstream o(outRoot / "Makefile", std::ios::binary);
    if (!o) {
        std::cerr << "protoscalac: cannot write " << (outRoot / "Makefile").string() << '\n';
        return false;
    }
    return writeMakefile(o, sources, tc);
}

#if defined(_WIN32)
// The command that sets up the Microsoft C++ toolset, or "" when the process
// already runs in a Developer environment (VCINSTALLDIR is set there). Outside
// one, vswhere -- installed with every Visual Studio 2017 or later, Build Tools
// included -- names the newest installation that has the C++ toolset, and its
// vcvars script sets the environment up for one command. `error` says what is
// missing when neither works.
std::string developerEnvironmentSetup(std::string* error) {
    if (const char* v = std::getenv("VCINSTALLDIR"); v && *v) return "";
    const char* pf = std::getenv("ProgramFiles(x86)");
    const fs::path vswhere =
        fs::path(pf && *pf ? pf : "C:\\Program Files (x86)") / "Microsoft Visual Studio" /
        "Installer" / "vswhere.exe";
    std::error_code ec;
    if (!fs::is_regular_file(vswhere, ec)) {
        *error = "no Visual Studio installation was found (" + vswhere.string() + " is missing)";
        return "";
    }
    const std::string query = "\"\"" + vswhere.string() +
                              "\" -latest -products * -requires "
                              "Microsoft.VisualStudio.Component.VC.Tools.x86.x64 "
                              "-property installationPath\"";
    std::string installation;
    if (FILE* p = _popen(query.c_str(), "r")) {
        char buf[1024];
        while (std::fgets(buf, sizeof buf, p)) installation += buf;
        _pclose(p);
    }
    while (!installation.empty() &&
           (installation.back() == '\n' || installation.back() == '\r' || installation.back() == ' '))
        installation.pop_back();
#if defined(_M_ARM64)
    const char* script = "vcvarsarm64.bat";
#else
    const char* script = "vcvars64.bat";
#endif
    const fs::path vcvars = fs::path(installation) / "VC" / "Auxiliary" / "Build" / script;
    if (installation.empty() || !fs::is_regular_file(vcvars, ec)) {
        *error = "vswhere found no Visual Studio installation with the C++ toolset "
                 "(Microsoft.VisualStudio.Component.VC.Tools.x86.x64)";
        return "";
    }
    return "call \"" + vcvars.string() + "\" >nul && ";
}

// Runs nmake in `dir`. cmd.exe strips the outermost pair of quotes of a /c
// command line that holds more than two, which is why the whole line is quoted
// once more.
bool runBuild(const fs::path& dir) {
    std::string error;
    const std::string setup = developerEnvironmentSetup(&error);
    if (!error.empty()) {
        std::cerr << "protoscalac: --build-so needs the Microsoft C++ toolset: " << error
                  << ".\n  Run protoscalac from a Developer Command Prompt for Visual Studio, "
                     "or install\n  Visual Studio (or its Build Tools) with the C++ workload.\n";
        return false;
    }
    const std::string cmd = "\"cd /d \"" + fs::absolute(dir).string() + "\" && " + setup +
                            "nmake /nologo\"";
    if (std::system(cmd.c_str()) != 0) {
        std::cerr << "protoscalac: nmake failed\n";
        return false;
    }
    return true;
}
#else
bool runBuild(const fs::path& dir) {
    const std::string cmd = "make -C " + dir.string();
    if (std::system(cmd.c_str()) != 0) {
        std::cerr << "protoscalac: make failed\n";
        return false;
    }
    return true;
}
#endif

void usage() {
    std::cerr
        << "usage: protoscalac <file.scala> [options]\n"
        << "\n"
        << "  --emit-cpp              generate C++ source only (the default)\n"
        << "  --emit-make             also write a Makefile (NMake syntax on Windows)\n"
        << "  --build-so              write the Makefile and run make (nmake on Windows)\n"
        << "  --as-module             module mode: no proto_module_main\n"
        << "  --as-script             script mode: emit proto_module_main\n"
        << "  --module-name <dotted>  the logical path the module declares\n"
        << "  --module-version <v>    the version component of the identity (default: none)\n"
        << "  --report-purity         print what the module needs and why, and exit\n"
        << "  -o <dir>                write the output there (default: the working directory)\n"
        << "\n"
        << "Without --as-module or --as-script the mode is chosen from the unit: script when\n"
        << "it declares an @main, module otherwise.\n";
}

struct Options {
    fs::path source;
    fs::path outDir = ".";
    std::string moduleName;
    std::string moduleVersion;
    bool emitMake = false;
    bool buildSo = false;
    bool forceModule = false;
    bool forceScript = false;
    bool reportPurity = false;
};

int transpile(const Options& opt) {
    std::string source;
    {
        std::ifstream in(opt.source, std::ios::binary);
        if (!in) {
            std::cerr << "protoscalac: cannot read " << opt.source.string() << '\n';
            return 1;
        }
        std::ostringstream buf;
        buf << in.rdbuf();
        source = buf.str();
    }

    // An `import` is refused, and the check is on the SOURCE rather than on the
    // bytecode, deliberately. An import is resolved at transpile time by loading
    // (D90) and leaves no trace of itself in the emitted code: the imported names
    // become ordinary global keys that the transpile-time loader filled. A
    // generated module would push those globals at load time with nothing having
    // filled them, which is a wrong answer rather than an error -- exactly what §D5
    // forbids. Re-performing the import at load time is Task 9 Step 4 and is not in
    // this cut, so the unit is refused here.
    //
    // The scan is an over-approximation: a line beginning with `import` inside a
    // triple-quoted string would be refused too. That is the safe direction, and it
    // is recorded in the specification, as the `await` name check is.
    {
        std::istringstream lines(source);
        std::string l;
        int n = 0;
        while (std::getline(lines, l)) {
            ++n;
            std::size_t i = l.find_first_not_of(" \t");
            if (i == std::string::npos) continue;
            if (l.compare(i, 7, "import ") == 0) {
                std::fprintf(stderr,
                             "%s:%d: error: an import is not supported by protoscalac yet "
                             "(D123): an import is resolved at transpile time by loading, and a "
                             "generated module does not re-perform it at load time\n",
                             opt.source.string().c_str(), n);
                return 1;
            }
        }
    }

    const fs::path abs = fs::absolute(opt.source).lexically_normal();
    const std::string logical =
        opt.moduleName.empty() ? abs.stem().string() : opt.moduleName;

    // A Session, per §D7: the imports of the unit must resolve exactly as they do
    // in the interpreter, which means loading them, which needs a ProtoSpace.
    Session session;
    CompiledUnit cu;
    bool asScript = opt.forceScript;
    try {
        std::unique_ptr<CompilationUnit> unit = parseSource(source);
        // The mode decides which desugaring runs, and the unit's own @main decides
        // the mode when neither option was given (§D8).
        if (!opt.forceModule && !opt.forceScript) {
            // The script desugaring runs and the unit's own @main decides the mode
            // (D8): a unit that declares one is a script, one that does not is a
            // module. `desugarModule` refuses an @main itself (D91), so trying the
            // script form first cannot hide that error -- it reports it later, with
            // its own wording, when --as-module is given explicitly.
            desugar(*unit);
            Compiler compiler(session.globals());
            compiler.setModuleLoader(&session.moduleLoader());
            compiler.setSourceDir(abs.parent_path().string());
            cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
            // An `object Main extends App` unit is a PROGRAM even though it declares
            // no @main (D104), so it is a script: module mode would emit no entry
            // point and the module would run its top level and print nothing.
            asScript = !cu.mainName.empty() || !cu.appKey.empty();
        } else if (opt.forceScript) {
            desugar(*unit);
            Compiler compiler(session.globals());
            compiler.setModuleLoader(&session.moduleLoader());
            compiler.setSourceDir(abs.parent_path().string());
            cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        } else {
            // A module is compiled against the PRELUDE-ONLY table, exactly as
            // Session::loadModuleFile does: it sees the prelude and nothing of any
            // importer, and the copy shares the key counters (A0-16).
            desugarModule(*unit, logical);
            GlobalTable table = session.moduleGlobals();
            Compiler compiler(table);
            compiler.setModuleLoader(&session.moduleLoader());
            compiler.setSourceDir(abs.parent_path().string());
            cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        }
    } catch (const ParseError& e) {
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", opt.source.string().c_str(), e.pos.line,
                     e.pos.column, e.what());
        return 1;
    } catch (const CompileError& e) {
        std::fprintf(stderr, "%s:%d:%d: error: %s\n", opt.source.string().c_str(), e.pos.line,
                     e.pos.column, e.what());
        return 1;
    } catch (const ScalaError& e) {
        std::fprintf(stderr, "%s: error: %s\n", opt.source.string().c_str(), e.what());
        return 1;
    }

    if (opt.reportPurity) {
        const std::vector<std::string> why = CppEmitter::purityReport(cu);
        if (why.empty()) {
            std::cout << opt.source.string() << " needs nothing of protoScala.\n"
                      << "verdict: protoCore-pure (emission under --pure is not offered; see D2)\n";
            return 0;
        }
        std::cout << opt.source.string() << " needs protoScala:\n";
        for (const std::string& line : why) std::cout << line << '\n';
        std::cout << "verdict: not protoCore-pure. Loading this module adds a protoScala runtime "
                     "and\ntherefore one ProtoSpace term to the process sizing rule (protoCore "
                     "MemoryModel.md).\n";
        return 0;
    }

    EmitOptions eo;
    eo.sourcePath = abs.string();
    eo.logicalPath = logical;
    eo.moduleVersion = opt.moduleVersion;
    eo.asScript = asScript;
    eo.appKey = cu.appKey;

    // check() walks first and emits NOTHING: a partially written .cpp that a
    // later `make` compiles into something is worse than no output (§D5).
    {
        std::ostringstream devnull;
        CppEmitter probe(devnull, eo);
        const std::vector<Refusal> refusals = probe.check(cu);
        if (!refusals.empty()) {
            for (const Refusal& r : refusals)
                std::fprintf(stderr, "%s:%d: error: %s\n", opt.source.string().c_str(), r.line,
                             r.message.c_str());
            return 1;
        }
    }

    std::error_code ec;
    fs::create_directories(opt.outDir, ec);
    const fs::path outCpp = opt.outDir / (abs.stem().string() + ".cpp");
    {
        std::ofstream o(outCpp, std::ios::binary);
        if (!o) {
            std::cerr << "protoscalac: cannot write " << outCpp.string() << '\n';
            return 1;
        }
        CppEmitter emitter(o, eo);
        try {
            if (!emitter.emit(cu, session.globals())) {
                std::cerr << "protoscalac: writing " << outCpp.string() << " failed\n";
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "protoscalac: " << e.what() << '\n';
            return 1;
        }
    }

    if (!opt.emitMake && !opt.buildSo) return 0;
    const Toolchain tc = resolveToolchain("protoscalac");
    if (!generateMakefile(opt.outDir, {outCpp}, tc)) return 1;
    if (!opt.buildSo) return 0;

    return runBuild(opt.outDir) ? 0 : 1;
}

int run(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "protoscalac: " << what << " needs a value\n";
                std::exit(1);
            }
            return argv[++i];
        };
        if (a == "--emit-cpp") continue;
        else if (a == "--emit-make") opt.emitMake = true;
        else if (a == "--build-so") { opt.emitMake = true; opt.buildSo = true; }
        else if (a == "--as-module") opt.forceModule = true;
        else if (a == "--as-script") opt.forceScript = true;
        else if (a == "--report-purity") opt.reportPurity = true;
        else if (a == "--module-name") opt.moduleName = next("--module-name");
        else if (a == "--module-version") opt.moduleVersion = next("--module-version");
        else if (a == "-o") opt.outDir = next("-o");
        else if (a == "--version") {
            std::cout << "protoscalac " << versionString() << '\n';
            return 0;
        } else if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (!a.empty() && a[0] == '-') {
            std::cerr << "protoscalac: unknown option '" << a << "'\n";
            usage();
            return 1;
        } else if (opt.source.empty()) opt.source = a;
        else {
            std::cerr << "protoscalac: more than one source path given ('" << opt.source.string()
                      << "' and '" << a << "')\n";
            return 1;
        }
    }
    if (opt.source.empty()) { usage(); return 1; }
    if (opt.forceModule && opt.forceScript) {
        std::cerr << "protoscalac: --as-module and --as-script are mutually exclusive\n";
        return 1;
    }
    std::error_code ec;
    if (!fs::exists(opt.source, ec)) {
        std::cerr << "protoscalac: cannot read " << opt.source.string() << '\n';
        return 1;
    }
    return transpile(opt);
}

struct Job { int argc; char** argv; int rc; };

int jobEntry(void* p) {
    Job* j = static_cast<Job*>(p);
    j->rc = run(j->argc, j->argv);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    // The same deep stack the interpreter gives its evaluator: the parser, the
    // desugarer and the compiler all check the native stack guard, and a deeply
    // nested source file must report StackOverflowError rather than crash.
    Job job{argc, argv, 0};
    runOnEvaluatorThread(&jobEntry, &job);
    return job.rc;
}
