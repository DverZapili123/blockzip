#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace blockzip {

// Incremental CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320), the same
// checksum used by zlib/gzip. `value()` may be called at any time and returns the
// checksum of everything fed so far without disturbing the state.
class Crc32 {
public:
    Crc32() noexcept = default;

    // Feeds `size` bytes starting at `data`; `data` may be null when size is 0.
    void update(const void* data, std::size_t size) noexcept;
    void update(const std::vector<std::uint8_t>& bytes) noexcept;

    // Finalised checksum of all data fed since construction or the last reset().
    std::uint32_t value() const noexcept;

    // Returns the object to its initial (empty-input) state.
    void reset() noexcept;

private:
    std::uint32_t state_ = 0xFFFFFFFFu;
};

// One-shot helpers.
std::uint32_t crc32(const void* data, std::size_t size) noexcept;
std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) noexcept;

}  // namespace blockzip
