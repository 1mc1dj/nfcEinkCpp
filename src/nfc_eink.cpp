#include "nfc_eink.hpp"
#include "image.hpp"

#include <iostream>
#include <thread>
#include <chrono>

NfcEinkCard::NfcEinkCard()
    : transport_(create_nfc_transport()) {}

NfcEinkCard::~NfcEinkCard() {
    close();
}

void NfcEinkCard::connect() {
    transport_->open();

    // Authenticate
    auto auth_apdu = build_auth_apdu();
    transport_->send_apdu(auth_apdu);

    // Read device info
    auto info_apdu = build_device_info_apdu();
    auto response = transport_->send_apdu(info_apdu);
    device_info_ = parse_device_info(response);

    std::cout << "Card: " << device_info_.serial_number
              << " (" << device_info_.width << "x" << device_info_.height
              << ", " << device_info_.num_colors() << " colors)" << std::endl;
}

void NfcEinkCard::close() {
    if (transport_) {
        transport_->close();
    }
}

void NfcEinkCard::send_image(const std::vector<std::vector<int>>& pixels) {
    auto all_apdus = encode_image(pixels, device_info_);
    std::cout << "Sending image (" << all_apdus.size() << " blocks)..." << std::endl;
    
    int block_idx = 0;
    for (const auto& block_apdus : all_apdus) {
        block_idx++;
        std::cout << "\rBlock " << block_idx << "/" << all_apdus.size() << " (" << block_apdus.size() << " fragments) " << std::flush;
        for (const auto& apdu : block_apdus) {
            transport_->send_apdu(apdu);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    std::cout << std::endl;
}

void NfcEinkCard::refresh(float timeout, float poll_interval) {
    auto refresh_cmd = build_refresh_apdu();
    transport_->send_apdu(refresh_cmd);

    auto poll_cmd = build_poll_apdu();
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::milliseconds(static_cast<int>(timeout * 1000));

    while (std::chrono::steady_clock::now() < deadline) {
        try {
            auto response = transport_->send_apdu(poll_cmd);
            if (is_refresh_complete(response)) {
                return;
            }
        } catch (...) {}
        std::this_thread::sleep_for(
            std::chrono::milliseconds(static_cast<int>(poll_interval * 1000))
        );
    }

    throw std::runtime_error("Screen refresh timed out");
}
