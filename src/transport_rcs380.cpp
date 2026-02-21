#include "transport_rcs380.hpp"

#include <libusb-1.0/libusb.h>

#include <cstring>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>

// RC-S380 USB identifiers
static const uint16_t RC_S380_VENDOR_ID  = 0x054C; // Sony
static const uint16_t RC_S380_PRODUCT_ID = 0x06C1; // RC-S380

// NFC Port-100 constants
static const uint8_t ACK_FRAME[] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

// Default protocol settings (from nfcpy)
static const std::vector<uint8_t> IN_SET_PROTOCOL_DEFAULTS = {
    0x00, 0x18, 0x01, 0x01, 0x02, 0x01, 0x03, 0x00,
    0x04, 0x00, 0x05, 0x00, 0x06, 0x00, 0x07, 0x08,
    0x08, 0x00, 0x09, 0x00, 0x0A, 0x00, 0x0B, 0x00,
    0x0C, 0x00, 0x0E, 0x04, 0x0F, 0x00, 0x10, 0x00,
    0x11, 0x00, 0x12, 0x00, 0x13, 0x06
};

Rcs380Transport::Rcs380Transport() {}

Rcs380Transport::~Rcs380Transport() {
    close();
}

// ==================== USB Transport ====================

void Rcs380Transport::usb_open() {
    libusb_context* ctx = nullptr;
    if (libusb_init(&ctx) < 0) {
        throw std::runtime_error("Failed to initialize libusb");
    }
    usb_ctx_ = ctx;

    libusb_device_handle* handle = libusb_open_device_with_vid_pid(
        ctx, RC_S380_VENDOR_ID, RC_S380_PRODUCT_ID);
    if (!handle) {
        throw std::runtime_error("RC-S380 not found (is it connected?)");
    }
    usb_handle_ = handle;

    if (libusb_kernel_driver_active(handle, 0) == 1) {
        libusb_detach_kernel_driver(handle, 0);
    }

    if (libusb_claim_interface(handle, 0) < 0) {
        throw std::runtime_error("Failed to claim USB interface");
    }

    libusb_device* dev = libusb_get_device(handle);
    libusb_config_descriptor* config;
    libusb_get_active_config_descriptor(dev, &config);

    for (int i = 0; i < config->interface[0].altsetting[0].bNumEndpoints; i++) {
        auto& ep = config->interface[0].altsetting[0].endpoint[i];
        if ((ep.bEndpointAddress & LIBUSB_ENDPOINT_IN) != 0) {
            ep_in_ = ep.bEndpointAddress;
        } else {
            ep_out_ = ep.bEndpointAddress;
        }
    }

    libusb_free_config_descriptor(config);

    if (!ep_in_ || !ep_out_) {
        throw std::runtime_error("Could not find USB endpoints");
    }
}

void Rcs380Transport::usb_write(const std::vector<uint8_t>& data) {
    auto handle = static_cast<libusb_device_handle*>(usb_handle_);
    int transferred;
    int ret = libusb_bulk_transfer(handle, ep_out_,
        const_cast<uint8_t*>(data.data()), (int)data.size(),
        &transferred, 5000);
    if (ret < 0) {
        throw std::runtime_error(std::string("USB write failed: ") +
                                 libusb_error_name(ret));
    }
}

std::vector<uint8_t> Rcs380Transport::usb_read(int timeout_ms) {
    auto handle = static_cast<libusb_device_handle*>(usb_handle_);
    uint8_t buf[512];
    int transferred;
    int ret = libusb_bulk_transfer(handle, ep_in_, buf, sizeof(buf),
                                   &transferred, timeout_ms);
    if (ret == LIBUSB_ERROR_TIMEOUT) {
        throw std::runtime_error("USB read timeout");
    }
    if (ret < 0) {
        throw std::runtime_error(std::string("USB read failed: ") +
                                 libusb_error_name(ret));
    }
    return std::vector<uint8_t>(buf, buf + transferred);
}

void Rcs380Transport::close() {
    if (usb_handle_) {
        auto handle = static_cast<libusb_device_handle*>(usb_handle_);
        libusb_release_interface(handle, 0);
        libusb_close(handle);
        usb_handle_ = nullptr;
    }
    if (usb_ctx_) {
        libusb_exit(static_cast<libusb_context*>(usb_ctx_));
        usb_ctx_ = nullptr;
    }
}

// ==================== NFC Port-100 Framing ====================

