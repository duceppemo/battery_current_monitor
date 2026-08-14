#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Owns the ESP32 OTA writer used by the BLE transport. The transport protocol
// is deliberately sequential: every data frame carries the expected offset,
// so a duplicate, gap, or out-of-order write can never be mistaken for a
// complete firmware image.
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
    };

    enum class Error : uint8_t {
        None = 0,
        Start = 1,
        Sequence = 2,
        Write = 3,
        Checksum = 4,
        Finalize = 5,
    };

    // BLE Firmware Transfer v1 commands.
    static constexpr uint8_t START_COMMAND = 0xA0;
    static constexpr uint8_t DATA_COMMAND = 0xA1;
    static constexpr uint8_t FINISH_COMMAND = 0xA2;
    static constexpr uint8_t ABORT_COMMAND = 0xA3;

    void handleFrame(const uint8_t* data, size_t length);
    bool consumeRestartRequested();

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
    void start(uint32_t imageSize, uint32_t expectedCrc32);
    void append(uint32_t offset, const uint8_t* data, size_t length);
    void finish();
    void abort();
    void fail(Error error);
    bool claim(Owner owner);
    void release(Owner owner);
    static uint32_t updateCrc32(uint32_t crc, const uint8_t* data, size_t length);

    std::atomic<State> state_{State::Idle};
    std::atomic<Error> error_{Error::None};
    std::atomic_uint32_t receivedBytes_{0};
    std::atomic_uint32_t expectedBytes_{0};
    std::atomic_bool restartRequested_{false};
    std::atomic<Owner> owner_{Owner::None};
    uint32_t expectedCrc32_ = 0;
    uint32_t runningCrc32_ = 0xFFFFFFFFUL;
};
