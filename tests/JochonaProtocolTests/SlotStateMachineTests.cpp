// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
#include "doctest.h"

#include <algorithm>

#include "Guid128.h"
#include "SlotStateMachine.h"

using namespace jochona::protocol;


// GCC refuses to bind a reference directly to a field of a packed struct
// (the address may be misaligned). Route each packed-field read through
// this by-value helper before handing it to doctest's CHECK/REQUIRE macros.
template <typename T>
T val(T v)
{
    return v;
}

namespace {

JochonaProtocolVersion V1()
{
    return JochonaProtocolVersion{JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR,
                                   JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MINOR};
}

JochonaGuid128 Owner(uint8_t seed)
{
    JochonaGuid128 guid{};
    guid.Bytes[0] = seed;
    return guid;
}

JochonaSlotMode Mode1440p()
{
    return JochonaSlotMode{2560, 1440, 60, 1, 10, 1, {0, 0, 0}};
}

// Deterministic test harness: fixed clock (advanced manually) + sequential
// GUID generator so lease tokens are distinguishable but reproducible.
class TestMachine
{
public:
    TestMachine(uint32_t slotCount = 1, uint32_t watchdogTimeoutMs = 5000)
        : machine_(
              slotCount,
              kDefaultBaselineMode,
              watchdogTimeoutMs,
              [this] { return clockMs_; },
              [this] {
                  JochonaGuid128 guid{};
                  guid.Bytes[0] = static_cast<uint8_t>(0x40 + nextGuid_);
                  guid.Bytes[1] = static_cast<uint8_t>(nextGuid_ >> 8);
                  ++nextGuid_;
                  return guid;
              })
    {
    }

    SlotStateMachine& machine() { return machine_; }
    void Advance(uint64_t ms) { clockMs_ += ms; }

private:
    uint64_t clockMs_ = 1000;
    uint32_t nextGuid_ = 0;
    SlotStateMachine machine_;
};

} // namespace

TEST_CASE("GetProtocolVersion reports v1.0 and the contract GUID")
{
    TestMachine harness;
    JochonaGetProtocolVersionOut out{};
    REQUIRE(harness.machine().GetProtocolVersion(out) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(out.Version.Major) == 1);
    CHECK(val(out.Version.Minor) == 0);
    static constexpr uint8_t kExpected[16] = JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_BYTES;
    CHECK(std::equal(std::begin(kExpected), std::end(kExpected), out.InterfaceGuid.Bytes));
}

TEST_CASE("A freshly constructed machine enumerates exactly one free default slot")
{
    TestMachine harness;
    JochonaSlotInfo slots[JOCHONA_PROTOCOL_V1_MAX_SLOTS];
    uint32_t count = 0;
    REQUIRE(harness.machine().Enumerate(V1(), slots, JOCHONA_PROTOCOL_V1_MAX_SLOTS, count) ==
            JOCHONA_STATUS_SUCCESS);
    REQUIRE(count == 1);
    CHECK(val(slots[0].SlotId) == 0);
    CHECK(val(slots[0].State) == JochonaSlotStateFree);
    CHECK(IsZero(slots[0].OwnerId));
    CHECK(IsZero(slots[0].LeaseToken));
    CHECK(val(slots[0].Mode.Width) == kDefaultBaselineMode.Width);
    CHECK(val(slots[0].Mode.Height) == kDefaultBaselineMode.Height);
}

TEST_CASE("Full lease -> configure -> release lifecycle transitions state and restores baseline")
{
    TestMachine harness;
    auto& sm = harness.machine();
    const auto owner = Owner(1);

    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, owner, token) == JOCHONA_STATUS_SUCCESS);
    CHECK_FALSE(IsZero(token));

    {
        JochonaSlotInfo slots[1];
        uint32_t count = 0;
        REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
        CHECK(val(slots[0].State) == JochonaSlotStateLeased);
        CHECK(Equals(slots[0].OwnerId, owner));
        CHECK(Equals(slots[0].LeaseToken, token));
    }

    JochonaSlotMode applied{};
    REQUIRE(sm.Configure(V1(), 0, token, Mode1440p(), applied) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(applied.Width) == 2560);
    CHECK(applied.HdrEnabled == 1);

    {
        JochonaSlotInfo slots[1];
        uint32_t count = 0;
        REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
        CHECK(val(slots[0].State) == JochonaSlotStateConfigured);
        CHECK(val(slots[0].Mode.Width) == 2560);
    }

    JochonaSlotMode restored{};
    REQUIRE(sm.Release(V1(), 0, token, restored) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(restored.Width) == kDefaultBaselineMode.Width);
    CHECK(restored.HdrEnabled == kDefaultBaselineMode.HdrEnabled);

    JochonaSlotInfo slots[1];
    uint32_t count = 0;
    REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(slots[0].State) == JochonaSlotStateFree);
    CHECK(IsZero(slots[0].OwnerId));
    CHECK(IsZero(slots[0].LeaseToken));
    CHECK(val(slots[0].Mode.Width) == kDefaultBaselineMode.Width);
}

