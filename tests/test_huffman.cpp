#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <numeric>
#include <queue>
#include <random>
#include <stdexcept>
#include <utility>
#include <vector>

#include "bitstream.h"
#include "huffman.h"

namespace {

using blockzip::BitStreamError;
using blockzip::BitWriter;
using blockzip::CodeLengths;
using blockzip::Frequencies;

constexpr std::uint32_t kKraftLimit = 1u << blockzip::kMaxCodeLength;

Frequencies frequencies_of(const std::vector<std::uint8_t>& data) {
    Frequencies freqs{};
    for (const std::uint8_t byte : data) {
        ++freqs[byte];
    }
    return freqs;
}

std::vector<std::uint8_t> make_random_data(std::size_t size, unsigned alphabet, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<unsigned> dist(0, alphabet - 1);
    std::vector<std::uint8_t> data(size);
    for (auto& byte : data) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return data;
}

// Kraft sum in units of 2^-15: <= 1<<15 means the code is complete enough and
// prefix-free. Length 0 marks an unused symbol and contributes nothing.
std::uint32_t kraft_units(const CodeLengths& lengths) {
    std::uint32_t units = 0;
    for (const std::uint8_t length : lengths) {
        if (length != 0) {
            units += 1u << (blockzip::kMaxCodeLength - length);
        }
    }
    return units;
}

std::uint64_t code_cost(const Frequencies& freqs, const CodeLengths& lengths) {
    std::uint64_t cost = 0;
    for (std::size_t symbol = 0; symbol < freqs.size(); ++symbol) {
        cost += static_cast<std::uint64_t>(freqs[symbol]) * lengths[symbol];
    }
    return cost;
}

// Classic Huffman tree without any length limit, used to prove that the
// Fibonacci frequency vector really does need codes longer than 15 bits.
unsigned unconstrained_huffman_depth(const Frequencies& freqs) {
    using Node = std::pair<std::uint64_t, unsigned>;  // (weight, depth below node)
    std::priority_queue<Node, std::vector<Node>, std::greater<Node>> queue;
    for (const std::uint32_t freq : freqs) {
        if (freq > 0) {
            queue.emplace(freq, 0);
        }
    }
    if (queue.empty()) {
        return 0;
    }
    if (queue.size() == 1) {
        return 1;
    }
    while (queue.size() > 1) {
        const Node first = queue.top();
        queue.pop();
        const Node second = queue.top();
        queue.pop();
        queue.emplace(first.first + second.first, std::max(first.second, second.second) + 1);
    }
    return queue.top().second;
}

// Exhaustive search for the cheapest length assignment with the given limit.
// Independent of the production algorithm: it only relies on the Kraft
// inequality, so it can be used as an oracle on small alphabets.
std::uint64_t brute_force_optimal_cost(const std::vector<std::uint32_t>& freqs, unsigned max_len) {
    const std::size_t count = freqs.size();
    const std::uint32_t limit = 1u << max_len;
    std::uint64_t best = std::numeric_limits<std::uint64_t>::max();
    std::vector<unsigned> lengths(count, 0);

    std::function<void(std::size_t, std::uint32_t)> visit =
        [&](std::size_t index, std::uint32_t kraft) {
            if (index == count) {
                std::uint64_t cost = 0;
                for (std::size_t i = 0; i < count; ++i) {
                    cost += static_cast<std::uint64_t>(freqs[i]) * lengths[i];
                }
                best = std::min(best, cost);
                return;
            }
            for (unsigned length = 1; length <= max_len; ++length) {
                const std::uint32_t unit = 1u << (max_len - length);
                if (kraft + unit > limit) {
                    continue;
                }
                lengths[index] = length;
                visit(index + 1, kraft + unit);
            }
        };

    visit(0, 0);
    return best;
}

void expect_valid_lengths(const Frequencies& freqs, const CodeLengths& lengths,
                          unsigned max_len = blockzip::kMaxCodeLength) {
    for (std::size_t symbol = 0; symbol < freqs.size(); ++symbol) {
        if (freqs[symbol] == 0) {
            EXPECT_EQ(lengths[symbol], 0u) << "symbol " << symbol;
        } else {
            EXPECT_GE(lengths[symbol], 1u) << "symbol " << symbol;
            EXPECT_LE(lengths[symbol], max_len) << "symbol " << symbol;
        }
    }
    EXPECT_LE(kraft_units(lengths), kKraftLimit);
}

}  // namespace

