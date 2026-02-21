#pragma once

#include "protocol.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

/// Abstract NFC transport interface for e-ink card communication
class NfcTransport {
public:
    virtual ~NfcTransport() = default;

    /// Open NFC device and wait for a card (blocking)
    virtual void open() = 0;

    /// Close NFC connection
    virtual void close() = 0;

    /// Send APDU and receive response (without status word)
    /// Throws on communication error or non-9000 status (unless allow_error)
    virtual std::vector<uint8_t> send_apdu(const Apdu& apdu) = 0;
};

/// Create the default NFC transport (selected at build time)
std::unique_ptr<NfcTransport> create_nfc_transport();
