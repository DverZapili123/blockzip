#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "crc32.h"

namespace {

using blockzip::Crc32;

std::uint32_t crc_of(const std::string& text) {
    return blockzip::crc32(text.data(), text.size());
}

std::vector<std::uint8_t> make_random_bytes(std::size_t size, std::uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_int_distribution<unsigned> dist(0, 255);
    std::vector<std::uint8_t> bytes(size);
    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(dist(rng));
    }
    return bytes;
}

}  // namespace

// Acceptance (SPEC, stage 1): crc32("123456789") == 0xCBF43926.
TEST(Crc32, StandardCheckValue) {
    EXPECT_EQ(crc_of("123456789"), 0xCBF43926u);
}

TEST(Crc32, EmptyInputIsZero) {
    EXPECT_EQ(blockzip::crc32(nullptr, 0), 0u);
    EXPECT_EQ(crc_of(""), 0u);
}

// Additional well-known vectors of the IEEE 802.3 (reflected) polynomial.
TEST(Crc32, KnownVectors) {
    EXPECT_EQ(crc_of("a"), 0xE8B7BE43u);
    EXPECT_EQ(crc_of("abc"), 0x352441C2u);
    EXPECT_EQ(crc_of("The quick brown fox jumps over the lazy dog"), 0x414FA339u);
}

TEST(Crc32, IncrementalMatchesOneShotAtEverySplitPoint) {
    const std::vector<std::uint8_t> bytes = make_random_bytes(200, 12345);
    const std::uint32_t expected = blockzip::crc32(bytes.data(), bytes.size());

    for (std::size_t split = 0; split <= bytes.size(); ++split) {
        Crc32 incremental;
        incremental.update(bytes.data(), split);
        incremental.update(bytes.data() + split, bytes.size() - split);
        EXPECT_EQ(incremental.value(), expected) << "split = " << split;
    }
}

TEST(Crc32, ByteByByteIncrementalMatchesOneShot) {
    const std::vector<std::uint8_t> bytes = make_random_bytes(257, 999);
    Crc32 incremental;
    for (std::uint8_t byte : bytes) {
        incremental.update(&byte, 1);
        // value() must be queryable mid-stream without disturbing the state.
        (void)incremental.value();
    }
    EXPECT_EQ(incremental.value(), blockzip::crc32(bytes.data(), bytes.size()));
}

TEST(Crc32, ResetStartsOver) {
    const std::string first = "123456789";
    const std::string second = "abc";

    Crc32 crc;
    crc.update(first.data(), first.size());
    EXPECT_EQ(crc.value(), 0xCBF43926u);

    crc.reset();
    crc.update(second.data(), second.size());
    EXPECT_EQ(crc.value(), 0x352441C2u);
}

TEST(Crc32, SingleBitFlipChangesChecksum) {
    std::vector<std::uint8_t> bytes = make_random_bytes(64, 7);
    const std::uint32_t before = blockzip::crc32(bytes.data(), bytes.size());

    bytes[10] ^= 0x01u;
    const std::uint32_t after = blockzip::crc32(bytes.data(), bytes.size());

    EXPECT_NE(before, after);
}
