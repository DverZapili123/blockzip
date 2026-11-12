#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "lz77.h"

namespace {

using blockzip::Lz77Error;
using blockzip::Lz77Options;
using blockzip::Lz77Token;

constexpr std::size_t kWindow = blockzip::kLz77WindowSize;
constexpr std::size_t kMinMatch = blockzip::kLz77MinMatchLength;
constexpr std::size_t kMaxMatch = blockzip::kLz77MaxMatchLength;

std::vector<std::uint8_t> make_random_data(std::size_t size, unsigned alphabet, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<unsigned> dist(0, alphabet - 1);
    std::vector<std::uint8_t> data(size);
    for (auto& byte : data) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return data;
}

std::vector<std::uint8_t> bytes_of(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

// Text-like data: a repeated paragraph with changing numbers, so it contains
// both long repetitions and incompressible fragments.
std::vector<std::uint8_t> make_pseudo_text(std::size_t size) {
    const std::string base =
        "Blockzip is a console archiver. It splits input into blocks, finds repeated "
        "substrings with LZ77, and codes the result with canonical Huffman. ";
    std::mt19937 rng(2024);
    std::vector<std::uint8_t> data;
    data.reserve(size + base.size() + 16);
    while (data.size() < size) {
        std::string sentence = base;
        if (rng() % 3 == 0) {
            sentence += "Record number " + std::to_string(rng() % 1000) + ". ";
        }
        data.insert(data.end(), sentence.begin(), sentence.end());
    }
    data.resize(size);
    return data;
}

std::vector<std::uint8_t> repeat(const std::string& unit, std::size_t times) {
    std::vector<std::uint8_t> data;
    data.reserve(unit.size() * times);
    for (std::size_t i = 0; i < times; ++i) {
        data.insert(data.end(), unit.begin(), unit.end());
    }
    return data;
}

// Exhaustive reference: length of the longest match at `pos` over every
// distance in the window, capped by the format limits. Returns 0 when no match
// of at least the minimum length exists.
std::size_t naive_match_length(const std::vector<std::uint8_t>& data, std::size_t pos) {
    const std::size_t max_length = std::min(kMaxMatch, data.size() - pos);
    if (max_length < kMinMatch) {
        return 0;
    }
    std::size_t best = 0;
    const std::size_t earliest = pos > kWindow ? pos - kWindow : 0;
    for (std::size_t candidate = pos; candidate-- > earliest;) {
        std::size_t length = 0;
        while (length < max_length && data[candidate + length] == data[pos + length]) {
            ++length;
        }
        best = std::max(best, length);
        if (best == max_length) {
            break;
        }
    }
    return best >= kMinMatch ? best : 0;
}

struct LocatedToken {
    std::size_t position = 0;
    Lz77Token token;
};

std::optional<LocatedToken> token_starting_at(const std::vector<Lz77Token>& tokens,
                                              std::size_t start) {
    std::size_t position = 0;
    for (const Lz77Token& token : tokens) {
        if (position == start) {
            return LocatedToken{position, token};
        }
        position += token.is_literal() ? 1 : token.length;
    }
    return std::nullopt;
}

void expect_roundtrip(const std::vector<std::uint8_t>& data, const Lz77Options& options = {}) {
    const std::vector<Lz77Token> tokens = blockzip::lz77_compress(data, options);
    EXPECT_EQ(blockzip::lz77_decompress(tokens), data)
        << "size = " << data.size() << ", depth = " << options.max_chain_depth
        << ", lazy = " << options.lazy_matching;
}

// A random prefix followed by a copy of its first `marker` bytes: the only
// candidate at the boundary sits exactly at distance `prefix.size()`.
std::vector<std::uint8_t> make_window_case(std::size_t prefix_size, std::size_t marker,
                                           std::uint32_t seed) {
    std::vector<std::uint8_t> data = make_random_data(prefix_size, 256, seed);
    const std::vector<std::uint8_t> tail(data.begin(), data.begin() + static_cast<std::ptrdiff_t>(marker));
    data.insert(data.end(), tail.begin(), tail.end());
    return data;
}

}  // namespace

// Round-trips: empty input, one byte, texts, repetitions, random data.
TEST(Lz77, RoundTripOnEmptyInput) {
    const std::vector<std::uint8_t> empty;
    const std::vector<Lz77Token> tokens = blockzip::lz77_compress(empty);
    EXPECT_TRUE(tokens.empty());
    EXPECT_TRUE(blockzip::lz77_decompress(tokens).empty());
    expect_roundtrip(empty, Lz77Options{0, true});
}

TEST(Lz77, RoundTripOnSingleByte) {
    for (const std::uint8_t value : {std::uint8_t{0x00}, std::uint8_t{0x42}, std::uint8_t{0xFF}}) {
        const std::vector<std::uint8_t> data{value};
        const std::vector<Lz77Token> tokens = blockzip::lz77_compress(data);
        ASSERT_EQ(tokens.size(), 1u);
        EXPECT_TRUE(tokens[0].is_literal());
        EXPECT_EQ(tokens[0].literal, value);
        expect_roundtrip(data);
        expect_roundtrip(data, Lz77Options{0, true});
    }
}

TEST(Lz77, RoundTripOnTextsAndRepetitions) {
    expect_roundtrip(bytes_of("the quick brown fox jumps over the lazy dog"));
    expect_roundtrip(make_pseudo_text(50000));
    expect_roundtrip(repeat("abc", 3000));
    expect_roundtrip(repeat("ababababab", 2000));
    expect_roundtrip(repeat("0123456789", 5000));
    expect_roundtrip(std::vector<std::uint8_t>(100000, 0x00));
    expect_roundtrip(std::vector<std::uint8_t>(100000, 0xFF));
    expect_roundtrip(bytes_of(std::string(5000, 'a') + std::string(5000, 'b')));
}

TEST(Lz77, RoundTripOnRandomData) {
    const std::array<std::size_t, 15> sizes{0,  1,  2,   3,   4,   7,   8,
                                            15, 16, 17,  63,  100, 255, 1000,
                                            5000};
    const std::array<unsigned, 6> alphabets{1, 2, 3, 4, 17, 256};
    const std::array<Lz77Options, 4> option_sets{Lz77Options{64, false}, Lz77Options{64, true},
                                                 Lz77Options{4, false}, Lz77Options{0, false}};

    for (const std::size_t size : sizes) {
        for (const unsigned alphabet : alphabets) {
            for (const std::uint32_t seed : {1u, 2u}) {
                const std::vector<std::uint8_t> data = make_random_data(size, alphabet, seed);
                for (const Lz77Options& options : option_sets) {
                    expect_roundtrip(data, options);
                }
            }
        }
    }
}

// Overlapping matches: the source is closer than the match is long. The
// decoder must copy byte by byte while producing the output.
TEST(Lz77, OverlappingMatchesAreUsedAndDecoded) {
    const std::vector<std::uint8_t> data = repeat("abc", 1000);
    const std::vector<Lz77Token> tokens = blockzip::lz77_compress(data);
    EXPECT_EQ(blockzip::lz77_decompress(tokens), data);

    bool has_overlap = false;
    for (const Lz77Token& token : tokens) {
        if (!token.is_literal() && token.distance < token.length) {
            has_overlap = true;
        }
    }
    EXPECT_TRUE(has_overlap) << "expected a match with distance < length";

    const std::vector<std::uint8_t> runs(10000, 0x5A);
    const std::vector<Lz77Token> run_tokens = blockzip::lz77_compress(runs);
    EXPECT_EQ(blockzip::lz77_decompress(run_tokens), runs);
    bool has_run_match = false;
    for (const Lz77Token& token : run_tokens) {
        if (!token.is_literal() && token.distance == 1 && token.length > token.distance) {
            has_run_match = true;
        }
    }
    EXPECT_TRUE(has_run_match);

    // Direct decoder check: literal 'a' followed by a match at distance 1.
    EXPECT_EQ(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{10, 1, 0}}),
              std::vector<std::uint8_t>(11, 'a'));
}

