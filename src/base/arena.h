// NextQt · 每帧/每文档 arena 分配器（bump allocator）
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>
#include <span>
#include <utility>

#include "base/status.h"

namespace nq {

// 单调递增分配，reset() 为 O(1)。大对象单独成块，避免浪费。
class Arena {
 public:
  explicit Arena(size_t block_size = 64 * 1024) noexcept;
  ~Arena();

  Arena(const Arena&) = delete;
  Arena& operator=(const Arena&) = delete;
  Arena(Arena&&) = delete;
  Arena& operator=(Arena&&) = delete;

  // 失败返回 nullptr（分配器不可抛异常）
  [[nodiscard]] void* alloc(size_t bytes, size_t align = 16) noexcept;

  template <class T, class... Args>
  [[nodiscard]] T* create(Args&&... args) noexcept {
    void* p = alloc(sizeof(T), alignof(T));
    if (p == nullptr) return nullptr;
    return new (p) T(std::forward<Args>(args)...);
  }

  template <class T>
  [[nodiscard]] std::span<T> alloc_array(size_t n) noexcept {
    void* p = alloc(sizeof(T) * n, alignof(T));
    if (p == nullptr) return {};
    return {static_cast<T*>(p), n};
  }

  void reset() noexcept;    // 保留第一块，供下一帧复用
  void release() noexcept;  // 归还全部内存

  [[nodiscard]] size_t bytes_used() const noexcept { return used_total_; }
  [[nodiscard]] size_t bytes_committed() const noexcept { return committed_; }
  [[nodiscard]] size_t block_count() const noexcept { return block_count_; }

 private:
  struct Block {
    Block* next;
    size_t size;
    size_t used;
    alignas(16) unsigned char data[1];
  };

  [[nodiscard]] bool add_block(size_t min_size) noexcept;

  size_t block_size_;
  Block* head_ = nullptr;
  size_t used_total_ = 0;
  size_t committed_ = 0;
  size_t block_count_ = 0;
};

}  // namespace nq
