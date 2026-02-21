#pragma once

#include <cstdint>
#include <vector>
#include <array>

/// RGB color
struct Color {
    int r, g, b;
};

/// 4-color palette: black, white, yellow, red
static const std::array<Color, 4> PALETTE_4COLOR = {{
    {0, 0, 0},       // 0: black
    {255, 255, 255},  // 1: white
    {255, 255, 0},    // 2: yellow
    {255, 0, 0},      // 3: red
}};

/// Load an image file and resize/fit to target dimensions with background color
/// Returns pixel data as RGB (w * h * 3)
std::vector<uint8_t> load_and_resize_image(const char* path,
                                            int target_w, int target_h,
                                            Color bg_color,
                                            const std::string& resize_mode = "fit");

/// Apply Atkinson dithering to an RGB image, producing a 2D color index array
std::vector<std::vector<int>> dither_atkinson(const std::vector<uint8_t>& rgb,
                                               int width, int height,
                                               const std::array<Color, 4>& palette = PALETTE_4COLOR);

/// Nearest-color quantization (no dithering)
std::vector<std::vector<int>> dither_none(const std::vector<uint8_t>& rgb,
                                           int width, int height,
                                           const std::array<Color, 4>& palette = PALETTE_4COLOR);
