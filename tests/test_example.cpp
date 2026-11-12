#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Example test for stage 0: it proves that the CMake + GoogleTest harness is
// wired up and that the compiler really is in C++20 mode.
TEST(BuildHarness, CompilerSupportsCpp20) {
    EXPECT_GE(__cplusplus, 202002L);
}

TEST(BuildHarness, StandardLibraryIsUsable) {
    const std::vector<std::uint8_t> bytes{1, 2, 3};
    EXPECT_EQ(bytes.size(), 3u);

    const std::string text = "blockzip";
    EXPECT_EQ(text.size(), 8u);
}
