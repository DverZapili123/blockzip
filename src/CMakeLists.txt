# Core library: bit-level I/O, checksums and (later) the codecs themselves.
add_library(blockzip_core STATIC
    bitstream.cpp
    bitstream.h
    crc32.cpp
    crc32.h
    huffman.cpp
    huffman.h
    lz77.cpp
    lz77.h
)

target_include_directories(blockzip_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(blockzip_core PUBLIC blockzip_warnings)
