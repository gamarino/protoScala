#include "frontend/Linearizer.h"

#include <unordered_set>

namespace protoScala {

std::vector<std::string> linearize(const std::string& self,
                                   const std::vector<std::vector<std::string>>& parents) {
    std::vector<std::string> acc;
    if (!parents.empty()) acc = parents.front();
    for (std::size_t i = 1; i < parents.size(); ++i) {
        const std::unordered_set<std::string> right(acc.begin(), acc.end());
        std::vector<std::string> merged;
        for (const std::string& x : parents[i])
            if (!right.count(x)) merged.push_back(x);
        merged.insert(merged.end(), acc.begin(), acc.end());
        acc = std::move(merged);
    }
    std::vector<std::string> out;
    out.reserve(acc.size() + 1);
    out.push_back(self);
    for (std::string& x : acc)
        if (x != self) out.push_back(std::move(x));
    return out;
}

} // namespace protoScala