std::vector<uint8_t> Rcs380Transport::build_frame(const std::vector<uint8_t>& data) {
    std::vector<uint8_t> frame = {0x00, 0x00, 0xFF, 0xFF, 0xFF};
    uint16_t len = (uint16_t)data.size();
    frame.push_back(len & 0xFF);
    frame.push_back((len >> 8) & 0xFF);
    uint8_t len_sum = (uint8_t)((256 - ((frame[5] + frame[6]) & 0xFF)) & 0xFF);
    frame.push_back(len_sum);
    frame.insert(frame.end(), data.begin(), data.end());
    uint8_t data_sum = 0;
    for (size_t i = 8; i < frame.size(); i++) data_sum += frame[i];
    frame.push_back((uint8_t)((256 - data_sum) & 0xFF));
    frame.push_back(0x00);
    return frame;
}

std::vector<uint8_t> Rcs380Transport::parse_frame(const std::vector<uint8_t>& frame) {
    if (frame.size() >= 6 &&
        frame[0] == 0x00 && frame[1] == 0x00 && frame[2] == 0xFF) {
        if (frame[3] == 0x00 && frame[4] == 0xFF && frame[5] == 0x00) {
            return {};
        }
        if (frame[3] == 0xFF && frame[4] == 0xFF && frame.size() >= 8) {
            uint16_t len = frame[5] | (frame[6] << 8);
            if (frame.size() >= (size_t)(8 + len)) {
                return std::vector<uint8_t>(frame.begin() + 8, frame.begin() + 8 + len);
            }
        }
    }
    return {};
}

std::vector<uint8_t> Rcs380Transport::send_command(uint8_t cmd_code,
                                                    const std::vector<uint8_t>& cmd_data) {
    std::vector<uint8_t> cmd = {0xD6, cmd_code};
    cmd.insert(cmd.end(), cmd_data.begin(), cmd_data.end());
    usb_write(build_frame(cmd));

    std::vector<uint8_t> buffer;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (std::chrono::steady_clock::now() < deadline) {
        try {
            auto raw = usb_read(500);
            buffer.insert(buffer.end(), raw.begin(), raw.end());
        } catch (const std::exception& e) {
            // Ignore timeouts, just keep waiting until deadline
            if (std::string(e.what()).find("timeout") == std::string::npos) throw;
        }

        // Process buffer for frames
        while (buffer.size() >= 6) {
            auto it = std::search(buffer.begin(), buffer.end(), ACK_FRAME, ACK_FRAME + 3);
            if (it == buffer.end()) {
                if (buffer.size() > 1024) buffer.clear(); // Safety
                break;
            }
            if (it != buffer.begin()) {
                buffer.erase(buffer.begin(), it);
            }

            if (buffer.size() < 6) break;

            if (buffer[3] == 0x00 && buffer[4] == 0xFF && buffer[5] == 0x00) {
                buffer.erase(buffer.begin(), buffer.begin() + 6);
                continue;
            }

            if (buffer.size() >= 10 && buffer[3] == 0xFF && buffer[4] == 0xFF) {
                uint16_t len = buffer[5] | (buffer[6] << 8);
                if (buffer.size() >= (size_t)(10 + len)) {
                    auto data_frame = std::vector<uint8_t>(buffer.begin(), buffer.begin() + 10 + len);
                    buffer.erase(buffer.begin(), buffer.begin() + 10 + len);
                    
                    auto rsp = parse_frame(data_frame);
                    if (rsp.size() >= 2 && rsp[0] == 0xD7 && rsp[1] == cmd_code + 1) {
                        return std::vector<uint8_t>(rsp.begin() + 2, rsp.end());
                    }
                } else {
                    break;
                }
            } else if (buffer.size() >= 6) {
                buffer.erase(buffer.begin()); // Skip invalid byte
            }
        }
    }

    throw std::runtime_error("Timeout waiting for RC-S380 command response");
}

void Rcs380Transport::set_command_type(uint8_t type) {
    auto data = send_command(0x2A, {type});
    if (!data.empty() && data[0] != 0) throw std::runtime_error("set_command_type failed");
}

void Rcs380Transport::get_firmware_version() {
    auto data = send_command(0x20, {});
    if (data.size() >= 2) {
        std::cout << "RC-S380 firmware: v" << (int)data[1] << "."
                  << std::setw(2) << std::setfill('0') << (int)data[0] << std::endl;
    }
}

void Rcs380Transport::switch_rf(bool on) {
    auto data = send_command(0x06, {(uint8_t)(on ? 1 : 0)});
    if (!data.empty() && data[0] != 0) throw std::runtime_error("switch_rf failed");
}

