// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
//
// End-to-end tests of the ProtocolDispatcher: exercises every one of the
// eight specified IOCTLs (GET_PROTOCOL_VERSION, ENUMERATE_SLOTS,
// LEASE_SLOT, CONFIGURE_SLOT, RELEASE_SLOT, SET_RENDER_ADAPTER_LUID,
// GET_WATCHDOG, WATCHDOG_PING) through the exact (code, inputBuffer,
// outputBuffer) shape the driver's WDF boundary supplies.
#include "doctest.h"

#include "Guid128.h"
#include "ProtocolDispatcher.h"
#include "SlotStateMachine.h"

using namespace jochona::protocol;

namespace {

JochonaProtocolVersion V1()
{
    return JochonaProtocolVersion{JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR,
                                   JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MINOR};
}

} // namespace

TEST_CASE("Dispatch(GET_PROTOCOL_VERSION) returns the wire-identical version and GUID")
{
    SlotStateMachine sm;
    JochonaGetProtocolVersionOut out{};
    const auto result = Dispatch(
        IOCTL_JOCHONA_GET_PROTOCOL_VERSION, nullptr, 0, &out, sizeof(out), sm);
    CHECK(result.status == JOCHONA_STATUS_SUCCESS);
    CHECK(result.bytesWritten == sizeof(out));
    CHECK(out.Version.Major == 1);
    CHECK(out.Version.Minor == 0);
}

TEST_CASE("Dispatch(ENUMERATE_SLOTS) round-trips through raw buffers")
{
    SlotStateMachine sm;
    JochonaEnumerateSlotsIn in{V1()};
    JochonaEnumerateSlotsOut out{};
    const auto result = Dispatch(
        IOCTL_JOCHONA_ENUMERATE_SLOTS, &in, sizeof(in), &out, sizeof(out), sm);
    CHECK(result.status == JOCHONA_STATUS_SUCCESS);
    REQUIRE(out.SlotCount == 1);
    CHECK(out.Slots[0].State == JochonaSlotStateFree);
}

TEST_CASE("Dispatch drives the full LEASE -> CONFIGURE -> RELEASE cycle end to end")
{
    SlotStateMachine sm;

    JochonaGuid128 owner{};
    owner.Bytes[0] = 0x7A;
    JochonaLeaseSlotIn leaseIn{V1(), 0, owner};
    JochonaLeaseSlotOut leaseOut{};
    auto leaseResult = Dispatch(
        IOCTL_JOCHONA_LEASE_SLOT, &leaseIn, sizeof(leaseIn), &leaseOut, sizeof(leaseOut), sm);
    REQUIRE(leaseResult.status == JOCHONA_STATUS_SUCCESS);
    CHECK_FALSE(IsZero(leaseOut.LeaseToken));

    JochonaConfigureSlotIn configureIn{};
    configureIn.RequestedVersion = V1();
    configureIn.SlotId = 0;
    configureIn.LeaseToken = leaseOut.LeaseToken;
    configureIn.Mode = JochonaSlotMode{3840, 2160, 120, 1, 10, 1, {0, 0, 0}};
    auto configureResult =
        Dispatch(IOCTL_JOCHONA_CONFIGURE_SLOT, &configureIn, sizeof(configureIn), nullptr, 0, sm);
    CHECK(configureResult.status == JOCHONA_STATUS_SUCCESS);
    CHECK(sm.CurrentMode(0).Width == 3840);

    JochonaReleaseSlotIn releaseIn{V1(), 0, leaseOut.LeaseToken};
    auto releaseResult =
        Dispatch(IOCTL_JOCHONA_RELEASE_SLOT, &releaseIn, sizeof(releaseIn), nullptr, 0, sm);
    CHECK(releaseResult.status == JOCHONA_STATUS_SUCCESS);
    CHECK(sm.CurrentMode(0).Width == kDefaultBaselineMode.Width);
}

TEST_CASE("Dispatch(SET_RENDER_ADAPTER_LUID) invokes the injected sink with the exact LUID")
{
    SlotStateMachine sm;
    JochonaSetRenderAdapterLuidIn in{V1(), /*LuidLowPart=*/0x1234, /*LuidHighPart=*/-7};

    int32_t observedLow = 0;
    int32_t observedHigh = 0;
    bool invoked = false;
    SetRenderAdapterLuidFn sink = [&](int32_t low, int32_t high) {
        invoked = true;
        observedLow = low;
        observedHigh = high;
        return JOCHONA_STATUS_SUCCESS;
    };

    const auto result = Dispatch(
        IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID, &in, sizeof(in), nullptr, 0, sm, sink);
    CHECK(result.status == JOCHONA_STATUS_SUCCESS);
    REQUIRE(invoked);
    CHECK(observedLow == 0x1234);
    CHECK(observedHigh == -7);
}

TEST_CASE("Dispatch(SET_RENDER_ADAPTER_LUID) propagates a sink failure status verbatim")
{
    SlotStateMachine sm;
    JochonaSetRenderAdapterLuidIn in{V1(), 1, 1};
    SetRenderAdapterLuidFn sink = [](int32_t, int32_t) {
        return JOCHONA_STATUS_INVALID_PARAMETER;
    };
    const auto result = Dispatch(
        IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID, &in, sizeof(in), nullptr, 0, sm, sink);
    CHECK(result.status == JOCHONA_STATUS_INVALID_PARAMETER);
}

TEST_CASE("Dispatch(GET_WATCHDOG / WATCHDOG_PING) reflects the leased slot's timer")
{
    SlotStateMachine sm;
    JochonaGuid128 owner{};
    owner.Bytes[0] = 1;
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, owner, token) == JOCHONA_STATUS_SUCCESS);

    JochonaGetWatchdogIn wdIn{V1(), 0};
    JochonaGetWatchdogOut wdOut{};
    auto wdResult =
        Dispatch(IOCTL_JOCHONA_GET_WATCHDOG, &wdIn, sizeof(wdIn), &wdOut, sizeof(wdOut), sm);
    CHECK(wdResult.status == JOCHONA_STATUS_SUCCESS);
    CHECK(wdOut.LeaseActive == 1);
    CHECK(wdOut.TimeoutMilliseconds == sm.WatchdogTimeoutMilliseconds());

    JochonaWatchdogPingIn pingIn{V1(), 0, token};
    auto pingResult = Dispatch(IOCTL_JOCHONA_WATCHDOG_PING, &pingIn, sizeof(pingIn), nullptr, 0, sm);
    CHECK(pingResult.status == JOCHONA_STATUS_SUCCESS);

    JochonaWatchdogPingIn wrongPing{V1(), 0, owner /* not the lease token */};
    auto wrongPingResult =
        Dispatch(IOCTL_JOCHONA_WATCHDOG_PING, &wrongPing, sizeof(wrongPing), nullptr, 0, sm);
    CHECK(wrongPingResult.status == JOCHONA_STATUS_LEASE_TOKEN_MISMATCH);
}

TEST_CASE("Dispatch of an unrecognized IOCTL code reports UNKNOWN_IOCTL")
{
    SlotStateMachine sm;
    const uint32_t bogusCode = 0xDEAD0000u;
    const auto result = Dispatch(bogusCode, nullptr, 0, nullptr, 0, sm);
    CHECK(result.status == JOCHONA_STATUS_UNKNOWN_IOCTL);
    CHECK(result.bytesWritten == 0);
}
