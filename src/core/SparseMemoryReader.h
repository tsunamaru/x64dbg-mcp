#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace MCP {
namespace SparseMemoryReader {

struct ReadResult {
    std::vector<uint8_t> bytes;
    size_t bytesRead = 0;
    size_t failedChunks = 0;
};

template <typename Reader>
ReadResult ReadZeroFilled(uint64_t address, size_t size, size_t pageSize,
                          Reader&& reader) {
    if (pageSize == 0) {
        throw std::invalid_argument("page size cannot be zero");
    }

    ReadResult result;
    result.bytes.resize(size, 0);

    size_t offset = 0;
    while (offset < size) {
        const uint64_t currentAddress = address + offset;
        const size_t pageOffset = static_cast<size_t>(currentAddress % pageSize);
        const size_t chunkSize = std::min(pageSize - pageOffset, size - offset);

        uint8_t* const destination = result.bytes.data() + offset;
        if (reader(currentAddress, destination, chunkSize)) {
            result.bytesRead += chunkSize;
        } else {
            // Some low-level readers can modify a destination before
            // reporting a failed or partial read.
            std::fill(destination, destination + chunkSize, 0);
            ++result.failedChunks;
        }
        offset += chunkSize;
    }

    return result;
}

} // namespace SparseMemoryReader
} // namespace MCP
