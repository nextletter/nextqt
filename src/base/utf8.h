// NextQt · UTF-8 编解码（无异常、无 locale 依赖、非法序列有确定行为）
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nq {

inline constexpr char32_t kReplacementChar = 0xFFFD;

// 解码一个码点；返回码点，*index 前进。非法序列返回 U+FFFD 并前进 1 字节。
inline char32_t utf8_decode(std::string_view text, size_t* index) noexcept {
  const size_t i = *index;
  if (i >= text.size()) return 0;
  const auto b0 = static_cast<uint8_t>(text[i]);
  if (b0 < 0x80) {
    *index = i + 1;
    return b0;
  }
  int extra = 0;
  char32_t cp = 0;
  if ((b0 & 0xE0) == 0xC0) {
    extra = 1;
    cp = b0 & 0x1F;
  } else if ((b0 & 0xF0) == 0xE0) {
    extra = 2;
    cp = b0 & 0x0F;
  } else if ((b0 & 0xF8) == 0xF0) {
    extra = 3;
    cp = b0 & 0x07;
  } else {
    *index = i + 1;
    return kReplacementChar;
  }
  if (i + static_cast<size_t>(extra) >= text.size()) {
    *index = i + 1;
    return kReplacementChar;
  }
  for (int k = 1; k <= extra; ++k) {
    const auto b = static_cast<uint8_t>(text[i + static_cast<size_t>(k)]);
    if ((b & 0xC0) != 0x80) {
      *index = i + 1;
      return kReplacementChar;
    }
    cp = (cp << 6) | (b & 0x3F);
  }
  static constexpr char32_t kMinForLength[4] = {0, 0x80, 0x800, 0x10000};
  if (cp < kMinForLength[extra] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
    *index = i + 1;
    return kReplacementChar;
  }
  *index = i + static_cast<size_t>(extra) + 1;
  return cp;
}

// 编码到 buf（至少 4 字节），返回写入字节数
inline size_t utf8_encode(char32_t cp, char* buf) noexcept {
  if (cp < 0x80) {
    buf[0] = static_cast<char>(cp);
    return 1;
  }
  if (cp < 0x800) {
    buf[0] = static_cast<char>(0xC0 | (cp >> 6));
    buf[1] = static_cast<char>(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    buf[0] = static_cast<char>(0xE0 | (cp >> 12));
    buf[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    buf[2] = static_cast<char>(0x80 | (cp & 0x3F));
    return 3;
  }
  buf[0] = static_cast<char>(0xF0 | (cp >> 18));
  buf[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
  buf[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
  buf[3] = static_cast<char>(0x80 | (cp & 0x3F));
  return 4;
}

inline std::string utf8_encode_string(char32_t cp) {
  char buf[4];
  const size_t n = utf8_encode(cp, buf);
  return std::string(buf, n);
}

// 遍历码点（只读）
class Utf8Iterator {
 public:
  explicit Utf8Iterator(std::string_view text) : text_(text) {}
  [[nodiscard]] bool done() const noexcept { return index_ >= text_.size(); }
  char32_t next() noexcept { return utf8_decode(text_, &index_); }
  [[nodiscard]] size_t byte_index() const noexcept { return index_; }

 private:
  std::string_view text_;
  size_t index_ = 0;
};

}  // namespace nq
