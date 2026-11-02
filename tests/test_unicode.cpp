// Unicode 一致性测试：直接跑官方的 GraphemeBreakTest（UAX #29）
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "base/diag.h"
#include "base/utf8.h"
#include "nqtest.h"
#include "unicode/grapheme.h"
#include "unicode/unicode_tables.h"

namespace {

using namespace nq;

std::string read_text(const char* path) {
  std::FILE* f = std::fopen(path, "rb");
  if (f == nullptr) return {};
  std::fseek(f, 0, SEEK_END);
  const long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::string out;
  if (size > 0) {
    out.resize(static_cast<size_t>(size));
    const size_t got = std::fread(out.data(), 1, out.size(), f);
    out.resize(got);
  }
  std::fclose(f);
  return out;
}

struct TestCase {
  std::vector<char32_t> cps;
  std::vector<uint32_t> expected;  // 码点索引边界
};

// 解析 "÷ 0041 × 0301 ÷" 形式
std::vector<TestCase> parse_grapheme_test(std::string_view text) {
  std::vector<TestCase> cases;
  size_t pos = 0;
  while (pos < text.size()) {
    const size_t eol = text.find('\n', pos);
    const std::string_view line =
        text.substr(pos, eol == std::string_view::npos ? std::string_view::npos : eol - pos);
    pos = (eol == std::string_view::npos) ? text.size() : eol + 1;

    const size_t hash = line.find('#');
    const std::string_view body = line.substr(0, hash == std::string_view::npos ? line.size()
                                                                                : hash);
    if (body.find("÷") == std::string_view::npos) continue;

    TestCase tc;
    size_t i = 0;
    while (i < body.size()) {
      while (i < body.size() && body[i] == ' ') ++i;
      if (i >= body.size()) break;
      // ÷ 和 × 都是 2 字节（UTF-8）
      if (body.compare(i, 2, "÷") == 0 || body.compare(i, 2, "\xC3\xB7") == 0) {
        tc.expected.push_back(static_cast<uint32_t>(tc.cps.size()));
        i += 2;
        while (i < body.size() && body[i] == ' ') ++i;
        continue;
      }
      if (body.compare(i, 2, "×") == 0 || body.compare(i, 2, "\xC3\x97") == 0) {
        i += 2;
        while (i < body.size() && body[i] == ' ') ++i;
        continue;
      }
      // 十六进制码点
      size_t j = i;
      uint32_t value = 0;
      bool any = false;
      while (j < body.size()) {
        const char c = body[j];
        uint32_t digit = 0;
        if (c >= '0' && c <= '9') {
          digit = static_cast<uint32_t>(c - '0');
        } else if (c >= 'A' && c <= 'F') {
          digit = static_cast<uint32_t>(c - 'A' + 10);
        } else if (c >= 'a' && c <= 'f') {
          digit = static_cast<uint32_t>(c - 'a' + 10);
        } else {
          break;
        }
        value = value * 16 + digit;
        any = true;
        ++j;
      }
      if (any) {
        tc.cps.push_back(static_cast<char32_t>(value));
        i = j;
      } else {
        ++i;
      }
    }
    if (!tc.cps.empty()) cases.push_back(std::move(tc));
  }
  return cases;
}

bool same(const std::vector<uint32_t>& a, const std::vector<uint32_t>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) return false;
  }
  return true;
}

std::string describe(const std::vector<char32_t>& cps) {
  std::string out;
  char buf[8];
  for (char32_t cp : cps) {
    std::snprintf(buf, sizeof(buf), "%04X ", static_cast<unsigned>(cp));
    out += buf;
  }
  return out;
}

}  // namespace

