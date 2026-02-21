#pragma once

#include "nfc_transport.hpp"
#include <cstdint>
#include <vector>

/// RC-S380 (NFC Port-100) transport via libusb — direct USB communication
class Rcs380Transport : public NfcTransport {
public:
    Rcs380Transport();
    ~Rcs380Transport() override;

    void open() override;
    void close() override;
    std::vector<uint8_t> send_apdu(const Apdu& apdu) override;

private:
    // USB transport
    void usb_open();
    void usb_write(const std::vector<uint8_t>& data);
    std::vector<uint8_t> usb_read(int timeout_ms = 5000);

    // NFC Port-100 framing
    std::vector<uint8_t> build_frame(const std::vector<uint8_t>& data);
    std::vector<uint8_t> parse_frame(const std::vector<uint8_t>& frame);

    // NFC Port-100 commands
    std::vector<uint8_t> send_command(uint8_t cmd_code, const std::vector<uint8_t>& cmd_data);
    void set_command_type(uint8_t type);
    void get_firmware_version();
    void switch_rf(bool on);
    void in_set_rf(const std::vector<uint8_t>& settings);
    void in_set_protocol(const std::vector<uint8_t>& data);
    std::vector<uint8_t> in_comm_rf(const std::vector<uint8_t>& data, int timeout_ms);

    // ISO14443 target activation
    bool sense_and_activate_target();

    // ISO-DEP I-block chaining implementation
    std::vector<uint8_t> _send_apdu_impl(const std::vector<uint8_t>& apdu_bytes);

    void* usb_ctx_ = nullptr;
    void* usb_handle_ = nullptr;
    uint8_t ep_out_ = 0;
    uint8_t ep_in_ = 0;
    uint8_t block_nr_ = 0;
};
