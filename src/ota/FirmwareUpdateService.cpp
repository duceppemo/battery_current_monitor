#include "ota/FirmwareUpdateService.h"

#include <Update.h>
#include <cstring>

#include <mbedtls/ecdsa.h>
#include <mbedtls/ecp.h>

#include "ota/FirmwareSigningKey.h"

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

FirmwareUpdateService::FirmwareUpdateService()
{
    mbedtls_sha256_init(&sha256Ctx_);
}

FirmwareUpdateService::~FirmwareUpdateService()
{
    mbedtls_sha256_free(&sha256Ctx_);
}

void FirmwareUpdateService::handleFrame(const uint8_t* data, size_t length)
{
    if (data == nullptr || length == 0) {
        return;
    }

    switch (data[0]) {
    case START_COMMAND:
        // command(1) + imageSize u32(4) + expectedCrc32 u32(4) + signature(64)
        if (length == 1 + 4 + 4 + SIGNATURE_SIZE) {
            start(readUint32LE(data + 1), readUint32LE(data + 5), data + 9);
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

void FirmwareUpdateService::start(
    uint32_t imageSize,
    uint32_t expectedCrc32,
    const uint8_t signature[SIGNATURE_SIZE])
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
    memcpy(signature_, signature, SIGNATURE_SIZE);
    mbedtls_sha256_free(&sha256Ctx_);
    mbedtls_sha256_init(&sha256Ctx_);
    mbedtls_sha256_starts_ret(&sha256Ctx_, 0);
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
    mbedtls_sha256_update_ret(&sha256Ctx_, data, length);
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

    // The remaining work -- ECDSA verification and Update.end() -- is heavy
    // enough to overflow the BLE controller task's stack if run here (this
    // method executes inside the GATT write callback). Defer it to the main
    // loop via processPendingVerification().
    state_.store(State::Verifying);
}

void FirmwareUpdateService::processPendingVerification()
{
    if (state_.load() != State::Verifying) {
        return;
    }

    // Verified before Update.end(): that call is what marks the newly
    // written partition bootable, so a bad signature must never reach it.
    // Update.abort() (not .end()) discards the write, leaving the currently
    // running firmware as the boot target.
    if (!verifySignature()) {
        Update.abort();
        fail(Error::Signature);
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

bool FirmwareUpdateService::verifySignature()
{
    uint8_t digest[32];
    mbedtls_sha256_finish_ret(&sha256Ctx_, digest);

    mbedtls_ecp_group group;
    mbedtls_ecp_point publicKey;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&publicKey);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);

    bool verified = false;
    if (mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1) == 0 &&
        mbedtls_ecp_point_read_binary(
            &group, &publicKey,
            Config::FIRMWARE_SIGNING_PUBLIC_KEY,
            sizeof(Config::FIRMWARE_SIGNING_PUBLIC_KEY)) == 0 &&
        mbedtls_mpi_read_binary(&r, signature_, SIGNATURE_SIZE / 2) == 0 &&
        mbedtls_mpi_read_binary(&s, signature_ + SIGNATURE_SIZE / 2, SIGNATURE_SIZE / 2) == 0) {
        verified = mbedtls_ecdsa_verify(&group, digest, sizeof(digest), &publicKey, &r, &s) == 0;
    }

    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&publicKey);
    mbedtls_ecp_group_free(&group);
    return verified;
}

void FirmwareUpdateService::abort()
{
    if (owner_.load() != Owner::Ble) {
        return;
    }
    // Verifying still holds an open Update session (finish() only moves the
    // state; the writer isn't finalized/discarded until
    // processPendingVerification() runs), so it must be discarded here too.
    if (state_.load() == State::Receiving || state_.load() == State::Verifying) {
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
