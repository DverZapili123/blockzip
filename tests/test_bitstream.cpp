#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <vector>

#include "bitstream.h"

namespace {

using blockzip::BitReader;
using blockzip::BitStreamError;
using blockzip::BitWriter;

struct WriteOp {
    std::uint32_t value;
    unsigned bits;
};

// Mask of the low `bits` bits; handles 0 and 32 without undefined shifts.
std::uint32_t low_mask(unsigned bits) {
    return bits >= 32 ? 0xFFFFFFFFu : (bits == 0 ? 0u : ((1u << bits) - 1u));
}

std::vector<WriteOp> make_random_ops(std::mt19937& rng, std::size_t count) {
    std::vector<WriteOp> ops;
    ops.reserve(count);
    std::uniform_int_distribution<unsigned> bits_dist(0, 32);
    for (std::size_t i = 0; i < count; ++i) {
        const unsigned bits = bits_dist(rng);
        const std::uint32_t mask = low_mask(bits);
        std::uniform_int_distribution<std::uint32_t> value_dist(0, mask);
        ops.push_back(WriteOp{value_dist(rng), bits});
    }
    return ops;
}

// Writes `ops`, then reads them back and checks the payload and the bit accounting.
void expect_roundtrip(const std::vector<WriteOp>& ops) {
    BitWriter writer;
    std::size_t expected_bits = 0;
    for (const WriteOp& op : ops) {
        writer.write_bits(op.value, op.bits);
        expected_bits += op.bits;
    }

    EXPECT_EQ(writer.bit_size(), expected_bits);
    EXPECT_EQ(writer.bytes().size(), (expected_bits + 7) / 8);

    BitReader reader(writer.bytes(), writer.bit_size());
    EXPECT_EQ(reader.bits_remaining(), expected_bits);
    EXPECT_EQ(reader.bit_position(), 0u);

    for (std::size_t i = 0; i < ops.size(); ++i) {
        EXPECT_EQ(reader.read_bits(ops[i].bits), ops[i].value & low_mask(ops[i].bits)) << "op #" << i;
    }

    EXPECT_EQ(reader.bits_remaining(), 0u);
    EXPECT_EQ(reader.bit_position(), expected_bits);
}

}  // namespace

// Acceptance (SPEC, stage 1): round-trip of bit sequences of arbitrary length.
TEST(BitStream, RoundTripEveryLengthUpTo64) {
    std::mt19937 rng(2024);
    for (unsigned length = 0; length <= 64; ++length) {
        BitWriter writer;
        std::vector<bool> bits(length);
        std::uniform_int_distribution<int> bit_dist(0, 1);
        for (unsigned i = 0; i < length; ++i) {
            bits[i] = bit_dist(rng) != 0;
            writer.write_bit(bits[i]);
        }

        EXPECT_EQ(writer.bit_size(), length);
        EXPECT_EQ(writer.bytes().size(), (length + 7) / 8);

        BitReader reader(writer.bytes(), writer.bit_size());
        for (unsigned i = 0; i < length; ++i) {
            EXPECT_EQ(reader.read_bit(), bits[i]) << "length = " << length << ", bit = " << i;
        }
        EXPECT_EQ(reader.bits_remaining(), 0u);
    }
}

TEST(BitStream, RoundTripRandomSequences) {
    std::mt19937 rng(42);
    for (int iteration = 0; iteration < 100; ++iteration) {
        const std::size_t count = static_cast<std::size_t>(rng() % 200);
        expect_roundtrip(make_random_ops(rng, count));
    }
}

TEST(BitStream, RoundTripLargeStream) {
    std::mt19937 rng(7);
    expect_roundtrip(make_random_ops(rng, 50000));
}

TEST(BitStream, RoundTripWideValues) {
    BitWriter writer;
    writer.write_bits(0xFFFFFFFFu, 32);
    writer.write_bits(0x00000001u, 32);
    writer.write_bits(0x80000000u, 32);
    writer.write_bits(0x00000000u, 32);
    EXPECT_EQ(writer.bit_size(), 128u);

    BitReader reader(writer.bytes());
    EXPECT_EQ(reader.read_bits(32), 0xFFFFFFFFu);
    EXPECT_EQ(reader.read_bits(32), 0x00000001u);
    EXPECT_EQ(reader.read_bits(32), 0x80000000u);
    EXPECT_EQ(reader.read_bits(32), 0x00000000u);
    EXPECT_EQ(reader.bits_remaining(), 0u);
}

TEST(BitStream, BitsArePackedLeastSignificantBitFirst) {
    BitWriter writer;
    writer.write_bits(0b101u, 3);
    ASSERT_EQ(writer.bytes().size(), 1u);
    EXPECT_EQ(writer.bytes()[0], 0x05u);

    BitWriter crossing;
    crossing.write_bits(0x1FFu, 9);  // nine ones -> 0xFF then 0x01
    ASSERT_EQ(crossing.bytes().size(), 2u);
    EXPECT_EQ(crossing.bytes()[0], 0xFFu);
    EXPECT_EQ(crossing.bytes()[1], 0x01u);
}

TEST(BitStream, WritingZeroBitsIsNoOp) {
    BitWriter writer;
    writer.write_bits(0xDEADBEEFu, 0);
    EXPECT_EQ(writer.bit_size(), 0u);
    EXPECT_TRUE(writer.bytes().empty());
}