// Window boundary: a match at distance exactly 32768 is allowed.
TEST(Lz77, MatchAtExactWindowDistance) {
    const std::size_t marker = 16;
    const std::vector<std::uint8_t> data = make_window_case(kWindow, marker, 12345);

    ASSERT_EQ(naive_match_length(data, kWindow), marker)
        << "the constructed marker must be a unique 16-byte suffix of the prefix";

    const std::vector<Lz77Token> tokens =
        blockzip::lz77_compress(data, Lz77Options{0, false});
    const std::optional<LocatedToken> token = token_starting_at(tokens, kWindow);
    ASSERT_TRUE(token.has_value()) << "expected a token exactly at the window boundary";
    EXPECT_FALSE(token->token.is_literal());
    EXPECT_EQ(token->token.length, marker);
    EXPECT_EQ(token->token.distance, kWindow);
    expect_roundtrip(data, Lz77Options{0, false});
}

// Window boundary: a match at distance 32769 is outside the window and must not
// be used; the encoder falls back to literals there.
TEST(Lz77, MatchAtDistance32769IsOutsideWindow) {
    const std::size_t prefix = kWindow + 1;
    const std::size_t marker = 16;
    const std::vector<std::uint8_t> data = make_window_case(prefix, marker, 54321);

    ASSERT_EQ(naive_match_length(data, prefix), 0u)
        << "no match within the window should start at the boundary";

    const std::vector<Lz77Token> tokens =
        blockzip::lz77_compress(data, Lz77Options{0, false});
    const std::optional<LocatedToken> token = token_starting_at(tokens, prefix);
    ASSERT_TRUE(token.has_value()) << "expected a token exactly at the boundary";
    EXPECT_TRUE(token->token.is_literal()) << "distance 32769 must not be used";

    for (const Lz77Token& candidate : tokens) {
        if (!candidate.is_literal()) {
            EXPECT_GE(candidate.distance, 1u);
            EXPECT_LE(candidate.distance, kWindow);
        }
    }
    expect_roundtrip(data, Lz77Options{0, false});
}

