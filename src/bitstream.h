#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace blockzip {

// Thrown by BitReader when a read would run past the end of the bit stream.
class BitStreamError : public std::runtime_error {
public:
    explicit BitStreamError(const std::string& message) : std::runtime_error(message) {}
};

// Appends bits to a growable byte buffer, least significant bit first.
//
// Bits are packed into bytes from bit 0 upwards; the trailing unused bits of the
// last byte are always zero. BitWriter never throws for valid input and keeps no
// ownership of any external buffer.
class BitWriter {
public:
    // Writes the low `nbits` bits of `value` (bits above `nbits` are ignored).
    // `nbits` must be in [0, 32]; anything wider is a programming error.
    void write_bits(std::uint32_t value, unsigned nbits);

    // Writes a single bit.
    void write_bit(bool bit);

    // Padded byte buffer produced so far.
    const std::vector<std::uint8_t>& bytes() const noexcept { return buffer_; }

    // Number of bits written so far (not rounded up to whole bytes).
    std::size_t bit_size() const noexcept { return bit_size_; }

    // Drops all buffered data and restarts from an empty stream.
    void clear() noexcept;

private:
    std::vector<std::uint8_t> buffer_;
    std::size_t bit_size_ = 0;
};

// Reads bits from an immutable byte buffer, least significant bit first.
//
// The buffer is referenced, not copied, so it must outlive the reader. Any read
// that would exceed the available bits throws BitStreamError and leaves the
// reader state untouched.
class BitReader {
public:
    // Treats every bit of the buffer as payload (byte-aligned streams).
    BitReader(const std::uint8_t* data, std::size_t size) noexcept;
    explicit BitReader(const std::vector<std::uint8_t>& bytes) noexcept;

    // Limits the payload to `bit_length` bits, so the zero padding of the last
    // byte is not readable. Throws std::invalid_argument when `bit_length`
    // exceeds the number of bits stored in the buffer.
    BitReader(const std::uint8_t* data, std::size_t size, std::size_t bit_length);
    BitReader(const std::vector<std::uint8_t>& bytes, std::size_t bit_length);

    // Reads the next `nbits` bits; `nbits` must be in [0, 32].
    // Throws BitStreamError when fewer than `nbits` bits remain.
    std::uint32_t read_bits(unsigned nbits);

    // Reads a single bit; throws BitStreamError at the end of the stream.
    bool read_bit();

    // Bits consumed so far.
    std::size_t bit_position() const noexcept { return bit_position_; }

    // Bits still available.
    std::size_t bits_remaining() const noexcept { return total_bits_ - bit_position_; }

private:
    const std::uint8_t* data_;
    std::size_t total_bits_;
    std::size_t bit_position_ = 0;
};

}  // namespace blockzip
