#include "image.hpp"
#include <lzo/lzo1x.h>
#include <cstring>
#include <stdexcept>
#include <algorithm>

static const int MAX_FRAGMENT_DATA = 250;

std::vector<uint8_t> pack_row(const std::vector<int>& pixels, int bits_per_pixel) {
    int ppb = 8 / bits_per_pixel;  // pixels per byte
    int width = (int)pixels.size();
    int bytes_per_row = width / ppb;
    std::vector<uint8_t> row_bytes(bytes_per_row, 0);

    for (int byte_idx = 0; byte_idx < bytes_per_row; byte_idx++) {
        int pixel_offset = (bytes_per_row - 1 - byte_idx) * ppb;
        uint8_t val = 0;
        for (int i = 0; i < ppb; i++) {
            val |= static_cast<uint8_t>(pixels[pixel_offset + i]) << (i * bits_per_pixel);
        }
        row_bytes[byte_idx] = val;
    }

    return row_bytes;
}

std::vector<uint8_t> pack_pixels(const std::vector<std::vector<int>>& pixels, int bits_per_pixel) {
    std::vector<uint8_t> result;
    for (const auto& row : pixels) {
        auto packed_row = pack_row(row, bits_per_pixel);
        result.insert(result.end(), packed_row.begin(), packed_row.end());
    }
    return result;
}

/// Rotate a 2D pixel array 90 degrees clockwise.
/// Input shape: (H, W) -> Output shape: (W, H)
std::vector<std::vector<int>> rotate_cw90(const std::vector<std::vector<int>>& pixels) {
    int h = (int)pixels.size();
    int w = (int)pixels[0].size();
    std::vector<std::vector<int>> rotated(w, std::vector<int>(h));
    for (int r = 0; r < w; r++) {
        for (int c = 0; c < h; c++) {
            rotated[r][c] = pixels[h - 1 - c][r];
        }
    }
    return rotated;
}

std::vector<std::vector<uint8_t>> split_blocks(const std::vector<uint8_t>& packed,
                                                const std::vector<int>& block_sizes) {
    std::vector<std::vector<uint8_t>> blocks;
    size_t offset = 0;
    for (int size : block_sizes) {
        size_t end = std::min(offset + size, packed.size());
        blocks.emplace_back(packed.begin() + offset, packed.begin() + end);
        offset = end;
    }
    return blocks;
}

std::vector<uint8_t> compress_block(const std::vector<uint8_t>& block) {
    static bool initialized = false;
    if (!initialized) {
        if (lzo_init() != LZO_E_OK) {
            throw std::runtime_error("LZO initialization failed");
        }
        initialized = true;
    }

    std::vector<uint8_t> wrkmem(LZO1X_1_MEM_COMPRESS, 0);
    lzo_uint out_len = block.size() + block.size() / 16 + 64 + 3;
    std::vector<uint8_t> out(out_len);

    int ret = lzo1x_1_compress(
        block.data(), (lzo_uint)block.size(),
        out.data(), &out_len,
        wrkmem.data()
    );

    if (ret != LZO_E_OK) {
        throw std::runtime_error("LZO compression failed");
    }

    out.resize(out_len);
    return out;
}

std::vector<std::vector<uint8_t>> make_fragments(const std::vector<uint8_t>& compressed) {
    std::vector<std::vector<uint8_t>> fragments;
    for (size_t i = 0; i < compressed.size(); i += MAX_FRAGMENT_DATA) {
        size_t end = std::min(i + MAX_FRAGMENT_DATA, compressed.size());
        fragments.emplace_back(compressed.begin() + i, compressed.begin() + end);
    }
    return fragments;
}

std::vector<std::vector<Apdu>> encode_image(const std::vector<std::vector<int>>& pixels,
                                             const DeviceInfo& device_info) {
    int bpp = device_info.bits_per_pixel;
    auto bsizes = device_info.block_sizes();

    // Rotate pixels 90° CW for rotated panels (e.g. 296×128)
    auto effective_pixels = device_info.rotated() ? rotate_cw90(pixels) : pixels;

    auto packed = pack_pixels(effective_pixels, bpp);
    auto blocks = split_blocks(packed, bsizes);

    std::vector<std::vector<Apdu>> all_apdus;

    for (size_t block_no = 0; block_no < blocks.size(); block_no++) {
        auto compressed = compress_block(blocks[block_no]);
        auto fragments = make_fragments(compressed);

        std::vector<Apdu> block_apdus;
        for (size_t frag_no = 0; frag_no < fragments.size(); frag_no++) {
            bool is_final = (frag_no == fragments.size() - 1);
            block_apdus.push_back(build_image_data_apdu((int)block_no, (int)frag_no, fragments[frag_no], is_final));
        }
        all_apdus.push_back(block_apdus);
    }

    return all_apdus;
}
