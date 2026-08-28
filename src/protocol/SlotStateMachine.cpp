// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
#include "SlotStateMachine.h"

#include <algorithm>
#include <chrono>
#include <random>

#include "Guid128.h"

namespace jochona::protocol {

uint64_t SlotStateMachine::DefaultClock()
{
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

JochonaGuid128 SlotStateMachine::DefaultGuidGenerator()
{
    // Portable fallback (used by tests and any non-Windows embedder). The
    // Windows driver overrides this with a CoCreateGuid-backed generator;
    // see driver/JochonaDisplayAdapter/Driver.cpp.
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<uint16_t> byteDist(0, 255);

    JochonaGuid128 guid{};
    for (auto& byte : guid.Bytes)
    {
        byte = static_cast<uint8_t>(byteDist(engine));
    }
    return guid;
}

SlotStateMachine::SlotStateMachine(
    uint32_t slotCount,
    JochonaSlotMode baselineMode,
    uint32_t watchdogTimeoutMs,
    ClockFn clock,
    GuidGeneratorFn guidGenerator)
    : watchdogTimeoutMs_(watchdogTimeoutMs)
    , clock_(std::move(clock))
    , guidGenerator_(std::move(guidGenerator))
{
    slotCount = std::min(slotCount, static_cast<uint32_t>(JOCHONA_PROTOCOL_V1_MAX_SLOTS));
    slots_.reserve(slotCount);
    for (uint32_t i = 0; i < slotCount; ++i)
    {
        Slot slot;
        slot.slotId = i;
        slot.baselineMode = baselineMode;
        slot.currentMode = baselineMode;
        slots_.push_back(slot);
    }
}

JochonaStatus SlotStateMachine::ValidateVersion(JochonaProtocolVersion requested)
{
    if (requested.Major != JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR)
    {
        return JOCHONA_STATUS_PROTOCOL_VERSION_MISMATCH;
    }
    // A caller on an older *minor* version of the same major is compatible
    // (the wire format only grows within a major version); a caller ahead
    // of us on minor is also accepted since v1.0 is the only minor so far.
    return JOCHONA_STATUS_SUCCESS;
}

SlotStateMachine::Slot* SlotStateMachine::FindSlot(uint32_t slotId)
{
    for (auto& slot : slots_)
    {
        if (slot.slotId == slotId)
        {
            return &slot;
        }
    }
    return nullptr;
}

const SlotStateMachine::Slot* SlotStateMachine::FindSlot(uint32_t slotId) const
{
    for (const auto& slot : slots_)
    {
        if (slot.slotId == slotId)
        {
            return &slot;
        }
    }
    return nullptr;
}

void SlotStateMachine::ForceReleaseLocked(Slot& slot)
{
    slot.state = JochonaSlotStateFree;
    slot.ownerId = ZeroGuid();
    slot.leaseToken = ZeroGuid();
    slot.currentMode = slot.baselineMode;
    slot.lastPingMs = 0;
}

JochonaSlotInfo SlotStateMachine::ToSlotInfoLocked(const Slot& slot) const
{
    JochonaSlotInfo info{};
    info.SlotId = slot.slotId;
    info.State = slot.state;
    info.OwnerId = slot.ownerId;
    info.LeaseToken = slot.leaseToken;
    info.Mode = slot.currentMode;
    return info;
}

JochonaStatus SlotStateMachine::GetProtocolVersion(JochonaGetProtocolVersionOut& out) const
{
    out.Version.Major = JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR;
    out.Version.Minor = JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MINOR;
    static constexpr uint8_t kGuidBytes[16] = JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_BYTES;
    std::copy(std::begin(kGuidBytes), std::end(kGuidBytes), out.InterfaceGuid.Bytes);
    return JOCHONA_STATUS_SUCCESS;
}

JochonaStatus SlotStateMachine::Enumerate(
    JochonaProtocolVersion requestedVersion,
    JochonaSlotInfo* outSlots,
    uint32_t maxSlots,
    uint32_t& outCount) const
{
    if (auto status = ValidateVersion(requestedVersion); status != JOCHONA_STATUS_SUCCESS)
    {
        return status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    outCount = static_cast<uint32_t>(slots_.size());
    if (outSlots == nullptr || maxSlots < outCount)
    {
        return JOCHONA_STATUS_BUFFER_TOO_SMALL;
    }
    for (uint32_t i = 0; i < outCount; ++i)
    {
        outSlots[i] = ToSlotInfoLocked(slots_[i]);
    }
    return JOCHONA_STATUS_SUCCESS;
}

JochonaStatus SlotStateMachine::Lease(
    JochonaProtocolVersion requestedVersion,
    uint32_t slotId,
    JochonaGuid128 ownerId,
    JochonaGuid128& outToken)
{
    if (auto status = ValidateVersion(requestedVersion); status != JOCHONA_STATUS_SUCCESS)
    {
        return status;
    }
    if (IsZero(ownerId))
    {
        return JOCHONA_STATUS_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = FindSlot(slotId);
    if (slot == nullptr)
    {
        return JOCHONA_STATUS_SLOT_NOT_FOUND;
    }
    if (slot->state != JochonaSlotStateFree)
    {
        return JOCHONA_STATUS_SLOT_BUSY;
    }

    slot->state = JochonaSlotStateLeased;
    slot->ownerId = ownerId;
    slot->leaseToken = guidGenerator_();
    slot->lastPingMs = clock_();
    outToken = slot->leaseToken;
    return JOCHONA_STATUS_SUCCESS;
}

JochonaStatus SlotStateMachine::Configure(
    JochonaProtocolVersion requestedVersion,
    uint32_t slotId,
    JochonaGuid128 leaseToken,
    const JochonaSlotMode& mode,
    JochonaSlotMode& outAppliedMode)
{
    if (auto status = ValidateVersion(requestedVersion); status != JOCHONA_STATUS_SUCCESS)
    {
        return status;
    }
    if (mode.Width == 0 || mode.Height == 0 || mode.RefreshNumerator == 0 ||
        mode.RefreshDenominator == 0 || mode.BitsPerChannel == 0)
    {
        return JOCHONA_STATUS_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = FindSlot(slotId);
    if (slot == nullptr)
    {
        return JOCHONA_STATUS_SLOT_NOT_FOUND;
    }
    if (slot->state == JochonaSlotStateFree)
    {
        return JOCHONA_STATUS_SLOT_NOT_LEASED;
    }
    if (!Equals(slot->leaseToken, leaseToken))
    {
        return JOCHONA_STATUS_LEASE_TOKEN_MISMATCH;
    }

    slot->state = JochonaSlotStateConfigured;
    slot->currentMode = mode;
    slot->lastPingMs = clock_();
    outAppliedMode = slot->currentMode;
    return JOCHONA_STATUS_SUCCESS;
}

JochonaStatus SlotStateMachine::Release(
    JochonaProtocolVersion requestedVersion,
    uint32_t slotId,
    JochonaGuid128 leaseToken,
    JochonaSlotMode& outRestoredMode)
{
    if (auto status = ValidateVersion(requestedVersion); status != JOCHONA_STATUS_SUCCESS)
    {
        return status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = FindSlot(slotId);
    if (slot == nullptr)
    {
        return JOCHONA_STATUS_SLOT_NOT_FOUND;
    }
    if (slot->state == JochonaSlotStateFree)
    {
        return JOCHONA_STATUS_SLOT_NOT_LEASED;
    }
    if (!Equals(slot->leaseToken, leaseToken))
    {
        return JOCHONA_STATUS_LEASE_TOKEN_MISMATCH;
    }

    ForceReleaseLocked(*slot);
    outRestoredMode = slot->currentMode;
    return JOCHONA_STATUS_SUCCESS;
}

JochonaStatus SlotStateMachine::GetWatchdog(
    JochonaProtocolVersion requestedVersion,
    uint32_t slotId,
    JochonaGetWatchdogOut& out) const
{
    if (auto status = ValidateVersion(requestedVersion); status != JOCHONA_STATUS_SUCCESS)
    {
        return status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = FindSlot(slotId);
    if (slot == nullptr)
    {
        return JOCHONA_STATUS_SLOT_NOT_FOUND;
    }

    out.TimeoutMilliseconds = watchdogTimeoutMs_;
    if (slot->state == JochonaSlotStateFree)
    {
        out.LeaseActive = 0;
        out.MillisecondsSinceLastPing = UINT32_MAX;
    }
    else
    {
        out.LeaseActive = 1;
        const uint64_t now = clock_();
        const uint64_t elapsed = now >= slot->lastPingMs ? now - slot->lastPingMs : 0;
        out.MillisecondsSinceLastPing =
            elapsed > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(elapsed);
    }
    out.Reserved[0] = out.Reserved[1] = out.Reserved[2] = 0;
    return JOCHONA_STATUS_SUCCESS;
}

JochonaStatus SlotStateMachine::Ping(
    JochonaProtocolVersion requestedVersion,
    uint32_t slotId,
    JochonaGuid128 leaseToken)
{
    if (auto status = ValidateVersion(requestedVersion); status != JOCHONA_STATUS_SUCCESS)
    {
        return status;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    Slot* slot = FindSlot(slotId);
    if (slot == nullptr)
    {
        return JOCHONA_STATUS_SLOT_NOT_FOUND;
    }
    if (slot->state == JochonaSlotStateFree)
    {
        return JOCHONA_STATUS_SLOT_NOT_LEASED;
    }
    if (!Equals(slot->leaseToken, leaseToken))
    {
        return JOCHONA_STATUS_LEASE_TOKEN_MISMATCH;
    }

    slot->lastPingMs = clock_();
    return JOCHONA_STATUS_SUCCESS;
}

std::vector<uint32_t> SlotStateMachine::ReapOrphans()
{
    std::vector<uint32_t> reaped;
    std::lock_guard<std::mutex> lock(mutex_);
    const uint64_t now = clock_();
    for (auto& slot : slots_)
    {
        if (slot.state == JochonaSlotStateFree)
        {
            continue;
        }
        const uint64_t elapsed = now >= slot.lastPingMs ? now - slot.lastPingMs : 0;
        if (elapsed > watchdogTimeoutMs_)
        {
            ForceReleaseLocked(slot);
            reaped.push_back(slot.slotId);
        }
    }
    return reaped;
}

JochonaSlotMode SlotStateMachine::CurrentMode(uint32_t slotId) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const Slot* slot = FindSlot(slotId);
    return slot != nullptr ? slot->currentMode : kDefaultBaselineMode;
}

} // namespace jochona::protocol
