#include "core/SparseMemoryReader.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace {

struct ReadCall {
    uint64_t address = 0;
    size_t size = 0;
};

} // namespace

int main() {
    constexpr uint64_t kBase = 0x1FFE;
    constexpr size_t kSize = 8196;
    constexpr size_t kPageSize = 4096;

    std::array<ReadCall, 4> calls{};
    size_t callCount = 0;
    const auto result = MCP::SparseMemoryReader::ReadZeroFilled(
        kBase,
        kSize,
        kPageSize,
        [&](uint64_t address, uint8_t* destination, size_t size) {
            if (callCount >= calls.size()) {
                return false;
            }
            calls[callCount++] = {address, size};
            if (address == 0x2000) {
                std::memset(destination, 0xCC, size);
                return false;
            }
            std::memset(destination,
                        static_cast<int>((address >> 12) & 0xFF),
                        size);
            return true;
        });

    if (callCount != calls.size() ||
        calls[0].address != 0x1FFE || calls[0].size != 2 ||
        calls[1].address != 0x2000 || calls[1].size != 4096 ||
        calls[2].address != 0x3000 || calls[2].size != 4096 ||
        calls[3].address != 0x4000 || calls[3].size != 2) {
        return 1;
    }

    if (result.bytes.size() != kSize || result.bytesRead != 4100 ||
        result.failedChunks != 1) {
        return 2;
    }
    if (result.bytes[0] != 1 || result.bytes[1] != 1 ||
        result.bytes[2] != 0 || result.bytes[4097] != 0 ||
        result.bytes[4098] != 3 || result.bytes[kSize - 2] != 4 ||
        result.bytes[kSize - 1] != 4) {
        return 3;
    }

    bool rejectedZeroPageSize = false;
    try {
        (void)MCP::SparseMemoryReader::ReadZeroFilled(
            0,
            1,
            0,
            [](uint64_t, uint8_t*, size_t) { return true; });
    } catch (const std::invalid_argument&) {
        rejectedZeroPageSize = true;
    }

    return rejectedZeroPageSize ? 0 : 4;
}
