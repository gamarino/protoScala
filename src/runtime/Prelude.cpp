#include "runtime/Prelude.h"
#include "compiler/Compiler.h"
#include "frontend/Desugar.h"
#include "frontend/Parser.h"
#include "runtime/Errors.h"
#include "runtime/ExecutionEngine.h"

#include <stdexcept>
#include <string>

namespace protoScala {

void loadPrelude(proto::ProtoContext* parent, ExecutionEngine& engine, GlobalTable& globals,
                 std::vector<std::unique_ptr<BytecodeModule>>& modules) {
    auto where = [](SourcePos p) {
        return " (lib/prelude.scala:" + std::to_string(p.line) + ":" + std::to_string(p.column) + ")";
    };
    try {
        auto unit = parseSource(preludeSource());
        desugar(*unit);
        Compiler compiler(globals);
        CompiledUnit cu = compiler.compileUnit(*unit, UnitMode::Script, 0);
        cu.module->linkSymbols(parent);
        const BytecodeModule& mod = *cu.module;
        modules.push_back(std::move(cu.module));
        engine.run(parent, mod);
    } catch (const ParseError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what() + where(e.pos));
    } catch (const CompileError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what() + where(e.pos));
    } catch (const ScalaError& e) {
        throw std::logic_error(std::string("prelude: ") + e.what());
    }
}

} // namespace protoScala
