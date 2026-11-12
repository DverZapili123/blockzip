#include "bitstream.h"

#include <algorithm>

namespace blockzip {
namespace {

constexpr unsigned kBitsPerByte = 8;
constexpr unsigned kMaxBitsPerWrite = 32;

unsigned low_bits_mask(unsigned nbits) noexcept {
    return nbits >= kMaxBitsPerWrite ? 0xFFFFFFFFu : ((1u << nbits) - 1u);
}

std::size_t checked_bit_length(std::size_t byte_size, std::size_t bit_length) {
    const std::size_t capacity = byte_size * kBitsPerByte;
    if (bit_length > capacity) {
        throw std::invalid_argument("BitReader: bit_length exceeds the buffer capacity");
    }
    return bit_length;
}

}  // namespace

void BitWriter::write_bits(std::uint32_t value, unsigned nbits) {
    if (nbits > kMaxBitsPerWrite) {
        throw std::invalid_argument("BitWriter::write_bits: nbits must not exceed 32");
    }

    unsigned remaining = nbits;
    while (remaining > 0) {
        const unsigned used_in_last = static_cast<unsigned>(bit_size_ % kBitsPerByte);
        if (used_in_last == 0) {
            buffer_.push_back(0);
        }

        const unsigned take = std::min(kBitsPerByte - used_in_last, remaining);
        const std::uint32_t chunk = value & low_bits_mask(take);

        buffer_.back() = static_cast<std::uint8_t>(buffer_.back() | (chunk << used_in_last));

        value >>= take;
        bit_size_ += take;
        remaining -= take;
    }
}

void BitWriter::write_bit(bool bit) {
    write_bits(bit ? 1u : 0u, 1);
}

void BitWriter::clear() noexcept {
    buffer_.clear();
    bit_size_ = 0;
}

BitReader::BitReader(const std::uint8_t* data, std::size_t size) noexcept
    : data_(data), total_bits_(size * kBitsPerByte) {}

BitReader::BitReader(const std::vector<std::uint8_t>& bytes) noexcept
    : data_(bytes.data()), total_bits_(bytes.size() * kBitsPerByte) {}

BitReader::BitReader(const std::uint8_t* data, std::size_t size, std::size_t bit_length)
    : data_(data), total_bits_(checked_bit_length(size, bit_length)) {}

BitReader::BitReader(const std::vector<std::uint8_t>& bytes, std::size_t bit_length)
    : data_(bytes.data()), total_bits_(checked_bit_length(bytes.size(), bit_length)) {}

std::uint32_t BitReader::read_bits(unsigned nbits) {
    if (nbits > kMaxBitsPerWrite) {
        throw std::invalid_argument("BitReader::read_bits: nbits must not exceed 32");
    }
    if (nbits > bits_remaining()) {
        throw BitStreamError("BitReader::read_bits: read past the end of the bit stream");
    }

    std::uint32_t result = 0;
    unsigned shift = 0;
    unsigned remaining = nbits;

    while (remaining > 0) {
        const std::size_t byte_index = bit_position_ / kBitsPerByte;
        const unsigned offset = static_cast<unsigned>(bit_position_ % kBitsPerByte);
        const unsigned take = std::min(kBitsPerByte - offset, remaining);

        const std::uint32_t chunk =
            static_cast<std::uint32_t>((data_[byte_index] >> offset) & low_bits_mask(take));

        result |= chunk << shift;

        bit_position_ += take;
        shift += take;
        remaining -= take;
    }

    return result;
}

bool BitReader::read_bit() {
    return read_bits(1) != 0;
}

}  // namespace blockzip
