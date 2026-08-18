#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include <mbedtls/sha256.h>

// Owns the ESP32 OTA writer used by the BLE transport. The transport protocol
// is deliberately sequential: every data frame carries the expected offset,
// so a duplicate, gap, or out-of-order write can never be mistaken for a
// complete firmware image. BLE-transferred images must also carry a valid
// ECDSA-P256 signature over their SHA-256 digest, verified against the
// public key embedded in FirmwareSigningKey.h before the image is ever
// marked bootable -- CRC-32 alone only catches accidental corruption, not a
// deliberately substituted image. The Web Dashboard upload path is
// unaffected: it stays CRC/format-checked only, matching its role as a
// secondary/recovery path that already requires reaching the recovery AP or
// home network.
class FirmwareUpdateService
{
public:
    enum class Owner : uint8_t {
        None = 0,
        Ble = 1,
        Web = 2,
    };

    enum class State : uint8_t {
        Idle = 0,
        Receiving = 1,
        Verified = 2,
        Error = 3,
        // Signature/ESP32-image verification is deferred out of the BLE GATT
        // callback (see processPendingVerification()); this is the state
        // between a size/CRC-clean finish() and that work completing.
        Verifying = 4,
    };

    enum class Error : uint8_t {
        None = 0,
        Start = 1,
        Sequence = 2,
        Write = 3,
        Checksum = 4,
        Finalize = 5,
        Signature = 6,
    };

    // BLE Firmware Transfer v1 commands.
    static constexpr uint8_t START_COMMAND = 0xA0;
    static constexpr uint8_t DATA_COMMAND = 0xA1;
    static constexpr uint8_t FINISH_COMMAND = 0xA2;
    static constexpr uint8_t ABORT_COMMAND = 0xA3;
    static constexpr size_t SIGNATURE_SIZE = 64;

    FirmwareUpdateService();
    ~FirmwareUpdateService();

    void handleFrame(const uint8_t* data, size_t length);
    bool consumeRestartRequested();

    // Called every main-loop iteration. mbedTLS's ECDSA math is too deep for
    // the Bluetooth controller task's stack -- running it synchronously in
    // finish() (invoked from the BLE write callback) crashed the device with
    // a BTC_TASK stack overflow. The main task's stack has ample headroom,
    // so the heavy verify-and-finalize work happens here instead once
    // finish() has moved state() to Verifying. A no-op otherwise.
    void processPendingVerification();

    // The Web Dashboard uses the ESP32 Update API directly because an HTTP
    // upload has no fixed image length up front. These methods still make its
    // ownership exclusive with the BLE transport.
    bool beginWebUpdate();
    void abandonWebUpdate();

    State state() const { return state_.load(); }
    Error error() const { return error_.load(); }
    uint32_t receivedBytes() const { return receivedBytes_.load(); }
    uint32_t expectedBytes() const { return expectedBytes_.load(); }

private:
    void start(uint32_t imageSize, uint32_t expectedCrc32, const uint8_t signature[SIGNATURE_SIZE]);
    void append(uint32_t offset, const uint8_t* data, size_t length);
    void finish();
    void abort();
    void fail(Error error);
    bool claim(Owner owner);
    void release(Owner owner);
    bool verifySignature();
    static uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t length);

    std::atomic<State> state_{State::Idle};
    std::atomic<Error> error_{Error::None};
    std::atomic_uint32_t receivedBytes_{0};
    std::atomic_uint32_t expectedBytes_{0};
    std::atomic_bool restartRequested_{false};
    std::atomic<Owner> owner_{Owner::None};
    uint32_t expectedCrc32_ = 0;
    uint32_t runningCrc32_ = 0xFFFFFFFFUL;
    uint8_t signature_[SIGNATURE_SIZE] = {};
    mbedtls_sha256_context sha256Ctx_;
};
