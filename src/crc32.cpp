#include "crc32.h"

#include <array>

namespace blockzip {
namespace {

constexpr std::uint32_t kReflectedPolynomial = 0xEDB88320u;
constexpr std::size_t kTableSize = 256;

// CRC table built at compile time, so there is no static initialisation order
// or runtime setup cost.
constexpr std::array<std::uint32_t, kTableSize> make_table() noexcept {
    std::array<std::uint32_t, kTableSize> table{};
    for (std::size_t index = 0; index < kTableSize; ++index) {
        std::uint32_t crc = static_cast<std::uint32_t>(index);
        for (int bit = 0; bit < 8; ++bit) {
            crc = ((crc & 1u) != 0u) ? ((crc >> 1) ^ kReflectedPolynomial) : (crc >> 1);
        }
        table[index] = crc;
    }
    return table;
}

constexpr std::array<std::uint32_t, kTableSize> kTable = make_table();

constexpr std::uint32_t kInitialState = 0xFFFFFFFFu;
constexpr std::uint32_t kFinalXor = 0xFFFFFFFFu;

}  // namespace

void Crc32::update(const void* data, std::size_t size) noexcept {
    if (data == nullptr || size == 0) {
        return;
    }

    const auto* bytes = static_cast<const std::uint8_t*>(data);
    std::uint32_t crc = state_;
    for (std::size_t index = 0; index < size; ++index) {
        const std::uint8_t table_index = static_cast<std::uint8_t>((crc ^ bytes[index]) & 0xFFu);
        crc = kTable[table_index] ^ (crc >> 8);
    }
    state_ = crc;
}

void Crc32::update(const std::vector<std::uint8_t>& bytes) noexcept {
    update(bytes.data(), bytes.size());
}

std::uint32_t Crc32::value() const noexcept {
    return state_ ^ kFinalXor;
}

void Crc32::reset() noexcept {
    state_ = kInitialState;
}

std::uint32_t crc32(const void* data, std::size_t size) noexcept {
    Crc32 crc;
    crc.update(data, size);
    return crc.value();
}

std::uint32_t crc32(const std::vector<std::uint8_t>& bytes) noexcept {
    return crc32(bytes.data(), bytes.size());
}

}  // namespace blockzip
