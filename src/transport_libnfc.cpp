#include "transport_libnfc.hpp"

#include <nfc/nfc.h>
#include <nfc/nfc-types.h>

#include <cstring>
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>

LibnfcTransport::LibnfcTransport() {}

LibnfcTransport::~LibnfcTransport() {
    close();
}

void LibnfcTransport::open() {
    nfc_context* context = nullptr;
    nfc_init(&context);
    if (!context) {
        throw std::runtime_error("Failed to initialize libnfc");
    }
    nfc_context_ = context;

    nfc_device* device = nfc_open(context, nullptr);
    if (!device) {
        throw std::runtime_error(
            "Failed to open NFC device. libnfc-supported reader required "
            "(e.g. PN532, ACR122U). For RC-S380, use the libusb backend.");
    }
    nfc_device_ = device;

    if (nfc_initiator_init(device) < 0) {
        throw std::runtime_error("Failed to initialize NFC initiator mode");
    }

    // Poll for ISO14443-4A target
    nfc_modulation nm;
    nm.nmt = NMT_ISO14443A;
    nm.nbr = NBR_106;

    nfc_target target;
    std::cout << "Waiting for NFC card..." << std::endl;

    int res = nfc_initiator_select_passive_target(device, nm, nullptr, 0, &target);
    if (res <= 0) {
        throw std::runtime_error("No NFC card detected");
    }
}

void LibnfcTransport::close() {
    if (nfc_device_) {
        nfc_close(static_cast<nfc_device*>(nfc_device_));
        nfc_device_ = nullptr;
    }
    if (nfc_context_) {
        nfc_exit(static_cast<nfc_context*>(nfc_context_));
        nfc_context_ = nullptr;
    }
}

std::vector<uint8_t> LibnfcTransport::send_apdu(const Apdu& apdu) {
    if (!nfc_device_) {
        throw std::runtime_error("Not connected to a card");
    }

    nfc_device* device = static_cast<nfc_device*>(nfc_device_);

    // Build APDU: CLA INS P1 P2 [Lc Data] [Le]
    std::vector<uint8_t> tx;
    tx.push_back(apdu.cla);
    tx.push_back(apdu.ins);
    tx.push_back(apdu.p1);
    tx.push_back(apdu.p2);

    if (apdu.has_data && !apdu.data.empty()) {
        tx.push_back(static_cast<uint8_t>(apdu.data.size()));
        tx.insert(tx.end(), apdu.data.begin(), apdu.data.end());
    }

    if (apdu.le >= 0) {
        tx.push_back(static_cast<uint8_t>(apdu.le == 256 ? 0x00 : apdu.le));
    }

    uint8_t rx[512];
    int rx_len = nfc_initiator_transceive_bytes(device, tx.data(), tx.size(),
                                                 rx, sizeof(rx), 5000);

    if (rx_len < 0) {
        throw std::runtime_error("APDU communication failed");
    }

    if (rx_len < 2) {
        if (apdu.ins == 0xDE || apdu.ins == 0xD4) return {};
        throw std::runtime_error("APDU response too short");
    }

    uint8_t sw1 = rx[rx_len - 2];
    uint8_t sw2 = rx[rx_len - 1];

    if (sw1 != 0x90 || sw2 != 0x00) {
        if (apdu.ins == 0xDE || apdu.ins == 0xD4) {
            return std::vector<uint8_t>(rx, rx + rx_len - 2);
        }
        std::ostringstream oss;
        oss << "APDU error: SW=" << std::hex << std::setw(2) << std::setfill('0')
            << (int)sw1 << std::setw(2) << (int)sw2;
        throw std::runtime_error(oss.str());
    }

    return std::vector<uint8_t>(rx, rx + rx_len - 2);
}

// Factory function for libnfc backend
#ifdef NFC_BACKEND_LIBNFC
std::unique_ptr<NfcTransport> create_nfc_transport() {
    return std::make_unique<LibnfcTransport>();
}
#endif