void Rcs380Transport::in_set_rf(const std::vector<uint8_t>& settings) {
    auto data = send_command(0x00, settings);
    if (!data.empty() && data[0] != 0) throw std::runtime_error("in_set_rf failed");
}

void Rcs380Transport::in_set_protocol(const std::vector<uint8_t>& data) {
    if (data.empty()) return;
    auto result = send_command(0x02, data);
    if (!result.empty() && result[0] != 0) throw std::runtime_error("in_set_protocol failed");
}

std::vector<uint8_t> Rcs380Transport::in_comm_rf(const std::vector<uint8_t>& data, int timeout_ms) {
    uint16_t timeout = std::min((timeout_ms + 1) * 10, 0xFFFF);
    std::vector<uint8_t> cmd_data;
    cmd_data.push_back(timeout & 0xFF);
    cmd_data.push_back((timeout >> 8) & 0xFF);
    cmd_data.insert(cmd_data.end(), data.begin(), data.end());
    auto result = send_command(0x04, cmd_data);
    if (result.size() >= 4 && (result[0] != 0 || result[1] != 0 ||
                                result[2] != 0 || result[3] != 0)) {
        std::ostringstream oss;
        oss << "in_comm_rf communication error: " 
            << std::hex << std::setw(2) << std::setfill('0') << (int)result[0] << " "
            << std::setw(2) << std::setfill('0') << (int)result[1] << " "
            << std::setw(2) << std::setfill('0') << (int)result[2] << " "
            << std::setw(2) << std::setfill('0') << (int)result[3];
        throw std::runtime_error(oss.str());
    }
    if (result.size() > 5) {
        return std::vector<uint8_t>(result.begin() + 5, result.end());
    }
    return {};
}

// ==================== ISO14443A Target Activation ====================

bool Rcs380Transport::sense_and_activate_target() {
    in_set_rf({0x02, 0x03, 0x0F, 0x03});
    in_set_protocol(IN_SET_PROTOCOL_DEFAULTS);
    in_set_protocol({
        0x00, 0x06, 0x01, 0x00, 0x02, 0x00, 0x05, 0x01, 0x07, 0x07,
    });

    std::vector<uint8_t> sens_res;
    try { sens_res = in_comm_rf({0x26}, 30); } catch (...) { return false; }
    if (sens_res.size() != 2) return false;

    in_set_protocol({0x07, 0x08, 0x04, 0x01});

    uint8_t sak = 0;
    for (uint8_t sel_cmd : {0x93, 0x95, 0x97}) {
        in_set_protocol({0x01, 0x00, 0x02, 0x00});
        std::vector<uint8_t> sdd_res;
        try { sdd_res = in_comm_rf({sel_cmd, 0x20}, 30); } catch (...) { return false; }
        if (sdd_res.size() < 5) return false;

        in_set_protocol({0x01, 0x01, 0x02, 0x01});
        std::vector<uint8_t> sel_req = {sel_cmd, 0x70};
        sel_req.insert(sel_req.end(), sdd_res.begin(), sdd_res.end());
        std::vector<uint8_t> sel_res;
        try { sel_res = in_comm_rf(sel_req, 30); } catch (...) { return false; }
        if (sel_res.empty()) return false;
        sak = sel_res[0];
        if (!(sak & 0x04)) break;
    }

    if (!(sak & 0x20)) {
        throw std::runtime_error("Card does not support ISO14443-4");
    }

    // Send RATS (Request for Answer To Select)
    // PARAM byte: FSD=256 (0x80), CID=0
    auto ats = in_comm_rf({0xE0, 0x80}, 30);
    if (ats.empty()) {
        throw std::runtime_error("RATS failed");
    }

    std::cout << "RATS Response: ";
    for (uint8_t b : ats) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    std::cout << std::dec << std::endl;

    if (ats.size() >= 2) {
        uint8_t fsci = ats[1] & 0x0F;
        int fsc[] = {16, 24, 32, 40, 48, 64, 96, 128, 256};
        if (fsci <= 8) {
            std::cout << "Card FSC: " << fsc[fsci] << " bytes (FSCI=" << (int)fsci << ")" << std::endl;
        }
    }

    return true;
}

