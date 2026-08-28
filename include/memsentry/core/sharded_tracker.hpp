#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <array>
#include "memsentry/types.hpp"

namespace memsentry::core {

inline constexpr size_t SHARD_COUNT = 64;
static_assert((SHARD_COUNT & (SHARD_COUNT - 1)) == 0, "SHARD_COUNT must be a power of 2");

class ShardedTracker {
public:
    ShardedTracker() = default;
    ~ShardedTracker() = default;

    ShardedTracker(const ShardedTracker&) = delete;
    ShardedTracker& operator=(const ShardedTracker&) = delete;

    void insert(const void* user_ptr, AllocationRecord record) {
        if (!user_ptr) return;
        size_t idx = get_shard_index(user_ptr);
        std::lock_guard<std::mutex> lock(shards_[idx].mtx);
        shards_[idx].records[user_ptr] = std::move(record);
    }

    bool erase(const void* user_ptr, AllocationRecord* out_record = nullptr) {
        if (!user_ptr) return false;
        size_t idx = get_shard_index(user_ptr);
        std::lock_guard<std::mutex> lock(shards_[idx].mtx);
        auto it = shards_[idx].records.find(user_ptr);
        if (it != shards_[idx].records.end()) {
            if (out_record) {
                *out_record = std::move(it->second);
            }
            shards_[idx].records.erase(it);
            return true;
        }
        return false;
    }

    bool find(const void* user_ptr, AllocationRecord* out_record = nullptr) const {
        if (!user_ptr) return false;
        size_t idx = get_shard_index(user_ptr);
        std::lock_guard<std::mutex> lock(shards_[idx].mtx);
        auto it = shards_[idx].records.find(user_ptr);
        if (it != shards_[idx].records.end()) {
            if (out_record) {
                *out_record = it->second;
            }
            return true;
        }
        return false;
    }

    [[nodiscard]] std::vector<AllocationRecord> snapshot_all() const {
        RecursionGuard guard;
        std::vector<AllocationRecord> result;
        
        for (size_t i = 0; i < SHARD_COUNT; ++i) {
            std::lock_guard<std::mutex> lock(shards_[i].mtx);
            for (const auto& pair : shards_[i].records) {
                result.push_back(pair.second);
            }
        }

        return result;
    }

    [[nodiscard]] size_t size() const noexcept {
        size_t total = 0;
        for (size_t i = 0; i < SHARD_COUNT; ++i) {
            std::lock_guard<std::mutex> lock(shards_[i].mtx);
            total += shards_[i].records.size();
        }
        return total;
    }

    void clear() noexcept {
        RecursionGuard guard;
        for (size_t i = 0; i < SHARD_COUNT; ++i) {
            std::lock_guard<std::mutex> lock(shards_[i].mtx);
            shards_[i].records.clear();
        }
    }

private:
    struct alignas(64) Shard {
        mutable std::mutex mtx;
        std::unordered_map<const void*, AllocationRecord> records;
    };

    static inline size_t get_shard_index(const void* ptr) noexcept {
        uintptr_t val = reinterpret_cast<uintptr_t>(ptr);
        return ((val >> 4) ^ (val >> 10)) & (SHARD_COUNT - 1);
    }

    std::array<Shard, SHARD_COUNT> shards_;
};

}