TEST(BitStream, ReadingZeroBitsReturnsZero) {
    BitWriter writer;
    writer.write_bits(0b1u, 1);
    BitReader reader(writer.bytes(), writer.bit_size());
    EXPECT_EQ(reader.read_bits(0), 0u);
    EXPECT_EQ(reader.bits_remaining(), 1u);
    EXPECT_EQ(reader.read_bits(1), 1u);
}

TEST(BitStream, HighBitsOfValueAreIgnored) {
    BitWriter writer;
    writer.write_bits(0xFFu, 3);
    BitReader reader(writer.bytes());
    EXPECT_EQ(reader.read_bits(3), 0x7u);
}

TEST(BitStream, LastByteIsZeroPadded) {
    BitWriter writer;
    writer.write_bits(0x3u, 2);
    ASSERT_EQ(writer.bytes().size(), 1u);
    EXPECT_EQ(writer.bytes()[0] & 0xFCu, 0u);
}

// Acceptance (SPEC, stage 1): reading past the end of the stream throws.
TEST(BitStream, ReadingPastEndThrows) {
    BitWriter writer;
    writer.write_bits(0xABu, 8);

    BitReader reader(writer.bytes());
    EXPECT_EQ(reader.read_bits(8), 0xABu);

    EXPECT_THROW(reader.read_bits(9), BitStreamError);
    EXPECT_THROW(reader.read_bits(1), BitStreamError);
    EXPECT_THROW(reader.read_bit(), BitStreamError);
    // A failed read must not silently advance the position.
    EXPECT_EQ(reader.bits_remaining(), 0u);
    EXPECT_EQ(reader.bit_position(), 8u);
}

TEST(BitStream, ReadingPastEndThrowsStdRuntimeError) {
    BitWriter writer;
    writer.write_bits(0x1u, 1);
    BitReader reader(writer.bytes(), writer.bit_size());
    EXPECT_THROW(reader.read_bits(2), std::runtime_error);
}

// Padding bits of the last byte must stay unreadable when the payload length is
// shorter than the buffer: otherwise a decoder would consume garbage as data.
TEST(BitStream, PaddingBitsAreNotReadable) {
    BitWriter writer;
    writer.write_bits(0b101u, 3);
    ASSERT_EQ(writer.bytes().size(), 1u);

    BitReader reader(writer.bytes(), writer.bit_size());
    EXPECT_EQ(reader.bits_remaining(), 3u);
    EXPECT_EQ(reader.read_bits(3), 0b101u);
    EXPECT_EQ(reader.bits_remaining(), 0u);
    EXPECT_THROW(reader.read_bits(1), BitStreamError);

    // The same buffer without a payload length exposes all eight bits.
    BitReader padded(writer.bytes());
    EXPECT_EQ(padded.bits_remaining(), 8u);
    EXPECT_EQ(padded.read_bits(3), 0b101u);
    EXPECT_EQ(padded.read_bits(5), 0u);
}

TEST(BitStream, PayloadLongerThanBufferIsRejected) {
    const std::vector<std::uint8_t> bytes{0xFF};
    EXPECT_THROW(BitReader(bytes, 9), std::invalid_argument);
    EXPECT_NO_THROW(BitReader(bytes, 8));
}

TEST(BitStream, EmptyStreamThrowsOnAnyRead) {
    const std::vector<std::uint8_t> empty;
    BitReader reader(empty);
    EXPECT_EQ(reader.bits_remaining(), 0u);
    EXPECT_THROW(reader.read_bits(1), BitStreamError);
    EXPECT_THROW(reader.read_bits(32), BitStreamError);
    EXPECT_THROW(reader.read_bit(), BitStreamError);
}

TEST(BitStream, ReaderFromRawPointerMatchesVectorOverload) {
    BitWriter writer;
    writer.write_bits(0x1234u, 16);
    const std::vector<std::uint8_t>& bytes = writer.bytes();

    BitReader reader(bytes.data(), bytes.size());
    EXPECT_EQ(reader.bits_remaining(), 16u);
    EXPECT_EQ(reader.read_bits(16), 0x1234u);
    EXPECT_EQ(reader.bits_remaining(), 0u);
}

TEST(BitStream, WriterClearResetsState) {
    BitWriter writer;
    writer.write_bits(0xFFFFu, 16);
    EXPECT_EQ(writer.bit_size(), 16u);

    writer.clear();
    EXPECT_EQ(writer.bit_size(), 0u);
    EXPECT_TRUE(writer.bytes().empty());

    writer.write_bits(0x5u, 3);
    EXPECT_EQ(writer.bit_size(), 3u);
    ASSERT_EQ(writer.bytes().size(), 1u);
    EXPECT_EQ(writer.bytes()[0], 0x5u);
}

TEST(BitStream, AlternatingBitsSurviveManyBytes) {
    BitWriter writer;
    std::vector<bool> pattern;
    for (int i = 0; i < 1000; ++i) {
        const bool bit = (i % 3) == 0;
        pattern.push_back(bit);
        writer.write_bit(bit);
    }

    BitReader reader(writer.bytes());
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        EXPECT_EQ(reader.read_bit(), pattern[i]) << "bit #" << i;
    }
    EXPECT_EQ(reader.bits_remaining(), 0u);
}

TEST(BitStream, RejectsBitCountAboveThirtyTwo) {
    BitWriter writer;
    EXPECT_THROW(writer.write_bits(0x1u, 33), std::invalid_argument);

    writer.write_bits(0x1u, 8);
    BitReader reader(writer.bytes());
    EXPECT_THROW(reader.read_bits(33), std::invalid_argument);
}
