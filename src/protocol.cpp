#include "protocol.hpp"
#include <map>
#include <stdexcept>
#include <sstream>

// Color mode mapping: mode byte -> bits per pixel
static const std::map<uint8_t, int> COLOR_MODE_TO_BPP = {
    {0x01, 1},  // 2-color
    {0x07, 2},  // 4-color
};

Apdu build_auth_apdu() {
    return {0x00, 0x20, 0x00, 0x01, {0x20, 0x09, 0x12, 0x10}, true, -1};
}

Apdu build_device_info_apdu() {
    return {0x00, 0xD1, 0x00, 0x00, {}, false, 256};
}

Apdu build_image_data_apdu(int block_no, int frag_no, const std::vector<uint8_t>& data, bool is_final, int page) {
    std::vector<uint8_t> payload;
    payload.push_back((uint8_t)block_no);
    payload.push_back((uint8_t)frag_no);
    payload.insert(payload.end(), data.begin(), data.end());
    uint8_t p2 = is_final ? 0x01 : 0x00;
    return {0xF0, 0xD3, (uint8_t)page, p2, payload, true, -1};
}

Apdu build_refresh_apdu() {
    return {0xF0, 0xD4, 0x85, 0x80, {}, false, 256};
}

Apdu build_poll_apdu() {
    return {0xF0, 0xDE, 0x00, 0x00, {}, false, 1};
}

bool is_refresh_complete(const std::vector<uint8_t>& response) {
    if (response.empty()) return false;
    return response[0] == 0x00;
}

//--- TLV Parser ---

static std::map<uint8_t, std::vector<uint8_t>> parse_tlv(const std::vector<uint8_t>& data) {
    std::map<uint8_t, std::vector<uint8_t>> result;
    size_t i = 0;
    while (i < data.size()) {
        uint8_t tag = data[i++];
        if (i >= data.size()) break;
        uint8_t len = data[i++];
        if (i + len > data.size()) break;
        std::vector<uint8_t> value(data.begin() + i, data.begin() + i + len);
        result[tag] = value;
        i += len;
    }
    return result;
}

DeviceInfo parse_device_info(const std::vector<uint8_t>& data) {
    auto tlv = parse_tlv(data);

    if (tlv.find(0xA0) == tlv.end() || tlv[0xA0].size() < 7) {
        std::ostringstream oss;
        oss << "Missing or invalid A0 tag in device info";
        throw std::runtime_error(oss.str());
    }

    auto& a0 = tlv[0xA0];
    uint8_t color_mode = a0[1];
    int rows_per_block = a0[2];
    int height_raw = (a0[3] << 8) | a0[4];
    int width = (a0[5] << 8) | a0[6];

    auto it = COLOR_MODE_TO_BPP.find(color_mode);
    if (it == COLOR_MODE_TO_BPP.end()) {
        std::ostringstream oss;
        oss << "Unknown color mode 0x" << std::hex << (int)color_mode;
        throw std::runtime_error(oss.str());
    }

    int bpp = it->second;
    int height = height_raw / bpp;

    DeviceInfo info;
    info.width = width;
    info.height = height;
    info.bits_per_pixel = bpp;
    info.rows_per_block = rows_per_block;
    info.raw = data;

    if (tlv.find(0xC0) != tlv.end()) {
        auto& c0 = tlv[0xC0];
        info.serial_number = std::string(c0.begin(), c0.end());
    }

    if (tlv.find(0xC1) != tlv.end()) {
        info.c1 = tlv[0xC1];
    }

    return info;
}