// With an unlimited chain the matcher must find exactly what an exhaustive
// search finds at every position where it emits a match.
TEST(Lz77, UnlimitedDepthMatchesNaiveSearch) {
    const std::array<std::size_t, 10> sizes{0, 1, 2, 3, 4, 10, 50, 100, 200, 300};
    const std::array<unsigned, 6> alphabets{1, 2, 3, 4, 17, 256};

    for (const std::size_t size : sizes) {
        for (const unsigned alphabet : alphabets) {
            for (const std::uint32_t seed : {1u, 2u}) {
                const std::vector<std::uint8_t> data = make_random_data(size, alphabet, seed);
                for (const bool lazy : {false, true}) {
                    Lz77Options options;
                    options.max_chain_depth = blockzip::kLz77UnlimitedChainDepth;
                    options.lazy_matching = lazy;

                    const std::vector<Lz77Token> tokens = blockzip::lz77_compress(data, options);
                    std::size_t position = 0;
                    for (const Lz77Token& token : tokens) {
                        if (token.is_literal()) {
                            ASSERT_LT(position, data.size());
                            EXPECT_EQ(token.literal, data[position]);
                            ++position;
                            continue;
                        }
                        ASSERT_LT(position, data.size());
                        EXPECT_GE(token.length, kMinMatch);
                        EXPECT_LE(token.length, kMaxMatch);
                        EXPECT_GE(token.distance, 1u);
                        EXPECT_LE(token.distance, position);
                        EXPECT_EQ(token.length, naive_match_length(data, position))
                            << "size = " << size << ", alphabet = " << alphabet
                            << ", seed = " << seed << ", lazy = " << lazy
                            << ", position = " << position;
                        position += token.length;
                    }
                    EXPECT_EQ(position, data.size());
                    EXPECT_EQ(blockzip::lz77_decompress(tokens), data);
                }
            }
        }
    }
}

// Lazy matching must never produce more tokens than greedy matching on the
// representative texts below, and both must decode correctly.
TEST(Lz77, LazyMatchingIsNoWorseThanGreedy) {
    std::vector<std::vector<std::uint8_t>> texts;
    texts.push_back(make_pseudo_text(50000));
    texts.push_back(repeat("abc", 5000));
    texts.push_back(repeat("0123456789", 5000));
    texts.push_back(make_random_data(20000, 16, 7));
    texts.push_back(bytes_of(std::string(20000, 'a') + std::string(20000, 'b')));
    texts.push_back(repeat("Blockzip blockzip BLOCKZIP ", 3000));

    for (const std::vector<std::uint8_t>& data : texts) {
        Lz77Options greedy;
        greedy.max_chain_depth = 64;
        greedy.lazy_matching = false;
        Lz77Options lazy = greedy;
        lazy.lazy_matching = true;

        const std::vector<Lz77Token> greedy_tokens = blockzip::lz77_compress(data, greedy);
        const std::vector<Lz77Token> lazy_tokens = blockzip::lz77_compress(data, lazy);

        EXPECT_EQ(blockzip::lz77_decompress(greedy_tokens), data);
        EXPECT_EQ(blockzip::lz77_decompress(lazy_tokens), data);
        EXPECT_LE(lazy_tokens.size(), greedy_tokens.size())
            << "size = " << data.size() << ", greedy = " << greedy_tokens.size()
            << ", lazy = " << lazy_tokens.size();
    }
}

