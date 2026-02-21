#pragma once

#include "nfc_transport.hpp"
#include "protocol.hpp"
#include <memory>
#include <vector>

/// High-level NFC e-ink card manager — transport-agnostic
class NfcEinkCard {
public:
    NfcEinkCard();
    ~NfcEinkCard();

    /// Connect, authenticate, and read device info
    void connect();

    /// Close connection
    void close();

    /// Get device information
    const DeviceInfo& device_info() const { return device_info_; }

    /// Send a 2D color-index image to the card
    void send_image(const std::vector<std::vector<int>>& pixels);

    /// Start refresh and poll until complete
    void refresh(float timeout = 30.0f, float poll_interval = 0.5f);

private:
    std::unique_ptr<NfcTransport> transport_;
    DeviceInfo device_info_;
};
