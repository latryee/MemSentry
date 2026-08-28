#pragma once

#include "memsentry/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace memsentry::profiler {

struct SizeBucket {
    std::string label;
    size_t min_bytes;
    size_t max_bytes;
    uint64_t count = 0;
    uint64_t total_bytes = 0;
};

class AllocationHistogram {
public:
    AllocationHistogram() {
        buckets_ = {SizeBucket{"[1 - 16 B]", 1, 16, 0, 0},
                    SizeBucket{"[17 - 64 B]", 17, 64, 0, 0},
                    SizeBucket{"[65 - 256 B]", 65, 256, 0, 0},
                    SizeBucket{"[257 B - 1 KB]", 257, 1024, 0, 0},
                    SizeBucket{"[1 KB - 4 KB]", 1025, 4096, 0, 0},
                    SizeBucket{"[4 KB - 64 KB]", 4097, 65536, 0, 0},
                    SizeBucket{"[64 KB - 1 MB]", 65537, 1048576, 0, 0},
                    SizeBucket{"[> 1 MB]", 1048577, static_cast<size_t>(-1), 0, 0}};
    }

    void record(size_t size) noexcept {
        for (auto& bucket : buckets_) {
            if (size >= bucket.min_bytes && size <= bucket.max_bytes) {
                bucket.count++;
                bucket.total_bytes += size;
                break;
            }
        }
    }

    void feed(const std::vector<AllocationRecord>& records) noexcept {
        for (const auto& rec : records) {
            record(rec.requested_size);
        }
    }

    [[nodiscard]] const std::vector<SizeBucket>& buckets() const noexcept { return buckets_; }

private:
    std::vector<SizeBucket> buckets_;
};

}  // namespace memsentry::profiler