TEST_CASE("Leasing an already-leased slot is rejected as busy, without disturbing the holder")
{
    TestMachine harness;
    auto& sm = harness.machine();
    JochonaGuid128 firstToken{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), firstToken) == JOCHONA_STATUS_SUCCESS);

    JochonaGuid128 secondToken{};
    CHECK(sm.Lease(V1(), 0, Owner(2), secondToken) == JOCHONA_STATUS_SLOT_BUSY);

    JochonaSlotInfo slots[1];
    uint32_t count = 0;
    REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
    CHECK(Equals(slots[0].LeaseToken, firstToken));
    CHECK(Equals(slots[0].OwnerId, Owner(1)));
}

TEST_CASE("Configure/Release with the wrong lease token is rejected and state is unchanged")
{
    TestMachine harness;
    auto& sm = harness.machine();
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), token) == JOCHONA_STATUS_SUCCESS);

    const JochonaGuid128 wrongToken = Owner(0xEE);
    JochonaSlotMode applied{};
    CHECK(sm.Configure(V1(), 0, wrongToken, Mode1440p(), applied) ==
          JOCHONA_STATUS_LEASE_TOKEN_MISMATCH);

    JochonaSlotMode restored{};
    CHECK(sm.Release(V1(), 0, wrongToken, restored) == JOCHONA_STATUS_LEASE_TOKEN_MISMATCH);

    JochonaSlotInfo slots[1];
    uint32_t count = 0;
    REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(slots[0].State) == JochonaSlotStateLeased);
    CHECK(Equals(slots[0].LeaseToken, token));
}

TEST_CASE("Configure/Release/Ping on a Free slot report SLOT_NOT_LEASED")
{
    TestMachine harness;
    auto& sm = harness.machine();
    const JochonaGuid128 anyToken = Owner(1);

    JochonaSlotMode applied{};
    CHECK(sm.Configure(V1(), 0, anyToken, Mode1440p(), applied) == JOCHONA_STATUS_SLOT_NOT_LEASED);

    JochonaSlotMode restored{};
    CHECK(sm.Release(V1(), 0, anyToken, restored) == JOCHONA_STATUS_SLOT_NOT_LEASED);

    CHECK(sm.Ping(V1(), 0, anyToken) == JOCHONA_STATUS_SLOT_NOT_LEASED);
}

TEST_CASE("Operations against a nonexistent slot id report SLOT_NOT_FOUND")
{
    TestMachine harness;
    auto& sm = harness.machine();
    JochonaGuid128 token{};
    CHECK(sm.Lease(V1(), 99, Owner(1), token) == JOCHONA_STATUS_SLOT_NOT_FOUND);

    JochonaGetWatchdogOut wd{};
    CHECK(sm.GetWatchdog(V1(), 99, wd) == JOCHONA_STATUS_SLOT_NOT_FOUND);
}

TEST_CASE("Lease with a zero owner id is rejected as an invalid parameter")
{
    TestMachine harness;
    JochonaGuid128 token{};
    CHECK(harness.machine().Lease(V1(), 0, ZeroGuid(), token) == JOCHONA_STATUS_INVALID_PARAMETER);
}

TEST_CASE("Configure with a zero-valued mode field is rejected as an invalid parameter")
{
    TestMachine harness;
    auto& sm = harness.machine();
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), token) == JOCHONA_STATUS_SUCCESS);

    JochonaSlotMode badMode = Mode1440p();
    badMode.RefreshDenominator = 0;
    JochonaSlotMode applied{};
    CHECK(sm.Configure(V1(), 0, token, badMode, applied) == JOCHONA_STATUS_INVALID_PARAMETER);
}

