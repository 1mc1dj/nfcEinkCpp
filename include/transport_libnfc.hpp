#pragma once

#include "nfc_transport.hpp"

/// libnfc-based NFC transport — works with PN53x and other libnfc-supported readers
class LibnfcTransport : public NfcTransport {
public:
    LibnfcTransport();
    ~LibnfcTransport() override;

    void open() override;
    void close() override;
    std::vector<uint8_t> send_apdu(const Apdu& apdu) override;

private:
    void* nfc_context_ = nullptr;   // nfc_context*
    void* nfc_device_ = nullptr;    // nfc_device*
};
