// NextQt · golden 图像对比（自研，无第三方）
#pragma once

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "nqtest.h"
#include "raster/surface.h"

namespace nqtest {

inline bool read_file(const std::string& path, std::vector<uint8_t>* out) {
  std::FILE* f = std::fopen(path.c_str(), "rb");
  if (f == nullptr) return false;
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  if (size < 0) {
    std::fclose(f);
    return false;
  }
  out->resize(static_cast<size_t>(size));
  const size_t read = std::fread(out->data(), 1, out->size(), f);
  std::fclose(f);
  return read == out->size();
}

inline bool write_file(const std::string& path, const std::vector<uint8_t>& data) {
  std::FILE* f = std::fopen(path.c_str(), "wb");
  if (f == nullptr) return false;
  const size_t written = std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
  return written == data.size();
}

// 渲染结果与 tests/golden/<name>.ppm 逐字节比较；--update-golden 时覆盖
inline void expect_golden(const nq::Surface& surface, const char* name) {
  const std::string golden_path = std::string(NQ_GOLDEN_DIR) + "/" + name + ".ppm";
  const std::string actual_path = std::string(NQ_BUILD_DIR) + "/actual_" + name + ".ppm";
  if (!nq::write_ppm(surface, actual_path.c_str())) {
    report_failure(__FILE__, __LINE__, std::string("cannot write ") + actual_path);
    return;
  }
  std::vector<uint8_t> actual;
  if (!read_file(actual_path, &actual)) {
    report_failure(__FILE__, __LINE__, std::string("cannot read ") + actual_path);
    return;
  }
  if (update_golden()) {
    if (!write_file(golden_path, actual)) {
      report_failure(__FILE__, __LINE__, std::string("cannot write ") + golden_path);
    } else {
      std::printf("  [golden updated] %s\n", golden_path.c_str());
    }
    return;
  }
  std::vector<uint8_t> expected;
  if (!read_file(golden_path, &expected)) {
    report_failure(__FILE__, __LINE__,
                   std::string("missing golden ") + golden_path +
                       " (run with --update-golden after reviewing " + actual_path + ")");
    return;
  }
  if (expected == actual) return;
  size_t first_diff = 0;
  while (first_diff < expected.size() && first_diff < actual.size() &&
         expected[first_diff] == actual[first_diff]) {
    ++first_diff;
  }
  report_failure(__FILE__, __LINE__,
                 std::string("golden mismatch: ") + name + " (expected " +
                     std::to_string(expected.size()) + " bytes, actual " +
                     std::to_string(actual.size()) + ", first diff at byte " +
                     std::to_string(first_diff) + ")");
}

}  // namespace nqtest
