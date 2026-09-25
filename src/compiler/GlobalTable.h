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
#include "compiler/ClassInfo.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace protoScala {

// An `Object` binding holds the lazy singleton holder of an `object`: reads
// FORCE it like a lazy val.
enum class BindingKind : uint8_t {
    Val, Var, LazyVal, Def, ParamlessDef, Builtin, Param, Object,
    // A parameter declared `x: => T`: the slot holds a zero-argument thunk and
    // every read of the name forces it (D47).
    ByNameParam
};

struct GlobalBinding {
    BindingKind kind;
    std::string key;  // name of the runtime global
    // One mask per parameter list: bit k is set when argument k of that list is
    // by-name, so a call site that resolves to this binding wraps the argument
    // in a thunk (D47). For a `def` the masks come from its parameter lists;
    // for a builtin object they describe its `apply`.
    std::vector<std::uint32_t> byNameMasks;
};

class GlobalTable {
public:
    // Starts a compilation unit: names declared from now on shadow the
    // bindings of earlier units.
    void beginUnit() {
        declaredInUnit_.clear();
        typesDeclaredInUnit_.clear();
    }

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
        b = GlobalBinding{kind, std::move(key), {}};
        return b.key;
    }

    // Binds `name` to an EXISTING binding, rather than allocating a key for
    // it. An import does this: the module's global already exists under its
    // own key and the importing unit only needs the name to reach it.
    // Deliberately does not touch `counters_`: the key was allocated by the
    // module's own table, which shares those counters (Phase 6 A0-16), so no
    // second allocation can produce it.
    void bind(const std::string& name, const GlobalBinding& b) {
        declaredInUnit_.insert(name);
        table_[name] = b;
        // The key exists already; make sure a LATER declaration of the same
        // name in this table shadows it rather than re-issuing the same key.
        // Without this, a prelude binding installed from the image (which never
        // goes through declare()) would leave the counter unset, and a REPL
        // redefinition of a prelude name would be handed the prelude's own key.
        reserveKey(*counters_, name, b.key);
    }

    // The names this unit declared. A module's exports are the difference
    // between its table and the prelude snapshot it was copied from.
    const std::unordered_set<std::string>& declaredInUnit() const { return declaredInUnit_; }

    // Records which arguments of a call to `name` are by-name (D47). Called
    // right after declare(), which resets the masks, so a redefinition that
    // drops the by-name parameter drops the wrapping with it.
    void setByNameMasks(const std::string& name, std::vector<std::uint32_t> masks) {
        auto it = table_.find(name);
        if (it == table_.end()) return;
        noteByNameSelector(name, masks);
        it->second.byNameMasks = std::move(masks);
    }

    std::uint32_t byNameMaskOf(const std::string& name, std::size_t list = 0) const {
        const GlobalBinding* b = binding(name);
        if (!b || list >= b->byNameMasks.size()) return 0;
        return b->byNameMasks[list];
    }

    // --- Conservative selector index (CaptureAnalysis) ---------------------
    // Boxing is decided by a pre-pass that runs before any name is resolved, so
    // it cannot tell which declaration a call reaches. It asks this index
    // instead: the union of every by-name mask ever declared under a selector
    // (a def name, a method name or a class name for its constructor). It only
    // ever over-approximates, and an extra Cell is harmless where a missing one
    // would not be.
    void noteByNameSelector(const std::string& selector,
                            const std::vector<std::uint32_t>& masks) {
        std::uint32_t any = 0;
        for (std::uint32_t m : masks) any |= m;
        if (any) (*byNameSelectors_)[selector] |= any;
    }

    std::uint32_t anyByNameMaskOf(const std::string& selector) const {
        auto it = byNameSelectors_->find(selector);
        return it == byNameSelectors_->end() ? 0 : it->second;
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

    // --- Type namespace (classes, traits, the classes of objects) ---------
    // Declares (or, within one unit, redeclares) a type name; returns its key:
    // "@Name" for the first definition, "@Name#N" for a definition that
    // shadows one from an earlier unit (the REPL rule of term keys, D25).
    const std::string& declareType(const std::string& name) {
        if (!typesDeclaredInUnit_.count(name)) {
            typesDeclaredInUnit_.insert(name);
            auto [counter, fresh] = typeCounters_->try_emplace(name, 0);
            typeKeyOfName_[name] =
                fresh ? "@" + name : "@" + name + "#" + std::to_string(++counter->second);
        }
        return typeKeyOfName_.at(name);
    }
    // Makes `name` resolve to an existing type key (an imported type). The key
    // was allocated by the module's own table, which shares `typeCounters_`.
    void aliasType(const std::string& name, const std::string& key) {
        typesDeclaredInUnit_.insert(name);
        typeKeyOfName_[name] = key;
        reserveKey(*typeCounters_, name, key.empty() ? key : key.substr(1));
    }
    // The type names this unit declared (see declaredInUnit()).
    const std::unordered_set<std::string>& typesDeclaredInUnit() const {
        return typesDeclaredInUnit_;
    }
    // Records the description of a declared type (info.key from declareType).
    void defineType(ClassInfo info) {
        const std::string key = info.key;
        typesByKey_[key] = std::move(info);
    }
    // A runtime-provided type: its name resolves to its fixed key.
    void defineBuiltinType(ClassInfo info) {
        typeKeyOfName_[info.name] = info.key;
        defineType(std::move(info));
    }
    const ClassInfo* findType(const std::string& name) const {
        auto it = typeKeyOfName_.find(name);
        return it == typeKeyOfName_.end() ? nullptr : findTypeByKey(it->second);
    }
    // Every type ever defined stays reachable by key: the linearizations of
    // classes compiled earlier name the keys of their (possibly shadowed) parents.
    const ClassInfo* findTypeByKey(const std::string& key) const {
        auto it = typesByKey_.find(key);
        return it == typesByKey_.end() ? nullptr : &it->second;
    }
    ClassInfo* mutableTypeByKey(const std::string& key) {
        auto it = typesByKey_.find(key);
        return it == typesByKey_.end() ? nullptr : &it->second;
    }

    // --- Prefixed names, for a member import (Track X) --------------------
    // A template nested in an `object` is lifted to a top-level definition with
    // a dotted name (`Color.Red`), so the members an `object` or `enum`
    // contributes are partly its ClassInfo's members and partly these. Both
    // helpers answer the names exactly ONE level under `prefix` (which must end
    // in '.'), as `<simple name>, <value>`: `Color.Red` under `Color.` is `Red`,
    // and `Color.Red.Inner` is not answered at all, staying qualified as it does
    // in Scala. A type's `.type` suffix is not a level: `Color.Red.type` is
    // answered as `Red.type`, so the class of the nested object stays reachable.
    std::vector<std::pair<std::string, const GlobalBinding*>> bindingsUnder(
        const std::string& prefix) const {
        std::vector<std::pair<std::string, const GlobalBinding*>> out;
        for (const auto& kv : table_) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
            const std::string rest = kv.first.substr(prefix.size());
            if (rest.empty() || rest.find('.') != std::string::npos) continue;
            out.emplace_back(rest, &kv.second);
        }
        return out;
    }

    std::vector<std::pair<std::string, std::string>> typesUnder(
        const std::string& prefix) const {
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto& kv : typeKeyOfName_) {
            if (kv.first.compare(0, prefix.size(), prefix) != 0) continue;
            std::string rest = kv.first.substr(prefix.size());
            const bool dotType = rest.size() > 5 &&
                                 rest.compare(rest.size() - 5, 5, ".type") == 0;
            const std::string head = dotType ? rest.substr(0, rest.size() - 5) : rest;
            if (head.empty() || head.find('.') != std::string::npos) continue;
            out.emplace_back(rest, kv.second);
        }
        return out;
    }

    // --- Whole-table access, for the prelude image and its generator -------
    // `protoscala-precompile` writes these out and buildPreludeImage replays
    // them, so a session that takes the image path ends up with exactly the
    // table the compiler produced. Nothing else should need them.
    const std::unordered_map<std::string, GlobalBinding>& allBindings() const { return table_; }
    const std::unordered_map<std::string, ClassInfo>& allTypes() const { return typesByKey_; }
    const std::unordered_map<std::string, std::string>& typeKeys() const { return typeKeyOfName_; }
    const std::unordered_map<std::string, std::uint32_t>& byNameSelectors() const {
        return *byNameSelectors_;
    }

