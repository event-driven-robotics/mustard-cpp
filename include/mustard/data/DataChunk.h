#pragma once
#include <cstdint>
#include <vector>

namespace mustard {

/// A contiguous block of typed data covering the time interval [t_start, t_end].
template <typename T>
struct DataChunk {
    int64_t        t_start{0};
    int64_t        t_end{0};
    std::vector<T> data;
};

} // namespace mustard
