// NextQt · 稳定哈希：跨平台确定（仅整数运算，无长度/字节序依赖）
#pragma once

#include <cstdint>
#include <cstring>
#include <string_view>

namespace nq {

// murmur3 finalizer 变体：足够的雪崩性，整数运算，可移植
constexpr uint64_t mix64(uint64_t x) noexcept {
  x ^= x >> 33;
  x *= 0xff51afd7ed558ccdULL;
  x ^= x >> 33;
  x *= 0xc4ceb9fe1a85ec53ULL;
  x ^= x >> 33;
  return x;
}

inline uint64_t hash_bytes(const void* data, size_t len) noexcept {
  const auto* p = static_cast<const uint8_t*>(data);
  uint64_t h = mix64(0x9e3779b97f4a7c15ULL ^ (static_cast<uint64_t>(len) * 0xff51afd7ed558ccdULL));
  while (len >= 8) {
    uint64_t word = 0;
    std::memcpy(&word, p, 8);  // 小端假设在 x86/arm64/wasm 成立；如需大端支持在此转换
    h = mix64(h ^ word);
    p += 8;
    len -= 8;
  }
  uint64_t tail = 0;
  for (size_t i = 0; i < len; ++i) {
    tail |= static_cast<uint64_t>(p[i]) << (i * 8);
  }
  h = mix64(h ^ tail);
  return h;
}

inline uint64_t hash_bytes(std::string_view s) noexcept {
  return hash_bytes(s.data(), s.size());
}

// 组合哈希（顺序敏感）
constexpr uint64_t hash_combine(uint64_t a, uint64_t b) noexcept {
  return mix64(a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2)));
}

}  // namespace nq
