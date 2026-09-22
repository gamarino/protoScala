// GlobalTable.h — compile-time view of module / session globals.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace protoScala {

enum class BindingKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef, Builtin, Param };

class GlobalTable {
public:
    // Declares or redeclares (REPL redefinition) a global.
    void declare(const std::string& name, BindingKind kind) { table_[name] = kind; }
    std::optional<BindingKind> find(const std::string& name) const {
        auto it = table_.find(name);
        if (it == table_.end()) return std::nullopt;
        return it->second;
    }
private:
    std::unordered_map<std::string, BindingKind> table_;
};

} // namespace protoScala
