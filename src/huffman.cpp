#include "huffman.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace blockzip {
namespace {

constexpr std::uint32_t kKraftLimit = 1u << kMaxCodeLength;

// Package-merge works on two kinds of items: leaves (coins of one symbol at one
// denomination) and packages of two items. Nodes are kept in an arena and
// referred to by index, so packages never copy the leaves they contain, and the
// same leaf node can be reused at every denomination level.
struct Node {
    std::uint64_t weight = 0;
    std::uint16_t symbol = 0;
    std::int32_t left = -1;
    std::int32_t right = -1;
    bool leaf = false;
    std::uint32_t order = 0;
};

// Deterministic total order: lighter first, then leaves before packages, then
// by symbol or creation order. Ties therefore never depend on the sort.
bool node_less(const Node& lhs, const Node& rhs) noexcept {
    if (lhs.weight != rhs.weight) {
        return lhs.weight < rhs.weight;
    }
    if (lhs.leaf != rhs.leaf) {
        return lhs.leaf;
    }
    return lhs.order < rhs.order;
}

class Arena {
public:
    std::uint32_t add(Node node) {
        nodes_.push_back(node);
        return static_cast<std::uint32_t>(nodes_.size() - 1);
    }

    const Node& operator[](std::uint32_t index) const noexcept { return nodes_[index]; }

    std::uint32_t add_package(std::uint32_t left, std::uint32_t right, std::uint32_t order) {
        Node package;
        package.weight = nodes_[left].weight + nodes_[right].weight;
        package.leaf = false;
        package.left = static_cast<std::int32_t>(left);
        package.right = static_cast<std::int32_t>(right);
        package.order = order;
        return add(package);
    }

private:
    std::vector<Node> nodes_;
};

// Packages adjacent items pairwise; the highest-value leftover is dropped, as
// the coin-collector formulation requires. Sums of adjacent items of a sorted
// list are non-decreasing, so the package list is already sorted.
std::vector<std::uint32_t> package_pairs(const std::vector<std::uint32_t>& items, Arena& arena,
                                         std::uint32_t& next_order) {
    std::vector<std::uint32_t> packages;
    packages.reserve(items.size() / 2);
    for (std::size_t index = 0; index + 1 < items.size(); index += 2) {
        packages.push_back(arena.add_package(items[index], items[index + 1], next_order++));
    }
    return packages;
}

std::vector<std::uint32_t> merge_with_leaves(const std::vector<std::uint32_t>& leaves,
                                             const std::vector<std::uint32_t>& packages,
                                             const Arena& arena) {
    std::vector<std::uint32_t> merged;
    merged.reserve(leaves.size() + packages.size());
    const auto less = [&arena](std::uint32_t lhs, std::uint32_t rhs) {
        return node_less(arena[lhs], arena[rhs]);
    };
    std::merge(leaves.begin(), leaves.end(), packages.begin(), packages.end(),
               std::back_inserter(merged), less);
    return merged;
}

// Number of codes of each length; counts[0] stays zero so the canonical
// first-code formula can use counts[length - 1] directly.
std::array<std::uint32_t, kMaxCodeLength + 1> count_code_lengths(const CodeLengths& lengths) {
    std::array<std::uint32_t, kMaxCodeLength + 1> counts{};
    for (const std::uint8_t length : lengths) {
        if (length > kMaxCodeLength) {
            throw std::logic_error("huffman: code length above the maximum");
        }
        if (length != 0) {
            ++counts[length];
        }
    }
    return counts;
}

std::uint16_t reverse_bits(std::uint32_t code, unsigned length) noexcept {
    std::uint32_t reversed = 0;
    for (unsigned index = 0; index < length; ++index) {
        reversed = (reversed << 1) | (code & 1u);
        code >>= 1;
    }
    return static_cast<std::uint16_t>(reversed);
}

struct CanonicalCode {
    std::array<std::uint32_t, kAlphabetSize> code{};
    std::array<std::uint16_t, kAlphabetSize> reversed{};
    std::array<std::uint8_t, kAlphabetSize> length{};
};

CanonicalCode build_canonical_codes(const CodeLengths& lengths) {
    CanonicalCode table;
    const auto counts = count_code_lengths(lengths);

    std::array<std::uint32_t, kMaxCodeLength + 1> next_code{};
    std::uint32_t next = 0;
    for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
        next = (next + counts[length - 1]) << 1;
        next_code[length] = next;
    }

