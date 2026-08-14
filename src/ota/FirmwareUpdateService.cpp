#include "ota/FirmwareUpdateService.h"

#include <Update.h>

namespace
{
    uint32_t readUint32LE(const uint8_t* source)
    {
        return static_cast<uint32_t>(source[0]) |
               (static_cast<uint32_t>(source[1]) << 8) |
               (static_cast<uint32_t>(source[2]) << 16) |
               (static_cast<uint32_t>(source[3]) << 24);
    }
}

void FirmwareUpdateService::handleFrame(const uint8_t* data, size_t length)
{
    if (data == nullptr || length == 0) {
        return;
    }

    switch (data[0]) {
    case START_COMMAND:
        if (length == 9) {
            start(readUint32LE(data + 1), readUint32LE(data + 5));
        } else {
            fail(Error::Start);
        }
        break;
    case DATA_COMMAND:
        if (length > 5) {
            append(readUint32LE(data + 1), data + 5, length - 5);
        } else {
            fail(Error::Sequence);
        }
        break;
    case FINISH_COMMAND:
        if (length == 1) {
            finish();
        } else {
            fail(Error::Sequence);
        }
        break;
    case ABORT_COMMAND:
        abort();
        break;
    default:
        fail(Error::Sequence);
        break;
    }
}

bool FirmwareUpdateService::consumeRestartRequested()
{
    return restartRequested_.exchange(false);
}

bool FirmwareUpdateService::beginWebUpdate()
{
    return claim(Owner::Web);
}

void FirmwareUpdateService::abandonWebUpdate()
{
    release(Owner::Web);
}

void FirmwareUpdateService::start(uint32_t imageSize, uint32_t expectedCrc32)
{
    if (state_.load() == State::Receiving && owner_.load() == Owner::Ble) {
        Update.abort();
        release(Owner::Ble);
    }

    restartRequested_.store(false);
    receivedBytes_.store(0);
    expectedBytes_.store(imageSize);
    expectedCrc32_ = expectedCrc32;
    runningCrc32_ = 0xFFFFFFFFUL;
    error_.store(Error::None);

    if (!claim(Owner::Ble) || imageSize == 0 || !Update.begin(imageSize, U_FLASH)) {
        release(Owner::Ble);
        state_.store(State::Error);
        error_.store(Error::Start);
        return;
    }

    state_.store(State::Receiving);
}

void FirmwareUpdateService::append(uint32_t offset, const uint8_t* data, size_t length)
{
    const uint32_t received = receivedBytes_.load();
    const uint32_t expected = expectedBytes_.load();
    if (state_.load() != State::Receiving || offset != received ||
        length > (expected - received)) {
        fail(Error::Sequence);
        return;
    }

    // Arduino's Update API predates const-correct buffers; it does not mutate
    // the frame supplied by BLE.
    if (Update.write(const_cast<uint8_t*>(data), length) != length) {
        fail(Error::Write);
        return;
    }

    runningCrc32_ = updateCrc32(runningCrc32_, data, length);
    receivedBytes_.store(received + static_cast<uint32_t>(length));
}

void FirmwareUpdateService::finish()
{
    if (state_.load() != State::Receiving ||
        receivedBytes_.load() != expectedBytes_.load()) {
        fail(Error::Sequence);
        return;
    }

    if ((~runningCrc32_) != expectedCrc32_) {
        fail(Error::Checksum);
        return;
    }

    if (!Update.end()) {
        fail(Error::Finalize);
        return;
    }

    state_.store(State::Verified);
    error_.store(Error::None);
    restartRequested_.store(true);
}

void FirmwareUpdateService::abort()
{
    if (owner_.load() != Owner::Ble) {
        return;
    }
    if (state_.load() == State::Receiving) {
        Update.abort();
    }
    release(Owner::Ble);
    state_.store(State::Idle);
    error_.store(Error::None);
    receivedBytes_.store(0);
    expectedBytes_.store(0);
    restartRequested_.store(false);
}

void FirmwareUpdateService::fail(Error error)
{
    if (owner_.load() == Owner::Ble && state_.load() == State::Receiving) {
        Update.abort();
    }
    release(Owner::Ble);
    state_.store(State::Error);
    error_.store(error);
    restartRequested_.store(false);
}

bool FirmwareUpdateService::claim(Owner owner)
{
    Owner expected = Owner::None;
    return owner_.compare_exchange_strong(expected, owner);
}

void FirmwareUpdateService::release(Owner owner)
{
    Owner expected = owner;
    owner_.compare_exchange_strong(expected, Owner::None);
}

uint32_t FirmwareUpdateService::updateCrc32(uint32_t crc, const uint8_t* data, size_t length)
{
    for (size_t index = 0; index < length; ++index) {
        crc ^= data[index];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc;
}
