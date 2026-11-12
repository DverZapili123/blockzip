#include "lz77.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace blockzip {
namespace {

constexpr std::uint32_t kNoPosition = 0xFFFFFFFFu;
constexpr unsigned kHashBits = 16;
constexpr std::size_t kHashSize = std::size_t{1} << kHashBits;
constexpr std::size_t kWindowMask = kLz77WindowSize - 1;

std::uint32_t hash3(const std::uint8_t* bytes) noexcept {
    const std::uint32_t value = (static_cast<std::uint32_t>(bytes[0]) << 16) |
                                (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                static_cast<std::uint32_t>(bytes[2]);
    return (value * 2654435761u) >> (32u - kHashBits);
}

struct Match {
    std::size_t length = 0;
    std::size_t distance = 0;
};

// Hash chains over the input: head_[hash] is the most recent position with that
// three-byte hash, prev_[pos & mask] the previous one. Positions are inserted in
// strictly increasing order and only after the match at `pos` was searched, so a
// candidate inside the window always has an intact prev_ slot: the slot is
// reused exactly 32768 positions later, which is either still the current
// position or already outside the window.
class Matcher {
public:
    Matcher(const std::uint8_t* data, std::size_t size, const Lz77Options& options)
        : data_(data),
          size_(size),
          max_depth_(options.max_chain_depth),
          head_(kHashSize, kNoPosition),
          prev_(kLz77WindowSize, kNoPosition) {}

    Match find(std::size_t pos) const {
        Match best;
        if (pos + kLz77MinMatchLength > size_) {
            return best;
        }

        const std::size_t max_length = std::min(kLz77MaxMatchLength, size_ - pos);
        std::uint32_t candidate = head_[hash3(data_ + pos)];
        std::size_t examined = 0;

        while (candidate != kNoPosition) {
            if (candidate >= pos) {
                break;
            }
            const std::size_t distance = pos - candidate;
            if (distance > kLz77WindowSize) {
                break;
            }
            if (max_depth_ != kLz77UnlimitedChainDepth && examined >= max_depth_) {
                break;
            }
            ++examined;

            // Cheap rejection first: a longer match must agree at best.length.
            if (best.length < max_length &&
                data_[candidate + best.length] == data_[pos + best.length]) {
                std::size_t length = 0;
                while (length < max_length && data_[candidate + length] == data_[pos + length]) {
                    ++length;
                }
                if (length > best.length) {
                    best.length = length;
                    best.distance = distance;
                    if (length == max_length) {
                        break;
                    }
                }
            }

            candidate = prev_[candidate & kWindowMask];
        }
        return best;
    }

    void insert(std::size_t pos) {
        if (pos + kLz77MinMatchLength > size_) {
            return;
        }
        const std::uint32_t hash = hash3(data_ + pos);
        prev_[pos & kWindowMask] = head_[hash];
        head_[hash] = static_cast<std::uint32_t>(pos);
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t max_depth_;
    std::vector<std::uint32_t> head_;
    std::vector<std::uint32_t> prev_;
};

Lz77Token make_literal(std::uint8_t value) noexcept {
    return Lz77Token{0, 0, value};
}

Lz77Token make_match(std::size_t length, std::size_t distance) noexcept {
    return Lz77Token{static_cast<std::uint16_t>(length), static_cast<std::uint16_t>(distance), 0};
}

std::vector<Lz77Token> compress_greedy(const std::uint8_t* data, std::size_t size,
                                       const Lz77Options& options) {
    Matcher matcher(data, size, options);
    std::vector<Lz77Token> tokens;
    std::size_t pos = 0;

    while (pos < size) {
        const Match match = matcher.find(pos);
        if (match.length >= kLz77MinMatchLength) {
            tokens.push_back(make_match(match.length, match.distance));
            for (std::size_t offset = 0; offset < match.length; ++offset) {
                matcher.insert(pos + offset);
            }
            pos += match.length;
        } else {
            tokens.push_back(make_literal(data[pos]));
            matcher.insert(pos);
            ++pos;
        }
    }
    return tokens;
}

std::vector<Lz77Token> compress_lazy_stream(const std::uint8_t* data, std::size_t size,
                                            const Lz77Options& options) {
    Matcher matcher(data, size, options);
    std::vector<Lz77Token> tokens;
    std::size_t pos = 0;
    Match current = matcher.find(pos);

    while (pos < size) {
        if (current.length >= kLz77MinMatchLength) {
            // Make `pos` visible to the search at `pos + 1` before comparing.
            matcher.insert(pos);
            Match next;
            if (pos + 1 < size) {
                next = matcher.find(pos + 1);
            }

            if (next.length > current.length) {
                // A literal now buys a longer match one byte later.
                tokens.push_back(make_literal(data[pos]));
                ++pos;
                current = next;
                continue;
            }

            tokens.push_back(make_match(current.length, current.distance));
            for (std::size_t offset = 1; offset < current.length; ++offset) {
                matcher.insert(pos + offset);
            }
            pos += current.length;
        } else {
            tokens.push_back(make_literal(data[pos]));
            matcher.insert(pos);
            ++pos;
        }

        if (pos < size) {
            current = matcher.find(pos);
        }
    }
    return tokens;
}

// Lazy matching is a heuristic and can occasionally produce one token more than
// the greedy pass. Building both streams and returning the shorter one makes the
// "lazy" mode a strict non-regression in token count; it costs a second pass.
std::vector<Lz77Token> compress_lazy(const std::uint8_t* data, std::size_t size,
                                     const Lz77Options& options) {
    std::vector<Lz77Token> lazy = compress_lazy_stream(data, size, options);
    std::vector<Lz77Token> greedy = compress_greedy(data, size, options);
    if (lazy.size() <= greedy.size()) {
        return lazy;
    }
    return greedy;
}

}  // namespace

std::vector<Lz77Token> lz77_compress(const std::uint8_t* data, std::size_t size,
                                     const Lz77Options& options) {
    if (data == nullptr && size != 0) {
        throw std::invalid_argument("lz77_compress: null input with a non-zero size");
    }
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("lz77_compress: input exceeds the 32-bit position space");
    }
    if (size == 0) {
        return {};
    }
    return options.lazy_matching ? compress_lazy(data, size, options)
                                 : compress_greedy(data, size, options);
}

std::vector<Lz77Token> lz77_compress(const std::vector<std::uint8_t>& data,
                                     const Lz77Options& options) {
    return lz77_compress(data.data(), data.size(), options);
}

std::vector<std::uint8_t> lz77_decompress(const std::vector<Lz77Token>& tokens) {
    std::size_t total = 0;
    for (const Lz77Token& token : tokens) {
        const std::size_t addition = token.is_literal() ? 1 : token.length;
        if (total > std::numeric_limits<std::size_t>::max() - addition) {
            throw Lz77Error("lz77_decompress: decoded size overflows");
        }
        total += addition;
    }

    std::vector<std::uint8_t> output;
    output.reserve(total);

    for (const Lz77Token& token : tokens) {
        if (token.is_literal()) {
            output.push_back(token.literal);
            continue;
        }
        if (token.length < kLz77MinMatchLength || token.length > kLz77MaxMatchLength) {
            throw Lz77Error("lz77_decompress: match length outside [3, 258]");
        }
        if (token.distance == 0 || token.distance > kLz77WindowSize) {
            throw Lz77Error("lz77_decompress: match distance outside [1, 32768]");
        }
        if (token.distance > output.size()) {
            throw Lz77Error("lz77_decompress: match distance exceeds the decoded data");
        }

        const std::size_t source = output.size() - token.distance;
        for (std::size_t offset = 0; offset < token.length; ++offset) {
            // Copy into a local: `source` may point inside `output`, and the
            // vector can reallocate while the match is being reconstructed.
            const std::uint8_t byte = output[source + offset];
            output.push_back(byte);
        }
    }
    return output;
}

}  // namespace blockzip
