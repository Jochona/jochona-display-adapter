// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
//
// Portable slot lease/configure/release state machine for the Jochona
// Display Adapter protocol (v1.0). Deliberately free of any WDF/IddCx/
// Windows dependency so it can be exercised by tests/JochonaProtocolTests
// on any host platform; the driver links this file unmodified and wires
// real clock/GUID sources through the constructor.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

#include "jochona/display_adapter_abi.h"

namespace jochona::protocol {

// One stable default monitor slot's baseline (restore-on-release) mode:
// 1920x1080@60Hz, 8 bits per channel, SDR. Matches the Host contract's
// proven H.264 1920x1080@60 baseline tuple.
inline constexpr JochonaSlotMode kDefaultBaselineMode{
    /* Width */ 1920,
    /* Height */ 1080,
    /* RefreshNumerator */ 60,
    /* RefreshDenominator */ 1,
    /* BitsPerChannel */ 8,
    /* HdrEnabled */ 0,
    /* Reserved */ {0, 0, 0},
};

inline constexpr uint32_t kDefaultWatchdogTimeoutMs = 5000;

class SlotStateMachine
{
public:
    using ClockFn = std::function<uint64_t()>;         // monotonic milliseconds
    using GuidGeneratorFn = std::function<JochonaGuid128()>;

    // slotCount MUST NOT exceed JOCHONA_PROTOCOL_V1_MAX_SLOTS (the v1.0
    // wire format's fixed slot array bound). The default matches the
    // contract's "default pool capacity 1".
    explicit SlotStateMachine(
        uint32_t slotCount = JOCHONA_PROTOCOL_V1_MAX_SLOTS,
        JochonaSlotMode baselineMode = kDefaultBaselineMode,
        uint32_t watchdogTimeoutMs = kDefaultWatchdogTimeoutMs,
        ClockFn clock = DefaultClock,
        GuidGeneratorFn guidGenerator = DefaultGuidGenerator);

    JochonaStatus GetProtocolVersion(JochonaGetProtocolVersionOut& out) const;

    JochonaStatus Enumerate(
        JochonaProtocolVersion requestedVersion,
        JochonaSlotInfo* outSlots,
        uint32_t maxSlots,
        uint32_t& outCount) const;

    JochonaStatus Lease(
        JochonaProtocolVersion requestedVersion,
        uint32_t slotId,
        JochonaGuid128 ownerId,
        JochonaGuid128& outToken);

    JochonaStatus Configure(
        JochonaProtocolVersion requestedVersion,
        uint32_t slotId,
        JochonaGuid128 leaseToken,
        const JochonaSlotMode& mode,
        JochonaSlotMode& outAppliedMode);

    JochonaStatus Release(
        JochonaProtocolVersion requestedVersion,
        uint32_t slotId,
        JochonaGuid128 leaseToken,
        JochonaSlotMode& outRestoredMode);

    JochonaStatus GetWatchdog(
        JochonaProtocolVersion requestedVersion,
        uint32_t slotId,
        JochonaGetWatchdogOut& out) const;

    JochonaStatus Ping(
        JochonaProtocolVersion requestedVersion,
        uint32_t slotId,
        JochonaGuid128 leaseToken);

    // Invoked periodically (e.g. by the driver's WDF timer). Any slot whose
    // lease has been silent for longer than watchdogTimeoutMs is force
    // released and its mode restored to baseline. Returns the slot ids
    // that were reaped, so the caller can push a corresponding IddCx mode
    // update for each one.
    std::vector<uint32_t> ReapOrphans();

    // Non-protocol accessor: current live mode of a slot (baseline if the
    // slot has never been configured or is Free). Used by the driver to
    // seed the initial IddCx target-mode list.
    JochonaSlotMode CurrentMode(uint32_t slotId) const;

    uint32_t WatchdogTimeoutMilliseconds() const noexcept { return watchdogTimeoutMs_; }

    static uint64_t DefaultClock();
    static JochonaGuid128 DefaultGuidGenerator();

private:
    struct Slot
    {
        uint32_t slotId = 0;
        JochonaSlotState state = JochonaSlotStateFree;
        JochonaGuid128 ownerId{};
        JochonaGuid128 leaseToken{};
        JochonaSlotMode baselineMode{};
        JochonaSlotMode currentMode{};
        uint64_t lastPingMs = 0;
    };

    static JochonaStatus ValidateVersion(JochonaProtocolVersion requested);
    Slot* FindSlot(uint32_t slotId);
    const Slot* FindSlot(uint32_t slotId) const;
    void ForceReleaseLocked(Slot& slot);
    JochonaSlotInfo ToSlotInfoLocked(const Slot& slot) const;

    mutable std::mutex mutex_;
    std::vector<Slot> slots_;
    uint32_t watchdogTimeoutMs_;
    ClockFn clock_;
    GuidGeneratorFn guidGenerator_;
};

} // namespace jochona::protocol