std::vector<uint8_t> Rcs380Transport::send_apdu(const Apdu& apdu) {
    // Build raw APDU bytes
    std::vector<uint8_t> apdu_bytes;
    apdu_bytes.push_back(apdu.cla);
    apdu_bytes.push_back(apdu.ins);
    apdu_bytes.push_back(apdu.p1);
    apdu_bytes.push_back(apdu.p2);

    if (apdu.has_data && !apdu.data.empty()) {
        apdu_bytes.push_back((uint8_t)apdu.data.size());
        apdu_bytes.insert(apdu_bytes.end(), apdu.data.begin(), apdu.data.end());
    }
    if (apdu.le >= 0) {
        apdu_bytes.push_back((uint8_t)(apdu.le == 256 ? 0x00 : apdu.le));
    }

    return _send_apdu_impl(apdu_bytes);
}

std::vector<uint8_t> Rcs380Transport::_send_apdu_impl(const std::vector<uint8_t>& apdu_bytes) {
    const int MIU = 253;
    std::vector<uint8_t> response;

    for (size_t offset = 0; offset < apdu_bytes.size(); offset += MIU) {
        bool more = (apdu_bytes.size() - offset) > (size_t)MIU;
        size_t chunk_end = std::min(offset + (size_t)MIU, apdu_bytes.size());

        uint8_t pcb = (more ? 0x12 : 0x02) | (block_nr_ & 0x01);
        std::vector<uint8_t> iblock;
        iblock.push_back(pcb);
        iblock.insert(iblock.end(), apdu_bytes.begin() + offset, apdu_bytes.begin() + chunk_end);

        response = in_comm_rf(iblock, 5000);

        // Handle WTX S-blocks
        while (!response.empty() && (response[0] & 0xFE) == 0xF2) {
            response = in_comm_rf({0xF2, response[1]}, (response[1] & 0x3F) * 1000);
        }

        if (more) {
            // Expect R(ACK): 0xA2|pni or 0xA3|pni
            if (response.empty() || (response[0] & 0xF6) != 0xA2) {
                throw std::runtime_error("Expected ACK R-block during ISO-DEP chaining");
            }
            block_nr_ ^= 1;
        }
    }

    // Toggle block number for the final I-block
    block_nr_ ^= 1;

    if (response.empty()) {
        throw std::runtime_error("Empty APDU response");
    }

    // Reassemble chained response (if card sends chained I-blocks)
    std::vector<uint8_t> full_response;
    full_response.insert(full_response.end(), response.begin() + 1, response.end());

    while (!response.empty() && (response[0] & 0x10)) {
        // Card is chaining; send R(ACK)
        std::vector<uint8_t> ack = {(uint8_t)(0xA2 | (block_nr_ & 0x01))};
        response = in_comm_rf(ack, 5000);
        if (!response.empty()) {
            full_response.insert(full_response.end(), response.begin() + 1, response.end());
            block_nr_ ^= 1;
        }
    }

    // Parse SW1/SW2
    if (full_response.size() < 2) {
        if (apdu_bytes.size() >= 2 && (apdu_bytes[1] == 0xDE || apdu_bytes[1] == 0xD4)) return {};
        throw std::runtime_error("APDU response too short");
    }

    uint8_t sw1 = full_response[full_response.size() - 2];
    uint8_t sw2 = full_response[full_response.size() - 1];

    if (sw1 != 0x90 || sw2 != 0x00) {
        if (apdu_bytes.size() >= 2 && (apdu_bytes[1] == 0xDE || apdu_bytes[1] == 0xD4)) {
            return std::vector<uint8_t>(full_response.begin(), full_response.end() - 2);
        }
        std::ostringstream oss;
        oss << "APDU error: SW=" << std::hex << std::setw(2) << std::setfill('0')
            << (int)sw1 << std::setw(2) << (int)sw2;
        throw std::runtime_error(oss.str());
    }

    return std::vector<uint8_t>(full_response.begin(), full_response.end() - 2);
}

// ==================== Public Interface ====================

void Rcs380Transport::open() {
    usb_open();

    std::vector<uint8_t> ack(ACK_FRAME, ACK_FRAME + sizeof(ACK_FRAME));
    usb_write(ack);
    try { while (true) { usb_read(100); } } catch (...) {}

    set_command_type(1);
    get_firmware_version();
    switch_rf(false);

    std::cout << "Waiting for NFC card..." << std::endl;

    bool found = false;
    for (int i = 0; i < 100; i++) {
        switch_rf(true);
        try {
            if (sense_and_activate_target()) { found = true; break; }
        } catch (...) {}
        switch_rf(false);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    if (!found) {
        throw std::runtime_error("No NFC card detected");
    }
}

// Factory function for RC-S380 backend
#ifdef NFC_BACKEND_RCS380
std::unique_ptr<NfcTransport> create_nfc_transport() {
    return std::make_unique<Rcs380Transport>();
}
#endif