NQ_TEST(unicode_tables_have_expected_values) {
  using namespace unicode;
  NQ_EXPECT_EQ(std::string_view(kUnicodeVersion), std::string_view("16.0.0"));

  NQ_EXPECT(gcb(U'A') == Gcb::kOther);
  NQ_EXPECT(gcb(0x0D) == Gcb::kCR);
  NQ_EXPECT(gcb(0x0A) == Gcb::kLF);
  NQ_EXPECT(gcb(0x0301) == Gcb::kExtend);          // 组合尖音符
  NQ_EXPECT(gcb(0x200D) == Gcb::kZWJ);
  NQ_EXPECT(gcb(0x1F1E6) == Gcb::kRegionalIndicator);  // 区域指示符 A
  NQ_EXPECT(gcb(0x1100) == Gcb::kL);               // Hangul 初声
  NQ_EXPECT(gcb(0x1160) == Gcb::kV);
  NQ_EXPECT(gcb(0x11A8) == Gcb::kT);
  NQ_EXPECT(gcb(0xAC00) == Gcb::kLV);
  NQ_EXPECT(gcb(0xAC01) == Gcb::kLVT);

  NQ_EXPECT(incb(0x0915) == IncB::kConsonant);     // 天城文 KA
  NQ_EXPECT(incb(0x094D) == IncB::kLinker);        // 天城文 VIRAMA
  NQ_EXPECT(incb(U'A') == IncB::kNone);

  NQ_EXPECT(lbc(U'A') == Lb::kAL);
  NQ_EXPECT(lbc(U' ') == Lb::kSP);
  NQ_EXPECT(lbc(0x4E2D) == Lb::kID);               // 中
  NQ_EXPECT(bidi_class(U'A') == Bc::kL);
  NQ_EXPECT(bidi_class(0x05D0) == Bc::kR);         // 希伯来 א
  NQ_EXPECT(bidi_class(0x0660) == Bc::kAN);        // 阿拉伯数字
  NQ_EXPECT(general_category(U'A') == Gc::kLu);
  NQ_EXPECT(general_category(U'a') == Gc::kLl);
  NQ_EXPECT(general_category(U'0') == Gc::kNd);
  NQ_EXPECT(general_category(0x4E2D) == Gc::kLo);

  NQ_EXPECT(is_extended_pictographic(0x1F600));    // 😀
  NQ_EXPECT(!is_extended_pictographic(U'A'));
  NQ_EXPECT(!is_extended_pictographic(0x110000));  // 越界安全

  // 非法码点（代理区）必须回退到 U+FFFD，不越界
  NQ_EXPECT(gcb(0xD800) == gcb(0xFFFD));
  NQ_EXPECT(general_category(0xDFFF) == general_category(0xFFFD));

  // 枚举名称表
  NQ_EXPECT_EQ(std::string_view(gcb_name(Gcb::kZWJ)), std::string_view("ZWJ"));
  NQ_EXPECT_EQ(std::string_view(incb_name(IncB::kLinker)), std::string_view("Linker"));
  NQ_EXPECT_EQ(std::string_view(lb_name(Lb::kAL)), std::string_view("AL"));
}

NQ_TEST(unicode_enum_names_are_complete) {
  using namespace unicode;
  // 逐个取值调用名称表：既验证表完整性，也覆盖生成的 switch 分支
  for (int i = 0; i < 64; ++i) {
    const auto g = static_cast<Gcb>(i);
    const auto in = static_cast<IncB>(i);
    const auto lb = static_cast<Lb>(i);
    const auto bc = static_cast<Bc>(i);
    const auto gc = static_cast<Gc>(i);
    if (i < 14) NQ_EXPECT(std::string_view(gcb_name(g)) != std::string_view("?"));
    if (i < 4) NQ_EXPECT(std::string_view(incb_name(in)) != std::string_view("?"));
    if (i < 48) NQ_EXPECT(std::string_view(lb_name(lb)) != std::string_view("?"));
    if (i < 24) NQ_EXPECT(std::string_view(bc_name(bc)) != std::string_view("?"));
    if (i < 30) NQ_EXPECT(std::string_view(gc_name(gc)) != std::string_view("?"));
    if (i >= 48) {
      // 越界值必须有确定回退，而不是 UB
      NQ_EXPECT_EQ(std::string_view(lb_name(lb)), std::string_view("?"));
      NQ_EXPECT_EQ(std::string_view(gcb_name(static_cast<Gcb>(200))), std::string_view("?"));
    }
  }
}

NQ_TEST(grapheme_ascii_and_empty) {
  using namespace unicode;
  NQ_EXPECT_EQ(grapheme_count(""), 0u);
  NQ_EXPECT_EQ(grapheme_count("abc"), 3u);
  const std::vector<uint32_t> bounds = grapheme_boundaries("abc");
  NQ_EXPECT_EQ(bounds.size(), 4u);
  NQ_EXPECT_EQ(bounds[0], 0u);
  NQ_EXPECT_EQ(bounds[3], 3u);
  NQ_EXPECT_EQ(next_grapheme_boundary("abc", 0), 1u);
  NQ_EXPECT_EQ(next_grapheme_boundary("abc", 2), 3u);
  NQ_EXPECT_EQ(next_grapheme_boundary("abc", 3), 3u);  // 末尾安全
  NQ_EXPECT_EQ(next_grapheme_boundary("abc", 99), 3u);

  const char32_t none[1] = {0};
  const std::vector<uint32_t> empty_bounds = grapheme_boundaries_cp(none, 0);
  NQ_EXPECT_EQ(empty_bounds.size(), 1u);
  NQ_EXPECT_EQ(empty_bounds[0], 0u);
}