// Acceptance (SPEC, stage 2): round-trip on the empty input.
TEST(Huffman, EmptyInputRoundTrips) {
    const std::vector<std::uint8_t> data;
    const std::vector<std::uint8_t> encoded = blockzip::huffman_encode(data);
    EXPECT_FALSE(encoded.empty());  // the empty table is still a header

    const std::vector<std::uint8_t> decoded = blockzip::huffman_decode(encoded, 0);
    EXPECT_TRUE(decoded.empty());

    const CodeLengths lengths = blockzip::build_code_lengths(frequencies_of(data));
    for (const std::uint8_t length : lengths) {
        EXPECT_EQ(length, 0u);
    }
}

// Acceptance (SPEC, stage 2): round-trip on exactly one byte.
TEST(Huffman, SingleByteRoundTrips) {
    for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0x42}, std::uint8_t{0xFF}}) {
        const std::vector<std::uint8_t> data{value};
        const CodeLengths lengths = blockzip::build_code_lengths(frequencies_of(data));
        EXPECT_EQ(lengths[value], 1u);
        for (std::size_t symbol = 0; symbol < lengths.size(); ++symbol) {
            if (symbol != value) {
                EXPECT_EQ(lengths[symbol], 0u);
            }
        }
        EXPECT_LE(kraft_units(lengths), kKraftLimit);

        const std::vector<std::uint8_t> encoded = blockzip::huffman_encode(data);
        EXPECT_EQ(blockzip::huffman_decode(encoded, data.size()), data);
    }
}

// Acceptance (SPEC, stage 2): round-trip on one repeated byte.
TEST(Huffman, SingleRepeatedByteRoundTrips) {
    for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0x7F}, std::uint8_t{0xFF}}) {
        const std::vector<std::uint8_t> data(10000, value);
        const CodeLengths lengths = blockzip::build_code_lengths(frequencies_of(data));
        EXPECT_EQ(lengths[value], 1u);
        EXPECT_LE(kraft_units(lengths), kKraftLimit);

        const std::vector<std::uint8_t> encoded = blockzip::huffman_encode(data);
        EXPECT_LT(encoded.size(), data.size());
        EXPECT_EQ(blockzip::huffman_decode(encoded, data.size()), data);
    }
}

// Acceptance (SPEC, stage 2): round-trip on all 256 byte values.
TEST(Huffman, AllByteValuesRoundTrip) {
    std::vector<std::uint8_t> data(blockzip::kAlphabetSize);
    std::iota(data.begin(), data.end(), std::uint8_t{0});

    const Frequencies freqs = frequencies_of(data);
    const CodeLengths lengths = blockzip::build_code_lengths(freqs);
    for (const std::uint8_t length : lengths) {
        EXPECT_EQ(length, 8u);
    }
    EXPECT_EQ(kraft_units(lengths), kKraftLimit);  // a complete 8-bit code

    std::vector<std::uint8_t> repeated = data;
    for (int i = 0; i < 7; ++i) {
        repeated.insert(repeated.end(), data.begin(), data.end());
    }
    EXPECT_EQ(blockzip::huffman_decode(blockzip::huffman_encode(repeated), repeated.size()),
              repeated);
}