private:
    // Records that `key` has been handed out for `name`, so the next declare()
    // of that name allocates a shadowing key instead of the same one. `key` is
    // either `name` or `name#N`.
    static void reserveKey(std::unordered_map<std::string, int>& counters,
                           const std::string& name, const std::string& key) {
        auto [it, fresh] = counters.try_emplace(name, 0);
        (void)fresh;
        const auto hash = key.rfind('#');
        if (hash == std::string::npos || key.compare(0, hash, name) != 0) return;
        int n = 0;
        for (std::size_t k = hash + 1; k < key.size(); ++k) {
            if (key[k] < '0' || key[k] > '9') return;
            n = n * 10 + (key[k] - '0');
        }
        if (n > it->second) it->second = n;
    }

    std::unordered_map<std::string, GlobalBinding> table_;
    std::unordered_set<std::string> declaredInUnit_;
    // Per name: how many shadowing keys were handed out (shared by copies).
    std::shared_ptr<std::unordered_map<std::string, int>> counters_ =
        std::make_shared<std::unordered_map<std::string, int>>();

    std::unordered_map<std::string, std::string> typeKeyOfName_;  // name -> current type key
    std::unordered_map<std::string, ClassInfo> typesByKey_;
    std::unordered_set<std::string> typesDeclaredInUnit_;
    std::shared_ptr<std::unordered_map<std::string, int>> typeCounters_ =
        std::make_shared<std::unordered_map<std::string, int>>();
    // Shared by every copy of the table and never pruned: a rolled-back REPL
    // line may leave a selector behind, which only over-boxes.
    std::shared_ptr<std::unordered_map<std::string, std::uint32_t>> byNameSelectors_ =
        std::make_shared<std::unordered_map<std::string, std::uint32_t>>();
};

} // namespace protoScala
