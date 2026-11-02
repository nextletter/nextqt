#include "unicode/grapheme.h"

#include <vector>

#include "base/diag.h"

namespace nq::unicode {
namespace {

using G = Gcb;
using I = IncB;

[[nodiscard]] constexpr bool is_control(G g) noexcept {
  return g == G::kControl || g == G::kCR || g == G::kLF;
}

// UAX #29 上下文状态：描述"截至前一个码点"的运行状态
struct BreakState {
  G prev = G::kOther;             // 前一个码点的 GCB
  int ri_run = 0;                 // 连续 Regional_Indicator 数量（含 prev）
  int incb_state = 0;             // 0 无；1 Consonant；2 Consonant…Linker…
  bool extpict_run = false;       // 截至 prev 形如 ExtPict Extend*
  bool zwj_after_extpict = false; // prev 是 ZWJ，且其前是 ExtPict Extend*
};

// 判定 prev 与 cur 之间是否断行。cur 的属性由调用方算好。
[[nodiscard]] bool should_break(const BreakState& st, G cur, bool cur_is_extpict,
                                bool cur_is_consonant, bool prev_is_zwj) noexcept {
  // GB3: CR × LF
  if (st.prev == G::kCR && cur == G::kLF) return false;
  // GB4: (Control | CR | LF) ÷
  if (is_control(st.prev)) return true;
  // GB5: ÷ (Control | CR | LF)
  if (is_control(cur)) return true;
  // GB6: L × (L | V | LV | LVT)
  if (st.prev == G::kL && (cur == G::kL || cur == G::kV || cur == G::kLV || cur == G::kLVT)) {
    return false;
  }
  // GB7: (LV | V) × (V | T)
  if ((st.prev == G::kLV || st.prev == G::kV) && (cur == G::kV || cur == G::kT)) return false;
  // GB8: (LVT | T) × T
  if ((st.prev == G::kLVT || st.prev == G::kT) && cur == G::kT) return false;
  // GB9: × (Extend | ZWJ)
  if (cur == G::kExtend || cur == G::kZWJ) return false;
  // GB9a: × SpacingMark
  if (cur == G::kSpacingMark) return false;
  // GB9b: Prepend ×
  if (st.prev == G::kPrepend) return false;
  // GB9c: Consonant [Extend Linker]* Linker [Extend Linker]* × Consonant
  if (cur_is_consonant && st.incb_state == 2) return false;
  // GB11: ExtPict Extend* ZWJ × ExtPict
  if (cur_is_extpict && prev_is_zwj && st.zwj_after_extpict) return false;
  // GB12/GB13: 奇数个 RI 后不断开
  if (st.prev == G::kRegionalIndicator && cur == G::kRegionalIndicator &&
      (st.ri_run % 2) == 1) {
    return false;
  }
  // GB999
  return true;
}

// 用当前码点更新上下文状态
void advance(BreakState* st, G cur, bool cur_is_extpict, char32_t cp) {
  st->prev = cur;

  if (cur == G::kRegionalIndicator) {
    ++st->ri_run;
  } else {
    st->ri_run = 0;
  }

  if (cur == G::kZWJ) {
    st->zwj_after_extpict = st->extpict_run;
    st->extpict_run = false;
  } else {
    st->zwj_after_extpict = false;
    if (cur_is_extpict) {
      st->extpict_run = true;
    } else if (cur != G::kExtend) {
      st->extpict_run = false;
    }
  }

  const I in = incb(cp);
  if (in == I::kConsonant) {
    st->incb_state = 1;
  } else if (st->incb_state != 0 && in == I::kExtend) {
    // 保持状态
  } else if (st->incb_state != 0 && in == I::kLinker) {
    st->incb_state = 2;
  } else {
    st->incb_state = 0;
  }
}

}  // namespace

std::vector<uint32_t> grapheme_boundaries_cp(const char32_t* cps, size_t count) {
  std::vector<uint32_t> boundaries;
  if (count == 0) {
    boundaries.push_back(0);
    return boundaries;
  }
  boundaries.push_back(0);
  diag::counter("unicode.grapheme_scans", "run").add();

  BreakState st;
  const G first = gcb(cps[0]);
  advance(&st, first, is_extended_pictographic(cps[0]), cps[0]);

  for (size_t i = 1; i < count; ++i) {
    const char32_t cp = cps[i];
    const G cur = gcb(cp);
    const bool cur_pict = is_extended_pictographic(cp);
    const bool cur_consonant = incb(cp) == I::kConsonant;
    const bool prev_zwj = st.prev == G::kZWJ;
    if (should_break(st, cur, cur_pict, cur_consonant, prev_zwj)) {
      boundaries.push_back(static_cast<uint32_t>(i));
      diag::counter("unicode.grapheme_breaks", "break").add();
    }
    advance(&st, cur, cur_pict, cp);
  }
  boundaries.push_back(static_cast<uint32_t>(count));
  return boundaries;
}

std::vector<uint32_t> grapheme_boundaries(std::string_view utf8) {
  std::vector<char32_t> cps;
  cps.reserve(utf8.size());
  std::vector<uint32_t> byte_offsets;
  byte_offsets.reserve(utf8.size() + 1);
  Utf8Iterator it(utf8);
  while (!it.done()) {
    byte_offsets.push_back(static_cast<uint32_t>(it.byte_index()));
    cps.push_back(it.next());
  }
  byte_offsets.push_back(static_cast<uint32_t>(utf8.size()));

  const std::vector<uint32_t> cp_bounds = grapheme_boundaries_cp(cps.data(), cps.size());
  std::vector<uint32_t> out;
  out.reserve(cp_bounds.size());
  for (uint32_t b : cp_bounds) out.push_back(byte_offsets[b]);
  return out;
}

size_t next_grapheme_boundary(std::string_view utf8, size_t byte_offset) {
  if (byte_offset >= utf8.size()) return utf8.size();
  // 从当前边界所在簇的起点扫描到下一个断点
  const std::vector<uint32_t> bounds = grapheme_boundaries(utf8);
  for (uint32_t b : bounds) {
    if (b > byte_offset) return b;
  }
  return utf8.size();
}

size_t grapheme_count(std::string_view utf8) {
  if (utf8.empty()) return 0;
  return grapheme_boundaries(utf8).size() - 1;
}

}  // namespace nq::unicode
