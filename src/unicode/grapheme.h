// NextQt · 字素簇分段（UAX #29）与 Unicode 表访问
//
// 覆盖规则：GB1–GB5、GB6–GB8（Hangul）、GB9/9a/9b、GB9c（Indic conjunct）、
// GB11（emoji ZWJ 序列）、GB12/GB13（区域旗帜）、GB999。
// 表由 tools/ucdgen 生成（Unicode 16.0.0），运行时零依赖。
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "base/utf8.h"
#include "unicode/unicode_tables.h"

namespace nq::unicode {

// 码点级：返回每个字素簇的起始码点索引（含 0 与 count）
std::vector<uint32_t> grapheme_boundaries_cp(const char32_t* cps, size_t count);

// UTF-8 级：返回字节偏移边界（含 0 与 size）
std::vector<uint32_t> grapheme_boundaries(std::string_view utf8);

// 从 byte_offset（必须是边界）出发，返回下一个边界；越界返回 utf8.size()
size_t next_grapheme_boundary(std::string_view utf8, size_t byte_offset);

// 字素簇数量
size_t grapheme_count(std::string_view utf8);

}  // namespace nq::unicode