// 1 MiB of one repeated byte and 1 MiB of text-like data must finish quickly:
// the depth limit keeps the hash chains from degenerating into O(n^2).
TEST(Lz77, LargeInputsCompleteQuickly) {
    const std::vector<std::uint8_t> runs(1u << 20, 0x00);
    const auto runs_start = std::chrono::steady_clock::now();
    const std::vector<Lz77Token> run_tokens = blockzip::lz77_compress(runs);
    const std::vector<std::uint8_t> run_decoded = blockzip::lz77_decompress(run_tokens);
    const auto runs_elapsed = std::chrono::steady_clock::now() - runs_start;

    EXPECT_EQ(run_decoded, runs);
    EXPECT_LT(std::chrono::duration<double>(runs_elapsed).count(), 30.0);

    const std::vector<std::uint8_t> text = make_pseudo_text(1u << 20);
    const auto text_start = std::chrono::steady_clock::now();
    const std::vector<Lz77Token> text_tokens = blockzip::lz77_compress(text);
    const std::vector<std::uint8_t> text_decoded = blockzip::lz77_decompress(text_tokens);
    const auto text_elapsed = std::chrono::steady_clock::now() - text_start;

    EXPECT_EQ(text_decoded, text);
    EXPECT_LT(std::chrono::duration<double>(text_elapsed).count(), 30.0);
}

// The decoder must reject malformed tokens instead of reading outside the
// already reconstructed data.
TEST(Lz77, DecoderRejectsInvalidTokens) {
    EXPECT_THROW(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{3, 0, 0}}),
                 Lz77Error);
    EXPECT_THROW(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{3, 2, 0}}),
                 Lz77Error);
    EXPECT_THROW(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{1, 1, 0}}),
                 Lz77Error);
    EXPECT_THROW(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{2, 1, 0}}),
                 Lz77Error);
    EXPECT_THROW(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{259, 1, 0}}),
                 Lz77Error);

    std::vector<Lz77Token> too_far(40000, Lz77Token{0, 0, 'a'});
    too_far.push_back(Lz77Token{3, 40000, 0});
    EXPECT_THROW(blockzip::lz77_decompress(too_far), Lz77Error);

    // Positive control: overlapping copy is valid.
    EXPECT_EQ(blockzip::lz77_decompress({Lz77Token{0, 0, 'a'}, Lz77Token{10, 1, 0}}),
              std::vector<std::uint8_t>(11, 'a'));
}

// Randomly corrupted token streams must either decode or throw Lz77Error; they
// must never crash, corrupt memory or hang.
TEST(Lz77, RandomTokenCorruptionNeverCrashes) {
    std::mt19937 rng(321);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const std::vector<std::uint8_t> data =
            make_random_data(1 + static_cast<std::size_t>(iteration) % 200,
                             1 + static_cast<unsigned>(iteration) % 8, iteration);
        std::vector<Lz77Token> tokens = blockzip::lz77_compress(data, Lz77Options{8, false});

        for (Lz77Token& token : tokens) {
            if (rng() % 4 == 0) {
                token.length = static_cast<std::uint16_t>(rng() % 300);
            }
            if (rng() % 4 == 0) {
                token.distance = static_cast<std::uint16_t>(rng() % 40000);
            }
            if (rng() % 4 == 0) {
                token.literal = static_cast<std::uint8_t>(rng());
            }
        }

        try {
            (void)blockzip::lz77_decompress(tokens);
        } catch (const Lz77Error&) {
            // Rejecting the corrupted stream is the expected outcome.
        }
    }

    for (int iteration = 0; iteration < 100; ++iteration) {
        std::vector<Lz77Token> tokens;
        const std::size_t count = rng() % 50;
        tokens.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            Lz77Token token;
            token.length = static_cast<std::uint16_t>(rng() % 300);
            token.distance = static_cast<std::uint16_t>(rng() % 40000);
            token.literal = static_cast<std::uint8_t>(rng());
            tokens.push_back(token);
        }
        try {
            (void)blockzip::lz77_decompress(tokens);
        } catch (const Lz77Error&) {
            // Expected for most random streams.
        }
    }
}
