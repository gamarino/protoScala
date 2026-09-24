/*
 * The four family prefixes of INTEROP §3. This list is CLOSED on purpose: if any
 * registered provider alias could be a prefix, `import util.Strings` would become
 * hijackable by a plug-in aliased "util", and a program's meaning must not depend
 * on which plug-ins are installed (Phase 6 plan A0-4).
 *
 * A fifth runtime needs one line here, which is a deliberate, reviewable edit.
 */
#pragma once
#include <string>

namespace protoScala {

inline constexpr const char* kFamilyPrefixes[] = {"py", "js", "st", "clj"};

inline bool isFamilyPrefix(const std::string& s) {
    for (const char* p : kFamilyPrefixes)
        if (s == p) return true;
    return false;
}

inline std::string providerSpecFor(const std::string& prefix) { return "provider:" + prefix; }

// The alias inside a "provider:<alias>" spec, for a message.
inline std::string aliasOfSpec(const std::string& spec) {
    static const std::string kPrefix = "provider:";
    return spec.compare(0, kPrefix.size(), kPrefix) == 0 ? spec.substr(kPrefix.size()) : spec;
}

} // namespace protoScala
