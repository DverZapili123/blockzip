#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace blockzip {

// LZ77 matcher and decoder for the block format of stage 3.
//
// The matcher turns bytes into a sequence of tokens: either a literal byte or a
// (length, distance) pair with length in [3, 258] and distance in [1, 32768].
// Distances may be shorter than the match length, which is how runs and periodic
// data are encoded; the decoder copies byte by byte, so such overlapping matches
// work naturally.

inline constexpr std::size_t kLz77WindowSize = 32768;
inline constexpr std::size_t kLz77MinMatchLength = 3;
inline constexpr std::size_t kLz77MaxMatchLength = 258;

// Chain depth 0 means "unlimited": the matcher then inspects every candidate
// and finds the same match length as an exhaustive search, at the cost of
// potentially quadratic time on adversarial input.
inline constexpr std::size_t kLz77UnlimitedChainDepth = 0;

class Lz77Error : public std::runtime_error {
public:
    explicit Lz77Error(const std::string& message) : std::runtime_error(message) {}
};

struct Lz77Token {
    // 0 marks a literal; a match has length in [3, 258].
    std::uint16_t length = 0;
    // 1..32768 for a match, unused for a literal.
    std::uint16_t distance = 0;
    // The byte value for a literal, unused for a match.
    std::uint8_t literal = 0;

    bool is_literal() const noexcept { return length == 0; }
};

struct Lz77Options {
    // Candidate positions inspected per hash chain before giving up. Bounding it
    // keeps compression linear in practice; 0 means unlimited.
    std::size_t max_chain_depth = 64;
    // Look one byte ahead and emit a literal instead when that buys a longer
    // match. The lazy pass is also compared with the greedy pass and the shorter
    // stream wins, so enabling this never increases the token count; it costs a
    // second matching pass.
    bool lazy_matching = false;
};

// Returns the token sequence for `data`. Throws std::invalid_argument for a null
// pointer with a non-zero size or for inputs above the 32-bit position space.
std::vector<Lz77Token> lz77_compress(const std::uint8_t* data, std::size_t size,
                                     const Lz77Options& options = {});
std::vector<Lz77Token> lz77_compress(const std::vector<std::uint8_t>& data,
                                     const Lz77Options& options = {});

// Reconstructs the original bytes. Throws Lz77Error when a match length is
// outside [3, 258], a distance is 0, exceeds the window, or points past the
// already reconstructed data.
std::vector<std::uint8_t> lz77_decompress(const std::vector<Lz77Token>& tokens);

}  // namespace blockzip
