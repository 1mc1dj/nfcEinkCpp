#pragma once

#include <cstdint>
#include <string>
#include <vector>

/// Device information parsed from 00D1 response
struct DeviceInfo {
    int width = 0;
    int height = 0;
    int bits_per_pixel = 0;
    int rows_per_block = 0;
    std::string serial_number;
    std::vector<uint8_t> c1;
    std::vector<uint8_t> raw;

    int num_colors() const { return 1 << bits_per_pixel; }
    int pixels_per_byte() const { return 8 / bits_per_pixel; }
    int bytes_per_row() const { return width / pixels_per_byte(); }

    /// Whether the framebuffer is rotated 90° CW relative to the physical display
    bool rotated() const { return width == 296 && height == 128; }

    /// Framebuffer dimensions (after rotation if applicable)
    int fb_width() const { return rotated() ? height : width; }
    int fb_height() const { return rotated() ? width : height; }
    int fb_bytes_per_row() const { return fb_width() / pixels_per_byte(); }
    int fb_total_bytes() const { return fb_bytes_per_row() * fb_height(); }

    std::vector<int> block_sizes() const {
        const int max_bs = 2000;
        int total = fb_total_bytes();
        std::vector<int> sizes;
        while (total > 0) {
            int s = std::min(total, max_bs);
            sizes.push_back(s);
            total -= s;
        }
        return sizes;
    }

    int num_blocks() const { return (int)block_sizes().size(); }
};

/// APDU command tuple
struct Apdu {
    uint8_t cla;
    uint8_t ins;
    uint8_t p1;
    uint8_t p2;
    std::vector<uint8_t> data;
    bool has_data = true;
    int le = -1; // -1 = no Le, >= 0 = expected response length
};

/// Build authentication APDU
Apdu build_auth_apdu();

/// Build device info query APDU (00D1)
Apdu build_device_info_apdu();

Apdu build_image_data_apdu(int block_no, int frag_no, const std::vector<uint8_t>& data, bool is_final, int page = 0);

/// Build screen refresh APDU (F0D4)
Apdu build_refresh_apdu();

/// Build refresh polling APDU (F0DE)
Apdu build_poll_apdu();

/// Check if poll response indicates refresh complete
bool is_refresh_complete(const std::vector<uint8_t>& response);

/// Parse device info from 00D1 response
DeviceInfo parse_device_info(const std::vector<uint8_t>& data);
