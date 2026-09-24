#include "repl/ModuleTable.h"

#include <stdexcept>

namespace protoScala {

const LoadedModule* ModuleTable::find(const std::string& absPath) const {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = loaded_.find(absPath);
    return it == loaded_.end() ? nullptr : &it->second;
}

bool ModuleTable::claim(const std::string& absPath, const std::string& logicalPath) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it = loading_.find(absPath);
    if (it == loading_.end()) {
        loading_.emplace(absPath, std::this_thread::get_id());
        return true;
    }
    // This thread is already loading it: the import graph has a cycle, and
    // waiting would deadlock against ourselves.
    if (it->second == std::this_thread::get_id())
        throw std::runtime_error("cyclic module import: " + logicalPath);
    return false;  // another thread holds it; the caller waits
}

void ModuleTable::publish(const std::string& absPath, LoadedModule m) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        loaded_[absPath] = std::move(m);
        loading_.erase(absPath);
    }
    cv_.notify_all();
}

void ModuleTable::abandon(const std::string& absPath) {
    {
        std::lock_guard<std::mutex> lock(mu_);
        loading_.erase(absPath);
    }
    cv_.notify_all();
}

const LoadedModule* ModuleTable::awaitLoaded(const std::string& absPath) {
    std::unique_lock<std::mutex> lock(mu_);
    cv_.wait(lock, [&] { return loading_.find(absPath) == loading_.end(); });
    auto it = loaded_.find(absPath);
    return it == loaded_.end() ? nullptr : &it->second;
}

} // namespace protoScala
