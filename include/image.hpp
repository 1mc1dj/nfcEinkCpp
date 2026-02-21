#pragma once

#include <cstdint>
#include <vector>
#include "protocol.hpp"

/// Pack a single row of color indices into bytes (right-to-left byte order)
std::vector<uint8_t> pack_row(const std::vector<int>& pixels, int bits_per_pixel = 2);

/// Pack a full screen of pixels into bytes
std::vector<uint8_t> pack_pixels(const std::vector<std::vector<int>>& pixels, int bits_per_pixel = 2);

/// Split packed data into blocks
std::vector<std::vector<uint8_t>> split_blocks(const std::vector<uint8_t>& packed,
                                                const std::vector<int>& block_sizes);

/// Compress a block using LZO1X-1
std::vector<uint8_t> compress_block(const std::vector<uint8_t>& block);

/// Split compressed data into fragments (max 250 bytes each)
std::vector<std::vector<uint8_t>> make_fragments(const std::vector<uint8_t>& compressed);

/// Encode a full image into APDU commands
std::vector<std::vector<Apdu>> encode_image(const std::vector<std::vector<int>>& pixels,
                                             const DeviceInfo& device_info);