// Acceptance (SPEC, stage 2): round-trip on random data of many shapes.
TEST(Huffman, RandomDataRoundTrip) {
    const std::array<std::size_t, 21> sizes{0,   1,    2,    3,    4,    5,    7,
                                            8,   15,   16,   17,   31,   32,   63,
                                            100, 255,  256,  257,  1000, 4096, 10000};
    const std::array<unsigned, 6> alphabets{1, 2, 3, 4, 17, 256};

    for (const std::size_t size : sizes) {
        for (const unsigned alphabet : alphabets) {
            for (const std::uint32_t seed : {1u, 2u}) {
                const std::vector<std::uint8_t> data = make_random_data(size, alphabet, seed);
                const Frequencies freqs = frequencies_of(data);
                const CodeLengths lengths = blockzip::build_code_lengths(freqs);
                expect_valid_lengths(freqs, lengths);

                const std::vector<std::uint8_t> encoded = blockzip::huffman_encode(data);
                const std::vector<std::uint8_t> decoded =
                    blockzip::huffman_decode(encoded, data.size());
                ASSERT_EQ(decoded, data)
                    << "size = " << size << ", alphabet = " << alphabet << ", seed = " << seed;
            }
        }
    }
}

// The Fibonacci distribution is the classic worst case for code lengths: an
// unlimited Huffman tree is a long chain. The 15-bit limit must flatten it
// without breaking the Kraft inequality or the round-trip.
TEST(Huffman, FibonacciFrequenciesStayWithinFifteenBits) {
    Frequencies freqs{};
    std::uint32_t previous = 1;
    std::uint32_t current = 1;
    for (std::size_t symbol = 0; symbol < 25; ++symbol) {
        freqs[symbol] = current;
        const std::uint32_t next = previous + current;
        previous = current;
        current = next;
    }

    ASSERT_GT(unconstrained_huffman_depth(freqs), blockzip::kMaxCodeLength)
        << "the test needs frequencies that overflow 15 bits when not limited";

    const CodeLengths lengths = blockzip::build_code_lengths(freqs);
    expect_valid_lengths(freqs, lengths);

    unsigned max_length = 0;
    for (const std::uint8_t length : lengths) {
        max_length = std::max(max_length, static_cast<unsigned>(length));
    }
    EXPECT_LE(max_length, blockzip::kMaxCodeLength);

    // Round-trip a payload that realises exactly these frequencies.
    std::vector<std::uint8_t> data;
    data.reserve(262144);
    for (std::size_t symbol = 0; symbol < 25; ++symbol) {
        data.insert(data.end(), freqs[symbol], static_cast<std::uint8_t>(symbol));
    }
    const std::vector<std::uint8_t> encoded = blockzip::huffman_encode(data);
    EXPECT_EQ(blockzip::huffman_decode(encoded, data.size()), data);
}

// Acceptance (SPEC, stage 2): the Kraft sum never exceeds one, for any input.
TEST(Huffman, KraftSumNeverExceedsOne) {
    std::mt19937 rng(777);
    for (int trial = 0; trial < 200; ++trial) {
        Frequencies freqs{};
        const unsigned shape = static_cast<unsigned>(trial % 6);
        for (std::size_t symbol = 0; symbol < freqs.size(); ++symbol) {
            switch (shape) {
                case 0:  // uniform random
                    freqs[symbol] = std::uniform_int_distribution<std::uint32_t>(0, 1000)(rng);
                    break;
                case 1:  // powers of two: very deep unconstrained tree
                    freqs[symbol] = symbol < 30 ? static_cast<std::uint32_t>(1u << symbol) : 0;
                    break;
                case 2:  // one dominant symbol
                    freqs[symbol] = symbol == 0 ? 1000000u : 1u;
                    break;
                case 3:  // all equal
                    freqs[symbol] = 7u;
                    break;
                case 4:  // sparse: every third symbol
                    freqs[symbol] = (symbol % 3 == 0) ? static_cast<std::uint32_t>(rng() % 100) + 1
                                                      : 0;
                    break;
                default:  // steep geometric decay
                    freqs[symbol] = symbol < 25 ? static_cast<std::uint32_t>(1u << symbol) : 0;
                    break;
            }
        }

        const CodeLengths lengths = blockzip::build_code_lengths(freqs);
        expect_valid_lengths(freqs, lengths);
        EXPECT_EQ(lengths, blockzip::build_code_lengths(freqs))
            << "lengths must be deterministic, trial " << trial;
    }
}

