#include "base/arena.h"
#include "base/clock.h"
#include "base/diag.h"
#include "base/hash.h"
#include "base/layout_unit.h"
#include "nqtest.h"

namespace {

using namespace nq;

}  // namespace

NQ_TEST(layout_unit_construction) {
  NQ_EXPECT_EQ(LayoutUnit::from_int(1).raw(), LayoutUnit::kOne);
  NQ_EXPECT_EQ(LayoutUnit::from_int(-2).raw(), -2 * LayoutUnit::kOne);
  NQ_EXPECT_NEAR(LayoutUnit::from_float(1.5f).to_float(), 1.5f, 1e-6);
  NQ_EXPECT_EQ(LayoutUnit::from_float(0.5f).raw(), LayoutUnit::kOne / 2);
  // 1/64 px 是最小单位：更小的值四舍五入
  NQ_EXPECT_EQ(LayoutUnit::from_float(0.001f).raw(), 0);
  NQ_EXPECT_EQ(LayoutUnit::from_float(0.01f).raw(), 1);
}

NQ_TEST(layout_unit_rounding) {
  NQ_EXPECT_EQ(LayoutUnit::from_float(2.4f).round_to_int(), 2);
  NQ_EXPECT_EQ(LayoutUnit::from_float(2.6f).round_to_int(), 3);
  NQ_EXPECT_EQ(LayoutUnit::from_float(-2.4f).round_to_int(), -2);
  NQ_EXPECT_EQ(LayoutUnit::from_float(-2.6f).round_to_int(), -3);
  NQ_EXPECT_EQ(LayoutUnit::from_float(-0.5f).floor_to_int(), -1);
  NQ_EXPECT_EQ(LayoutUnit::from_float(3.9f).floor_to_int(), 3);
  // 除法远离零舍入：10 / 4 = 2.5
  NQ_EXPECT_NEAR((LayoutUnit::from_int(10) / 4).to_float(), 2.5f, 1e-6);
}

NQ_TEST(layout_unit_saturates) {
  const LayoutUnit big = LayoutUnit::from_raw(LayoutUnit::kMax - 1);
  NQ_EXPECT_EQ((big + LayoutUnit::from_int(100)).raw(), LayoutUnit::kMax);
  const LayoutUnit neg = LayoutUnit::from_raw(LayoutUnit::kMin + 1);
  NQ_EXPECT_EQ((neg - LayoutUnit::from_int(100)).raw(), LayoutUnit::kMin);
  NQ_EXPECT_EQ((LayoutUnit::from_int(1) * 2147483647).raw(), LayoutUnit::kMax);
}

NQ_TEST(arena_alignment_and_reset) {
  Arena arena(4096);
  void* a = arena.alloc(1, 64);
  void* b = arena.alloc(13, 16);
  NQ_EXPECT(a != nullptr && b != nullptr);
  NQ_EXPECT_EQ(reinterpret_cast<uintptr_t>(a) % 64u, 0u);
  NQ_EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 16u, 0u);
  NQ_EXPECT(arena.bytes_used() >= 14);
  const size_t before = arena.bytes_committed();
  arena.reset();
  NQ_EXPECT_EQ(arena.bytes_used(), 0u);
  NQ_EXPECT(arena.bytes_committed() <= before);
  // reset 后可继续分配
  NQ_EXPECT(arena.alloc(1024, 16) != nullptr);
}

NQ_TEST(arena_large_allocation_grows) {
  Arena arena(4096);
  void* big = arena.alloc(1024 * 1024, 64);
  NQ_EXPECT(big != nullptr);
  NQ_EXPECT(arena.bytes_committed() >= 1024u * 1024u);
  // 继续小分配应复用/新增块，且仍然对齐
  void* small = arena.alloc(32, 16);
  NQ_EXPECT(small != nullptr);
  NQ_EXPECT_EQ(reinterpret_cast<uintptr_t>(small) % 16u, 0u);
}

NQ_TEST(hash_is_deterministic) {
  const char* a = "nextqt";
  const char* b = "nextqt";
  NQ_EXPECT_EQ(hash_bytes(a, 6), hash_bytes(b, 6));
  NQ_EXPECT(hash_bytes(a, 6) != hash_bytes("nextqu", 6));
  NQ_EXPECT_EQ(hash_combine(1, 2), hash_combine(1, 2));
  NQ_EXPECT(hash_combine(1, 2) != hash_combine(2, 1));
}

NQ_TEST(clock_is_injectable) {
  DeterministicClock clock;
  NQ_EXPECT_EQ(clock.now_micros(), 0u);
  clock.advance_micros(16'666);
  NQ_EXPECT_EQ(clock.now_micros(), 16666u);
  clock.sleep_micros(1'000);
  NQ_EXPECT_EQ(clock.now_micros(), 17666u);
}

NQ_TEST(diag_counters_are_queryable) {
  auto& c = diag::counter("test.counter", "op");
  const uint64_t before = c.value();
  c.add(3);
  NQ_EXPECT_EQ(c.value(), before + 3);
  bool found = false;
  for (const auto& s : diag::snapshot()) {
    if (s.name == "test.counter") {
      found = true;
      NQ_EXPECT_EQ(s.unit, std::string("op"));
    }
  }
  NQ_EXPECT(found);
}