    for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol) {
        const unsigned length = lengths[symbol];
        if (length == 0) {
            continue;
        }
        const std::uint32_t assigned = next_code[length]++;
        table.code[symbol] = assigned;
        table.reversed[symbol] = reverse_bits(assigned, length);
        table.length[symbol] = static_cast<std::uint8_t>(length);
    }
    return table;
}

bool lengths_are_valid(const Frequencies& frequencies, const CodeLengths& lengths,
                       unsigned max_code_length) noexcept {
    for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol) {
        if (frequencies[symbol] == 0) {
            continue;
        }
        if (lengths[symbol] == 0 || lengths[symbol] > max_code_length) {
            return false;
        }
    }
    return kraft_sum_units(lengths) <= kKraftLimit;
}

}  // namespace

Frequencies count_frequencies(const std::uint8_t* data, std::size_t size) noexcept {
    Frequencies frequencies{};
    for (std::size_t index = 0; index < size; ++index) {
        ++frequencies[data[index]];
    }
    return frequencies;
}

Frequencies count_frequencies(const std::vector<std::uint8_t>& data) noexcept {
    return count_frequencies(data.data(), data.size());
}

std::uint32_t kraft_sum_units(const CodeLengths& lengths) noexcept {
    std::uint32_t units = 0;
    for (const std::uint8_t length : lengths) {
        if (length == 0) {
            continue;
        }
        if (length > kMaxCodeLength) {
            return std::numeric_limits<std::uint32_t>::max();
        }
        units += 1u << (kMaxCodeLength - length);
    }
    return units;
}

CodeLengths build_code_lengths(const Frequencies& frequencies, unsigned max_code_length) {
    if (max_code_length == 0 || max_code_length > kMaxCodeLength) {
        throw std::invalid_argument("build_code_lengths: max_code_length must be in [1, 15]");
    }

    CodeLengths lengths{};

    std::vector<std::uint16_t> used_symbols;
    used_symbols.reserve(kAlphabetSize);
    for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol) {
        if (frequencies[symbol] != 0) {
            used_symbols.push_back(static_cast<std::uint16_t>(symbol));
        }
    }

    const std::size_t used = used_symbols.size();
    if (used == 0) {
        return lengths;
    }
    if (used == 1) {
        lengths[used_symbols.front()] = 1;
        return lengths;
    }
    if (used > (std::size_t{1} << max_code_length)) {
        throw std::invalid_argument("build_code_lengths: alphabet does not fit the length limit");
    }

    // Reduction to the coin collector's problem: every used symbol owns coins
    // of denominations 2^-1 .. 2^-max_code_length, all with numismatic value
    // equal to its frequency. Selecting the cheapest coins worth `used - 1`
    // dollars, where selecting a coin of denomination 2^-len means the code has
    // length `len`, gives the optimal length-limited code.
    //
    // Package-merge processes the denominations from the smallest upwards:
    // package the current list, merge it into the next denomination's list.
    // After the final merge the packages are the 1-dollar items; picking the
    // cheapest `used - 1` of them is the greedy optimum.
    Arena arena;
    std::uint32_t next_order = 0;

    std::vector<std::uint32_t> leaf_nodes;
    leaf_nodes.reserve(used);
    for (const std::uint16_t symbol : used_symbols) {
        Node leaf;
        leaf.weight = frequencies[symbol];
        leaf.symbol = symbol;
        leaf.leaf = true;
        leaf.order = symbol;
        leaf_nodes.push_back(arena.add(leaf));
    }
    const auto leaf_less = [&arena](std::uint32_t lhs, std::uint32_t rhs) {
        return node_less(arena[lhs], arena[rhs]);
    };
    std::sort(leaf_nodes.begin(), leaf_nodes.end(), leaf_less);

    std::vector<std::uint32_t> current = leaf_nodes;
    for (unsigned level = 1; level < max_code_length; ++level) {
        const std::vector<std::uint32_t> packages =
            package_pairs(current, arena, next_order);
        current = merge_with_leaves(leaf_nodes, packages, arena);
    }
    const std::vector<std::uint32_t> dollars = package_pairs(current, arena, next_order);

    if (dollars.size() < used - 1) {
        throw std::logic_error("build_code_lengths: not enough 1-dollar items");
    }

    std::vector<std::uint32_t> stack;
    for (std::size_t item = 0; item + 1 < used; ++item) {
        stack.clear();
        stack.push_back(dollars[item]);
        while (!stack.empty()) {
            const Node node = arena[stack.back()];
            stack.pop_back();
            if (node.leaf) {
                ++lengths[node.symbol];
            } else {
                stack.push_back(static_cast<std::uint32_t>(node.left));
                stack.push_back(static_cast<std::uint32_t>(node.right));
            }
        }
    }

    if (!lengths_are_valid(frequencies, lengths, max_code_length)) {
        throw std::logic_error("build_code_lengths: produced an invalid code");
    }
    return lengths;
}

