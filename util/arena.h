//  Copyright (c) 2013, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

// Arena is an implementation of Arena class. For a request of small size,
// it allocates a block with pre-defined block size. For a request of big
// size, it uses malloc to directly get the requested size.

#pragma once
#include <cstddef>
#include <vector>
#include <assert.h>
#include <stdint.h>
#include "util/arena.h"

namespace rocksdb {

class Logger;

class Arena {
 public:
  // No copying allowed
  Arena(const Arena&) = delete;
  void operator=(const Arena&) = delete;

  static const size_t kMinBlockSize;
  static const size_t kMaxBlockSize;

  explicit Arena(size_t block_size = kMinBlockSize);
  ~Arena();

  char* Allocate(size_t bytes);

  // huge_page_tlb_size: if >0, allocate bytes from huge page TLB and the size
  // of the huge page TLB. Bytes will be rounded up to multiple and 2MB and
  // allocate huge pages through mmap anonymous option with huge page on.
  // The extra  space allocated will be wasted. To enable it, need to reserve
  // huge pages for it to be allocated, like:
  //     sysctl -w vm.nr_hugepages=20
  // See linux doc Documentation/vm/hugetlbpage.txt for details.
  // huge page allocation can fail. In this case it will fail back to
  // normal cases. The messages will be logged to logger. So when calling with
  // huge_page_tlb_size > 0, we highly recommend a logger is passed in.
  // Otherwise, the error message will be printed out to stderr directly.
  char* AllocateAligned(size_t bytes, size_t huge_page_tlb_size = 0,
                        Logger* logger = nullptr);

  // Returns an estimate of the total memory usage of data allocated
  // by the arena (exclude the space allocated but not yet used for future
  // allocations).
  size_t ApproximateMemoryUsage() const {
    return blocks_memory_ + blocks_.capacity() * sizeof(char*) -
           alloc_bytes_remaining_;
  }

  size_t MemoryAllocatedBytes() const { return blocks_memory_; }

  size_t AllocatedAndUnused() const { return alloc_bytes_remaining_; }

  // If an allocation is too big, we'll allocate an irregular block with the
  // same size of that allocation.
  virtual size_t IrregularBlockNum() const { return irregular_block_num; }

  size_t BlockSize() const { return kBlockSize; }

 private:
  // Number of bytes allocated in one block
  const size_t kBlockSize;
  // Array of new[] allocated memory blocks
  typedef std::vector<char*> Blocks;
  Blocks blocks_;

  struct MmapInfo {
    void* addr_;
    size_t length_;

    MmapInfo(void* addr, size_t length) : addr_(addr), length_(length) {}
  };
  std::vector<MmapInfo> huge_blocks_;
  size_t irregular_block_num = 0;

  // Stats for current active block.
  // For each block, we allocate aligned memory chucks from one end and
  // allocate unaligned memory chucks from the other end. Otherwise the
  // memory waste for alignment will be higher if we allocate both types of
  // memory from one direction.
  char* unaligned_alloc_ptr_ = nullptr;
  char* aligned_alloc_ptr_ = nullptr;
  // How many bytes left in currently active block?
  size_t alloc_bytes_remaining_ = 0;

  char* AllocateFallback(size_t bytes, bool aligned);
  char* AllocateNewBlock(size_t block_bytes);

  // Bytes of memory in blocks allocated so far
  size_t blocks_memory_ = 0;
};

inline char* Arena::Allocate(size_t bytes) {
  // The semantics of what to return are a bit messy if we allow
  // 0-byte allocations, so we disallow them here (we don't need
  // them for our internal use).
  assert(bytes > 0);
  if (bytes <= alloc_bytes_remaining_) {
    unaligned_alloc_ptr_ -= bytes;
    alloc_bytes_remaining_ -= bytes;
    return unaligned_alloc_ptr_;
  }
  return AllocateFallback(bytes, false /* unaligned */);
}

// check and adjust the block_size so that the return value is
//  1. in the range of [kMinBlockSize, kMaxBlockSize].
//  2. the multiple of align unit.
extern size_t OptimizeBlockSize(size_t block_size);

}  // namespace rocksdb
