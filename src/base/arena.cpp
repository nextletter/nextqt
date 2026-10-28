#include "base/arena.h"

#include <cstdlib>
#include <new>

namespace nq {
namespace {

constexpr size_t kDefaultAlign = 16;

constexpr size_t align_up(size_t value, size_t align) noexcept {
  return (value + (align - 1)) & ~(align - 1);
}

}  // namespace

Arena::Arena(size_t block_size) noexcept
    : block_size_(block_size < 4096 ? 4096 : block_size) {}

Arena::~Arena() { release(); }

bool Arena::add_block(size_t min_size) noexcept {
  const size_t size = block_size_ > min_size ? block_size_ : min_size;
  auto* block = static_cast<Block*>(std::malloc(sizeof(Block) + size));
  if (block == nullptr) return false;
  block->next = head_;
  block->size = size;
  block->used = 0;
  head_ = block;
  committed_ += size;
  ++block_count_;
  return true;
}

void* Arena::alloc(size_t bytes, size_t align) noexcept {
  if (bytes == 0) return nullptr;
  if (align < kDefaultAlign) align = kDefaultAlign;
  if (head_ == nullptr && !add_block(bytes + align)) return nullptr;

  Block* block = head_;
  const auto aligned_offset = [](const Block* b, size_t align) noexcept {
    const uintptr_t base = reinterpret_cast<uintptr_t>(b->data);
    const size_t pad = (align - ((base + b->used) % align)) % align;
    return b->used + pad;
  };
  size_t offset = aligned_offset(block, align);
  if (offset + bytes > block->size) {
    if (!add_block(bytes + align)) return nullptr;
    block = head_;
    offset = aligned_offset(block, align);
  }
  void* ptr = block->data + offset;
  block->used = offset + bytes;
  used_total_ += bytes;
  return ptr;
}

void Arena::reset() noexcept {
  Block* keep = head_;
  Block* it = keep != nullptr ? keep->next : nullptr;
  while (it != nullptr) {
    Block* next = it->next;
    committed_ -= it->size;
    --block_count_;
    std::free(it);
    it = next;
  }
  if (keep != nullptr) {
    keep->next = nullptr;
    keep->used = 0;
  }
  used_total_ = 0;
}

void Arena::release() noexcept {
  Block* it = head_;
  while (it != nullptr) {
    Block* next = it->next;
    std::free(it);
    it = next;
  }
  head_ = nullptr;
  used_total_ = 0;
  committed_ = 0;
  block_count_ = 0;
}

}  // namespace nq
