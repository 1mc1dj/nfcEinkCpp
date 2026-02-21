#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#include "stb_image.h"
#include "dither.hpp"
#include <cmath>
#include <stdexcept>
#include <algorithm>
#include <string>

// --- Image loading and resizing ---

std::vector<uint8_t> load_and_resize_image(const char* path,
                                            int target_w, int target_h,
                                            Color bg_color,
                                            const std::string& resize_mode) {
    int w, h, channels;
    unsigned char* data = stbi_load(path, &w, &h, &channels, 4);  // Force RGBA
    if (!data) {
        throw std::runtime_error(std::string("Failed to load image: ") + path +
                                 " (" + stbi_failure_reason() + ")");
    }

    // Composite alpha onto background color
    std::vector<uint8_t> rgb(w * h * 3);
    for (int i = 0; i < w * h; i++) {
        float a = data[i * 4 + 3] / 255.0f;
        rgb[i * 3 + 0] = static_cast<uint8_t>(data[i * 4 + 0] * a + bg_color.r * (1 - a));
        rgb[i * 3 + 1] = static_cast<uint8_t>(data[i * 4 + 1] * a + bg_color.g * (1 - a));
        rgb[i * 3 + 2] = static_cast<uint8_t>(data[i * 4 + 2] * a + bg_color.b * (1 - a));
    }
    stbi_image_free(data);

    // Calculate resize dimensions
    int new_w, new_h;
    if (resize_mode == "cover") {
        float ratio = std::max((float)target_w / w, (float)target_h / h);
        new_w = (int)(w * ratio);
        new_h = (int)(h * ratio);
    } else {  // fit
        float ratio = std::min((float)target_w / w, (float)target_h / h);
        new_w = (int)(w * ratio);
        new_h = (int)(h * ratio);
    }

    // Simple bilinear resize
    std::vector<uint8_t> resized(new_w * new_h * 3);
    for (int y = 0; y < new_h; y++) {
        for (int x = 0; x < new_w; x++) {
            float src_x = (float)x * w / new_w;
            float src_y = (float)y * h / new_h;
            int sx = std::min((int)src_x, w - 1);
            int sy = std::min((int)src_y, h - 1);
            for (int c = 0; c < 3; c++) {
                resized[(y * new_w + x) * 3 + c] = rgb[(sy * w + sx) * 3 + c];
            }
        }
    }

    // Create output canvas
    std::vector<uint8_t> output(target_w * target_h * 3);
    for (int i = 0; i < target_w * target_h; i++) {
        output[i * 3 + 0] = bg_color.r;
        output[i * 3 + 1] = bg_color.g;
        output[i * 3 + 2] = bg_color.b;
    }

    if (resize_mode == "cover") {
        // Center crop
        int off_x = (new_w - target_w) / 2;
        int off_y = (new_h - target_h) / 2;
        for (int y = 0; y < target_h; y++) {
            for (int x = 0; x < target_w; x++) {
                int src_idx = ((y + off_y) * new_w + (x + off_x)) * 3;
                int dst_idx = (y * target_w + x) * 3;
                output[dst_idx + 0] = resized[src_idx + 0];
                output[dst_idx + 1] = resized[src_idx + 1];
                output[dst_idx + 2] = resized[src_idx + 2];
            }
        }
    } else {
        // Center paste (fit)
        int off_x = (target_w - new_w) / 2;
        int off_y = (target_h - new_h) / 2;
        for (int y = 0; y < new_h; y++) {
            for (int x = 0; x < new_w; x++) {
                int dst_idx = ((y + off_y) * target_w + (x + off_x)) * 3;
                int src_idx = (y * new_w + x) * 3;
                output[dst_idx + 0] = resized[src_idx + 0];
                output[dst_idx + 1] = resized[src_idx + 1];
                output[dst_idx + 2] = resized[src_idx + 2];
            }
        }
    }

    return output;
}

// --- Nearest color ---

static int nearest_color(int r, int g, int b, const std::array<Color, 4>& palette) {
    int best_idx = 0;
    int best_dist = INT32_MAX;
    for (int i = 0; i < (int)palette.size(); i++) {
        int dr = r - palette[i].r;
        int dg = g - palette[i].g;
        int db = b - palette[i].b;
        int dist = dr * dr + dg * dg + db * db;
        if (dist < best_dist) {
            best_dist = dist;
            best_idx = i;
        }
    }
    return best_idx;
}

// --- Atkinson dithering ---

std::vector<std::vector<int>> dither_atkinson(const std::vector<uint8_t>& rgb,
                                               int width, int height,
                                               const std::array<Color, 4>& palette) {
    // Working copy as float for error diffusion
    std::vector<std::vector<float>> err_r(height, std::vector<float>(width, 0));
    std::vector<std::vector<float>> err_g(height, std::vector<float>(width, 0));
    std::vector<std::vector<float>> err_b(height, std::vector<float>(width, 0));

    // Initialize with source pixel values
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            err_r[y][x] = rgb[idx + 0];
            err_g[y][x] = rgb[idx + 1];
            err_b[y][x] = rgb[idx + 2];
        }
    }

    std::vector<std::vector<int>> result(height, std::vector<int>(width, 0));

    // Atkinson distributes 6/8 of the error (1/8 each to 6 neighbors)
    // Neighbors: (x+1,y), (x+2,y), (x-1,y+1), (x,y+1), (x+1,y+1), (x,y+2)
    auto distribute = [&](int x, int y, float er, float eg, float eb) {
        const float coeff = 1.0f / 8.0f;
        const int offsets[][2] = {
            {1, 0}, {2, 0},
            {-1, 1}, {0, 1}, {1, 1},
            {0, 2}
        };
        for (auto& off : offsets) {
            int nx = x + off[0];
            int ny = y + off[1];
            if (nx >= 0 && nx < width && ny >= 0 && ny < height) {
                err_r[ny][nx] += er * coeff;
                err_g[ny][nx] += eg * coeff;
                err_b[ny][nx] += eb * coeff;
            }
        }
    };

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int r = std::clamp((int)std::round(err_r[y][x]), 0, 255);
            int g = std::clamp((int)std::round(err_g[y][x]), 0, 255);
            int b = std::clamp((int)std::round(err_b[y][x]), 0, 255);

            int idx = nearest_color(r, g, b, palette);
            result[y][x] = idx;

            float er = (float)r - palette[idx].r;
            float eg = (float)g - palette[idx].g;
            float eb = (float)b - palette[idx].b;

            distribute(x, y, er, eg, eb);
        }
    }

    return result;
}

std::vector<std::vector<int>> dither_none(const std::vector<uint8_t>& rgb,
                                           int width, int height,
                                           const std::array<Color, 4>& palette) {
    std::vector<std::vector<int>> result(height, std::vector<int>(width, 0));

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int idx = (y * width + x) * 3;
            result[y][x] = nearest_color(rgb[idx], rgb[idx + 1], rgb[idx + 2], palette);
        }
    }

    return result;
}