void huffman_encode(const std::uint8_t* data, std::size_t size, BitWriter& out) {
    const Frequencies frequencies = count_frequencies(data, size);
    const CodeLengths lengths = build_code_lengths(frequencies);

    for (const std::uint8_t length : lengths) {
        out.write_bits(length, kCodeLengthBits);
    }
    if (size == 0) {
        return;
    }

    const CanonicalCode table = build_canonical_codes(lengths);
    for (std::size_t index = 0; index < size; ++index) {
        const std::uint8_t symbol = data[index];
        const unsigned length = table.length[symbol];
        if (length == 0) {
            throw std::logic_error("huffman_encode: symbol without a code");
        }
        // BitWriter emits the least significant bit first, so the reversed code
        // lands on the wire most significant bit first, as canonical Huffman
        // requires.
        out.write_bits(table.reversed[symbol], length);
    }
}

void huffman_decode(BitReader& in, std::size_t size, std::vector<std::uint8_t>& out) {
    CodeLengths lengths{};
    for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol) {
        lengths[symbol] = static_cast<std::uint8_t>(in.read_bits(kCodeLengthBits));
    }

    const std::uint32_t kraft = kraft_sum_units(lengths);
    if (kraft > kKraftLimit) {
        throw BitStreamError("huffman_decode: code lengths violate the Kraft inequality");
    }
    if (size != 0 && kraft == 0) {
        throw BitStreamError("huffman_decode: empty code table for a non-empty payload");
    }

    out.assign(size, std::uint8_t{0});
    if (size == 0) {
        return;
    }

    std::array<std::uint32_t, kMaxCodeLength + 1> counts{};
    std::array<std::uint32_t, kMaxCodeLength + 1> first_code{};
    std::array<std::uint32_t, kMaxCodeLength + 1> first_index{};
    std::array<std::uint8_t, kAlphabetSize> ordered_symbols{};

    std::size_t offset = 0;
    for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
        for (std::size_t symbol = 0; symbol < kAlphabetSize; ++symbol) {
            if (lengths[symbol] == length) {
                ordered_symbols[offset++] = static_cast<std::uint8_t>(symbol);
                ++counts[length];
            }
        }
    }

    std::uint32_t next = 0;
    std::uint32_t sorted_offset = 0;
    for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
        next = (next + counts[length - 1]) << 1;
        first_code[length] = next;
        first_index[length] = sorted_offset;
        sorted_offset += counts[length];
    }

    for (std::size_t index = 0; index < size; ++index) {
        std::uint32_t code = 0;
        int decoded = -1;
        for (unsigned length = 1; length <= kMaxCodeLength; ++length) {
            code = (code << 1) | (in.read_bit() ? 1u : 0u);
            if (counts[length] != 0 && code >= first_code[length] &&
                code - first_code[length] < counts[length]) {
                const std::uint32_t position = first_index[length] + (code - first_code[length]);
                decoded = static_cast<int>(ordered_symbols[position]);
                break;
            }
        }
        if (decoded < 0) {
            throw BitStreamError("huffman_decode: invalid Huffman code");
        }
        out[index] = static_cast<std::uint8_t>(decoded);
    }
}

std::vector<std::uint8_t> huffman_encode(const std::uint8_t* data, std::size_t size) {
    BitWriter writer;
    huffman_encode(data, size, writer);
    return writer.bytes();
}

std::vector<std::uint8_t> huffman_encode(const std::vector<std::uint8_t>& data) {
    return huffman_encode(data.data(), data.size());
}

std::vector<std::uint8_t> huffman_decode(const std::uint8_t* data, std::size_t data_size,
                                         std::size_t size) {
    BitReader reader(data, data_size);
    std::vector<std::uint8_t> out;
    huffman_decode(reader, size, out);
    return out;
}

std::vector<std::uint8_t> huffman_decode(const std::vector<std::uint8_t>& encoded,
                                         std::size_t size) {
    return huffman_decode(encoded.data(), encoded.size(), size);
}

}  // namespace blockzip
