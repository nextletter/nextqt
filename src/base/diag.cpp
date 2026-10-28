#include "base/diag.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <memory>

namespace nq {

void assert_fail(const char* cond, const char* file, int line) noexcept {
  std::fprintf(stderr, "[nq] assert failed: %s\n      at %s:%d\n", cond, file, line);
  std::fflush(stderr);
  std::abort();
}

namespace diag {
namespace {

struct Registry {
  std::mutex mutex;
  std::map<std::string, std::unique_ptr<Counter>> counters;
  std::map<std::string, std::string> units;
};

Registry& registry() {
  static Registry* r = new Registry();  // 有意泄漏：避免静态析构顺序问题
  return *r;
}

}  // namespace

Counter& counter(std::string_view name, std::string_view unit) {
  Registry& reg = registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  std::string key(name);
  auto it = reg.counters.find(key);
  if (it != reg.counters.end()) return *it->second;
  auto owned = std::make_unique<Counter>();
  Counter* raw = owned.get();
  reg.counters.emplace(key, std::move(owned));
  reg.units.emplace(key, std::string(unit));
  return *raw;
}

std::vector<Snapshot> snapshot() {
  Registry& reg = registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  std::vector<Snapshot> out;
  out.reserve(reg.counters.size());
  for (const auto& [name, counter_ptr] : reg.counters) {  // std::map 已有序
    out.push_back({name, reg.units[name], counter_ptr->value()});
  }
  return out;
}

std::string dump_json() {
  const std::vector<Snapshot> snap = snapshot();
  std::string out = "{\n";
  for (size_t i = 0; i < snap.size(); ++i) {
    out += "  \"";
    out += snap[i].name;
    out += "\": {\"value\": ";
    out += std::to_string(snap[i].value);
    out += ", \"unit\": \"";
    out += snap[i].unit;
    out += "\"}";
    out += (i + 1 == snap.size()) ? "\n" : ",\n";
  }
  out += "}\n";
  return out;
}

void reset_all() {
  Registry& reg = registry();
  const std::lock_guard<std::mutex> lock(reg.mutex);
  for (auto& [name, counter_ptr] : reg.counters) counter_ptr->reset();
}

}  // namespace diag
}  // namespace nq
