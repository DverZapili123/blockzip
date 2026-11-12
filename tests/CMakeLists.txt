add_executable(blockzip_tests
    test_example.cpp
    test_bitstream.cpp
    test_crc32.cpp
    test_huffman.cpp
)

target_link_libraries(blockzip_tests PRIVATE blockzip_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(blockzip_tests DISCOVERY_TIMEOUT 60)
