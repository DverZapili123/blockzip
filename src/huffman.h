#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "bitstream.h"

namespace blockzip {

// Canonical Huffman coding for a byte alphabet with a hard 15-bit limit.
//
// The compressed representation is self-describing: the encoder writes a header
// with 256 code lengths (4 bits each, so 0..15 fits exactly), followed by the
// encoded symbols. The decoder is told how many symbols to produce by the caller
// (the container stores the raw block size), which is why the payload needs no
// explicit end marker.

inline constexpr unsigned kMaxCodeLength = 15;
inline constexpr std::size_t kAlphabetSize = 256;
inline constexpr unsigned kCodeLengthBits = 4;

using Frequencies = std::array<std::uint32_t, kAlphabetSize>;
using CodeLengths = std::array<std::uint8_t, kAlphabetSize>;

// Counts byte occurrences. An empty input yields an all-zero histogram.
Frequencies count_frequencies(const std::uint8_t* data, std::size_t size) noexcept;
Frequencies count_frequencies(const std::vector<std::uint8_t>& data) noexcept;

// Builds deterministic, length-limited canonical Huffman code lengths.
//
// Symbols with a zero frequency get length 0 and are not coded; a single used
// symbol gets length 1; an all-zero histogram yields all zeros. The result
// always satisfies the Kraft inequality sum(2^-len) <= 1 and every used symbol
// gets a length in [1, max_code_length]. Throws std::invalid_argument when the
// limit is outside [1, 15] or cannot host the used alphabet.
CodeLengths build_code_lengths(const Frequencies& frequencies,
                               unsigned max_code_length = kMaxCodeLength);

// Kraft sum in units of 2^-kMaxCodeLength; a valid code never exceeds 1 << 15.
std::uint32_t kraft_sum_units(const CodeLengths& lengths) noexcept;

// Encodes `size` bytes into `out`: the code-length header first, then the data.
// Throws std::logic_error only if the internal length table is inconsistent.
void huffman_encode(const std::uint8_t* data, std::size_t size, BitWriter& out);
void huffman_decode(BitReader& in, std::size_t size, std::vector<std::uint8_t>& out);

// Convenience wrappers that own the buffers.
std::vector<std::uint8_t> huffman_encode(const std::uint8_t* data, std::size_t size);
std::vector<std::uint8_t> huffman_encode(const std::vector<std::uint8_t>& data);

// `in`/`encoded` must describe exactly the same data: the header is read, then
// `size` symbols are decoded. Throws BitStreamError on a corrupted or truncated
// stream (bad Kraft sum, no codes, invalid code, or an early end of data).
std::vector<std::uint8_t> huffman_decode(const std::uint8_t* data, std::size_t data_size,
                                         std::size_t size);
std::vector<std::uint8_t> huffman_decode(const std::vector<std::uint8_t>& encoded,
                                         std::size_t size);

}  // namespace blockzip