// Determinism: the same input must always produce the same lengths and bytes.
TEST(Huffman, EncodingIsDeterministic) {
    const std::vector<std::uint8_t> data = make_random_data(5000, 64, 2024);
    const Frequencies freqs = frequencies_of(data);

    const CodeLengths first = blockzip::build_code_lengths(freqs);
    const CodeLengths second = blockzip::build_code_lengths(freqs);
    EXPECT_EQ(first, second);

    const std::vector<std::uint8_t> encoded_first = blockzip::huffman_encode(data);
    const std::vector<std::uint8_t> encoded_second = blockzip::huffman_encode(data);
    EXPECT_EQ(encoded_first, encoded_second);
}

// The length limiter must not lose optimality on small alphabets: compare the
// produced code cost with an exhaustive search over all valid assignments.
TEST(Huffman, MatchesBruteForceOptimalCostOnSmallAlphabets) {
    std::mt19937 rng(99);
    for (std::size_t count = 2; count <= 6; ++count) {
        for (unsigned max_len = 2; max_len <= 4; ++max_len) {
            if (count > (std::size_t{1} << max_len)) {
                continue;  // not enough code space
            }
            for (int trial = 0; trial < 10; ++trial) {
                std::vector<std::uint32_t> weights(count);
                Frequencies freqs{};
                for (std::size_t i = 0; i < count; ++i) {
                    weights[i] = std::uniform_int_distribution<std::uint32_t>(1, 100)(rng);
                    freqs[i] = weights[i];
                }

                const CodeLengths lengths = blockzip::build_code_lengths(freqs, max_len);
                expect_valid_lengths(freqs, lengths, max_len);
                EXPECT_EQ(code_cost(freqs, lengths), brute_force_optimal_cost(weights, max_len))
                    << "count = " << count << ", max_len = " << max_len;
            }
        }
    }
}

// The decoder must reject corrupt headers instead of reading garbage.
TEST(Huffman, DecoderRejectsInvalidCodeLengths) {
    // 256 codes of length 3: Kraft sum = 32 > 1.
    BitWriter overfull;
    for (std::size_t symbol = 0; symbol < blockzip::kAlphabetSize; ++symbol) {
        overfull.write_bits(3, 4);
    }
    EXPECT_THROW(blockzip::huffman_decode(overfull.bytes(), 1), BitStreamError);

    // A table without any codes cannot decode a non-empty payload.
    BitWriter empty_table;
    for (std::size_t symbol = 0; symbol < blockzip::kAlphabetSize; ++symbol) {
        empty_table.write_bits(0, 4);
    }
    EXPECT_THROW(blockzip::huffman_decode(empty_table.bytes(), 1), BitStreamError);
}

TEST(Huffman, DecoderRejectsTruncatedData) {
    std::vector<std::uint8_t> data(blockzip::kAlphabetSize);
    std::iota(data.begin(), data.end(), std::uint8_t{0});

    const std::vector<std::uint8_t> encoded = blockzip::huffman_encode(data);

    // A header cut in the middle.
    const std::vector<std::uint8_t> truncated_header(encoded.begin(), encoded.begin() + 16);
    EXPECT_THROW(blockzip::huffman_decode(truncated_header, 0), BitStreamError);
    EXPECT_THROW(blockzip::huffman_decode(truncated_header, 10), BitStreamError);

    // A payload cut before the last symbol was written.
    const std::vector<std::uint8_t> truncated_payload(encoded.begin(), encoded.end() - 1);
    EXPECT_THROW(blockzip::huffman_decode(truncated_payload, data.size()), BitStreamError);

    // Asking for more symbols than were encoded runs past the end.
    EXPECT_THROW(blockzip::huffman_decode(encoded, data.size() + 1), BitStreamError);
}
