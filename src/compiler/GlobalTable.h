// GlobalTable.h — compile-time view of module / session globals.
//
// Every global definition gets a key: the name of the runtime global that
// holds its value. The first definition of a name uses the name itself; a
// definition that shadows a binding from an earlier unit (a REPL
// redefinition) gets a fresh key `name#N`, so code compiled earlier keeps
// reading the binding it saw, as in the Scala REPL. `#` followed by digits
// never occurs in a Scala identifier, so a key never collides with a name.
// Within one unit a repeated definition reuses the unit's key (D25). Keys
// are never reused, even when a unit that allocated one is rolled back: the
// key counters are shared by every copy of the table.
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace protoScala {

enum class BindingKind : uint8_t { Val, Var, LazyVal, Def, ParamlessDef, Builtin, Param };

struct GlobalBinding {
    BindingKind kind;
    std::string key;  // name of the runtime global
};

class GlobalTable {
public:
    // Starts a compilation unit: names declared from now on shadow the
    // bindings of earlier units.
    void beginUnit() { declaredInUnit_.clear(); }

    // Declares (or, within one unit, redeclares) a global; returns its key.
    const std::string& declare(const std::string& name, BindingKind kind) {
        if (declaredInUnit_.count(name)) {
            GlobalBinding& b = table_.at(name);
            b.kind = kind;
            return b.key;
        }
        declaredInUnit_.insert(name);
        auto [counter, fresh] = counters_->try_emplace(name, 0);
        std::string key = fresh ? name : name + "#" + std::to_string(++counter->second);
        GlobalBinding& b = table_[name];
        b = GlobalBinding{kind, std::move(key)};
        return b.key;
    }

    std::optional<BindingKind> find(const std::string& name) const {
        const GlobalBinding* b = binding(name);
        if (!b) return std::nullopt;
        return b->kind;
    }

    const GlobalBinding* binding(const std::string& name) const {
        auto it = table_.find(name);
        return it == table_.end() ? nullptr : &it->second;
    }

    // Makes `name` resolve as it does in `other` (or not at all).
    void restore(const std::string& name, const GlobalTable& other) {
        if (const GlobalBinding* b = other.binding(name)) table_[name] = *b;
        else table_.erase(name);
    }

    // The source name of a key: `x#2` -> `x` (for messages).
    static std::string nameOfKey(const std::string& key) {
        const auto hash = key.rfind('#');
        if (hash == std::string::npos || hash + 1 == key.size()) return key;
        for (std::size_t k = hash + 1; k < key.size(); ++k)
            if (key[k] < '0' || key[k] > '9') return key;
        return key.substr(0, hash);
    }

private:
    std::unordered_map<std::string, GlobalBinding> table_;
    std::unordered_set<std::string> declaredInUnit_;
    // Per name: how many shadowing keys were handed out (shared by copies).
    std::shared_ptr<std::unordered_map<std::string, int>> counters_ =
        std::make_shared<std::unordered_map<std::string, int>>();
};

} // namespace protoScala
