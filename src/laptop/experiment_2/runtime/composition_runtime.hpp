#pragma once
#include <cstddef>
#include <cstdint>
#include <libaio.h>
#include <mutex>
#include <vector>
#include <memory>
#include <atomic>

namespace composition {
struct PoolStats {
  uint64_t setup=0, destroy=0, leases=0, live=0, peak_live=0, borrowed=0;
  uint64_t live_capacity=0, peak_live_capacity=0;
  double setup_s=0, destroy_s=0;
};
class ContextPool {
 public:
  explicit ContextPool(size_t limit=17);
  ~ContextPool();
  io_context_t acquire(size_t depth=1024);
  void release(io_context_t ctx, bool retain);
  void shutdown();
  void retire_current_thread_reference_context();
  void reference_thread_exit(io_context_t ctx,uint64_t generation);
  PoolStats stats() const;
  uint64_t pending() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
struct CacheStats {
  uint64_t reads=0, hits=0, writes=0, dirty_flushes=0, evictions=0;
  uint64_t resident=0, dirty=0, capacity=0, accounted_bytes=0;
};
class PageStore {
 public:
  explicit PageStore(size_t budget=33554432);
  ~PageStore();
  void bind(int fd, uint64_t generation);
  bool matches(int fd) const;
  bool get(uint64_t sector, void* out);
  void put_clean(uint64_t sector, const void* data);
  void put_dirty(uint64_t sector, const void* data);
  void boundary(bool retain);
  void finish();
  CacheStats stats() const;
 private:
  struct Slot { uint64_t sector; int next,prev,hash_next; bool valid,dirty; };
  mutable std::mutex mutex_;
  void* arena_=nullptr;
  Slot* slots_=nullptr;
  int* buckets_=nullptr;
  char* pages_=nullptr;
  size_t capacity_=0, bucket_count_=0;
  int fd_=-1, head_=-1, tail_=-1;
  uint64_t dev_=0, ino_=0, generation_=0;
  CacheStats stats_;
  int find(uint64_t sector) const;
  void touch(int slot);
  void flush(int slot);
  int reserve(uint64_t sector);
  void put(uint64_t sector, const void* data, bool dirty);
  void clear_locked();
};
void inject_setup_failure();
void inject_destroy_failure();
ContextPool& pool();
bool persistent_aio();
bool cache_enabled();
PageStore* store();
void begin_batch(const char* path);
void phase_boundary();
void end_batch();
void shutdown();
void report(const char* path);
void record_io(bool write, uint64_t bytes, uint64_t requests=1, bool sync_write=false);
} // namespace composition