TEST_CASE("A protocol major-version mismatch is rejected on every entry point")
{
    TestMachine harness;
    auto& sm = harness.machine();
    const JochonaProtocolVersion futureMajor{2, 0};

    JochonaSlotInfo slots[1];
    uint32_t count = 0;
    CHECK(sm.Enumerate(futureMajor, slots, 1, count) == JOCHONA_STATUS_PROTOCOL_VERSION_MISMATCH);

    JochonaGuid128 token{};
    CHECK(sm.Lease(futureMajor, 0, Owner(1), token) == JOCHONA_STATUS_PROTOCOL_VERSION_MISMATCH);
}

TEST_CASE("GetWatchdog reflects lease activity and elapsed time since the last ping")
{
    TestMachine harness;
    auto& sm = harness.machine();

    JochonaGetWatchdogOut before{};
    REQUIRE(sm.GetWatchdog(V1(), 0, before) == JOCHONA_STATUS_SUCCESS);
    CHECK(before.LeaseActive == 0);
    CHECK(val(before.MillisecondsSinceLastPing) == UINT32_MAX);

    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), token) == JOCHONA_STATUS_SUCCESS);
    harness.Advance(1500);

    JochonaGetWatchdogOut afterLease{};
    REQUIRE(sm.GetWatchdog(V1(), 0, afterLease) == JOCHONA_STATUS_SUCCESS);
    CHECK(afterLease.LeaseActive == 1);
    CHECK(val(afterLease.MillisecondsSinceLastPing) == 1500);

    REQUIRE(sm.Ping(V1(), 0, token) == JOCHONA_STATUS_SUCCESS);
    JochonaGetWatchdogOut afterPing{};
    REQUIRE(sm.GetWatchdog(V1(), 0, afterPing) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(afterPing.MillisecondsSinceLastPing) == 0);
}

TEST_CASE("ReapOrphans force-releases a lease that has been silent past the watchdog timeout")
{
    TestMachine harness{1, /*watchdogTimeoutMs=*/2000};
    auto& sm = harness.machine();
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), token) == JOCHONA_STATUS_SUCCESS);
    JochonaSlotMode applied{};
    REQUIRE(sm.Configure(V1(), 0, token, Mode1440p(), applied) == JOCHONA_STATUS_SUCCESS);

    // Still within the timeout: nothing reaped.
    harness.Advance(1000);
    CHECK(sm.ReapOrphans().empty());

    // Past the timeout with no ping: orphan is reaped and mode restored.
    harness.Advance(1500);
    const auto reaped = sm.ReapOrphans();
    REQUIRE(reaped.size() == 1);
    CHECK(reaped[0] == 0);
    CHECK(sm.CurrentMode(0).Width == kDefaultBaselineMode.Width);

    JochonaSlotInfo slots[1];
    uint32_t count = 0;
    REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(slots[0].State) == JochonaSlotStateFree);
}

TEST_CASE("A watchdog ping resets the orphan timer and prevents reaping")
{
    TestMachine harness{1, /*watchdogTimeoutMs=*/2000};
    auto& sm = harness.machine();
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), token) == JOCHONA_STATUS_SUCCESS);

    harness.Advance(1900);
    REQUIRE(sm.Ping(V1(), 0, token) == JOCHONA_STATUS_SUCCESS);
    harness.Advance(1900);
    CHECK(sm.ReapOrphans().empty());

    JochonaSlotInfo slots[1];
    uint32_t count = 0;
    REQUIRE(sm.Enumerate(V1(), slots, 1, count) == JOCHONA_STATUS_SUCCESS);
    CHECK(val(slots[0].State) == JochonaSlotStateLeased);
}

TEST_CASE("After a reaped lease, a new owner can lease the slot again")
{
    TestMachine harness{1, /*watchdogTimeoutMs=*/1000};
    auto& sm = harness.machine();
    JochonaGuid128 firstToken{};
    REQUIRE(sm.Lease(V1(), 0, Owner(1), firstToken) == JOCHONA_STATUS_SUCCESS);

    harness.Advance(1500);
    REQUIRE(sm.ReapOrphans().size() == 1);

    JochonaGuid128 secondToken{};
    REQUIRE(sm.Lease(V1(), 0, Owner(2), secondToken) == JOCHONA_STATUS_SUCCESS);
    CHECK_FALSE(Equals(secondToken, firstToken));
}