NQ_TEST(grapheme_cluster_examples) {
  using namespace unicode;
  // e + 组合尖音符 → 1 个簇
  NQ_EXPECT_EQ(grapheme_count("e\xCC\x81"), 1u);
  // 👨‍👩‍👧 家庭 emoji（ZWJ 序列）→ 1 个簇
  NQ_EXPECT_EQ(grapheme_count("\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D"
                              "\xF0\x9F\x91\xA7"),
               1u);
  // 两面区域旗帜 → 1 个簇（奇数个 RI 断开）
  NQ_EXPECT_EQ(grapheme_count("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3"), 1u);
  // 三面旗帜 → 2 个簇（前两面成对，第三个单独）
  NQ_EXPECT_EQ(grapheme_count("\xF0\x9F\x87\xA8\xF0\x9F\x87\xB3\xF0\x9F\x87\xBA"), 2u);
  // CRLF 是一个簇
  NQ_EXPECT_EQ(grapheme_count("\r\n"), 1u);
  // 控制符两侧都断开
  NQ_EXPECT_EQ(grapheme_count("a\nb"), 3u);
  // 天城文 conjunct：क ् ष 是一个簇（GB9c）
  NQ_EXPECT_EQ(grapheme_count("\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7"), 1u);
  // 韩文音节：L + V → 1 个簇
  NQ_EXPECT_EQ(grapheme_count("\xE1\x84\x80\xE1\x85\xA1"), 1u);
}

NQ_TEST(grapheme_conformance_official) {
  const std::string text = read_text(NQ_UCD_TEST_DIR "/GraphemeBreakTest-16.0.0.txt");
  NQ_EXPECT(!text.empty());
  NQ_EXPECT(text.find("GraphemeBreakTest-16.0.0") != std::string::npos);

  const std::vector<TestCase> cases = parse_grapheme_test(text);
  NQ_EXPECT(cases.size() > 500u);  // 官方文件约 1200 条

  size_t failures = 0;
  std::string first_failure;
  for (const TestCase& tc : cases) {
    const std::vector<uint32_t> actual =
        unicode::grapheme_boundaries_cp(tc.cps.data(), tc.cps.size());
    if (!same(actual, tc.expected)) {
      ++failures;
      if (first_failure.empty()) {
        first_failure = "cps: " + describe(tc.cps) + " expected " +
                        std::to_string(tc.expected.size()) + " bounds, got " +
                        std::to_string(actual.size());
      }
    }
  }
  if (failures != 0) {
    NQ_FAIL("GraphemeBreakTest failures: " + std::to_string(failures) + "/" +
            std::to_string(cases.size()) + "  first: " + first_failure);
  }
  diag::counter("unicode.conformance_cases", "case").add(cases.size());
}

NQ_TEST(grapheme_utf8_boundaries_map_back_to_bytes) {
  using namespace unicode;
  // "aé中" → 1 + 2 + 3 字节
  const std::string text = "a\xC3\xA9\xE4\xB8\xAD";
  const std::vector<uint32_t> bounds = grapheme_boundaries(text);
  NQ_EXPECT_EQ(bounds.size(), 4u);
  NQ_EXPECT_EQ(bounds[0], 0u);
  NQ_EXPECT_EQ(bounds[1], 1u);
  NQ_EXPECT_EQ(bounds[2], 3u);
  NQ_EXPECT_EQ(bounds[3], 6u);
  NQ_EXPECT_EQ(static_cast<size_t>(bounds.back()), text.size());

  // 组合序列在字节层面也只有一个边界
  const std::string combined = "e\xCC\x81" "x";
  const std::vector<uint32_t> cb = grapheme_boundaries(combined);
  NQ_EXPECT_EQ(cb.size(), 3u);
  NQ_EXPECT_EQ(cb[1], 3u);  // e + 组合符占 3 字节
  NQ_EXPECT_EQ(cb[2], 4u);
}
