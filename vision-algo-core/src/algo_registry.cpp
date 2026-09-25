// algo_registry.cpp — 算法注册表（线程安全单例）
#include "algo_base.h"
#include <mutex>

namespace vz {

namespace {
std::vector<AlgoRegistryEntry>& registry_storage() {
    static std::vector<AlgoRegistryEntry> r;
    return r;
}

std::mutex& registry_lock() {
    static std::mutex m;
    return m;
}
} // namespace

bool registerAlgo(AlgoRegistryEntry entry) {
    std::lock_guard<std::mutex> lock(registry_lock());
    for (const auto& e : registry_storage()) {
        if (e.name == entry.name) return false; // 已注册
    }
    registry_storage().push_back(std::move(entry));
    return true;
}

const AlgoRegistryEntry* findAlgo(const std::string& name) {
    std::lock_guard<std::mutex> lock(registry_lock());
    for (const auto& e : registry_storage()) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

const std::vector<AlgoRegistryEntry>& allAlgos() {
    return registry_storage();
}

} // namespace vz
