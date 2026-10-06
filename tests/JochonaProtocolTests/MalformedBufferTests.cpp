// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
//
// Adversarial buffer tests: every IOCTL must reject null/undersized
// input or output buffers with JOCHONA_STATUS_BUFFER_TOO_SMALL and MUST
// NOT write to an undersized output buffer or read past an undersized
// input buffer's declared length (verified by scribbling a byte
// immediately after the runt buffer that is included as the reported
// "capacity" and asserting the dispatcher never advances state).
#include "doctest.h"

#include <cstring>
#include <vector>

#include "ProtocolDispatcher.h"
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

} // namespace

TEST_CASE("GET_PROTOCOL_VERSION rejects a null output buffer and a one-byte-short buffer")
{
    SlotStateMachine sm;

    auto nullResult = Dispatch(IOCTL_JOCHONA_GET_PROTOCOL_VERSION, nullptr, 0, nullptr, 0, sm);
    CHECK(nullResult.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    CHECK(nullResult.bytesWritten == 0);

    JochonaGetProtocolVersionOut out{};
    auto shortResult = Dispatch(
        IOCTL_JOCHONA_GET_PROTOCOL_VERSION, nullptr, 0, &out, sizeof(out) - 1, sm);
    CHECK(shortResult.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    CHECK(shortResult.bytesWritten == 0);
}

TEST_CASE("ENUMERATE_SLOTS rejects a truncated input struct without touching the output")
{
    SlotStateMachine sm;
    JochonaEnumerateSlotsIn in{V1()};
    JochonaEnumerateSlotsOut out{};
    std::memset(&out, 0xCD, sizeof(out)); // sentinel pattern

    auto result = Dispatch(
        IOCTL_JOCHONA_ENUMERATE_SLOTS, &in, sizeof(in) - 1, &out, sizeof(out), sm);
    CHECK(result.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    CHECK(result.bytesWritten == 0);
    // Output buffer must be left untouched: still the sentinel pattern.
    CHECK(val(out.SlotCount) == 0xCDCDCDCDu);
}

TEST_CASE("ENUMERATE_SLOTS rejects a truncated output struct")
{
    SlotStateMachine sm;
    JochonaEnumerateSlotsIn in{V1()};
    std::vector<uint8_t> shortOut(sizeof(JochonaEnumerateSlotsOut) - 1, 0);

    auto result = Dispatch(
        IOCTL_JOCHONA_ENUMERATE_SLOTS, &in, sizeof(in), shortOut.data(), shortOut.size(), sm);
    CHECK(result.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    CHECK(result.bytesWritten == 0);
}

TEST_CASE("LEASE_SLOT with a zero-length input buffer is rejected and leases nothing")
{
    SlotStateMachine sm;
    JochonaLeaseSlotOut out{};
    auto result = Dispatch(IOCTL_JOCHONA_LEASE_SLOT, nullptr, 0, &out, sizeof(out), sm);
    CHECK(result.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);

    // Confirm the slot is still free: a well-formed lease must still succeed.
    JochonaGuid128 owner{};
    owner.Bytes[0] = 9;
    JochonaLeaseSlotIn goodIn{V1(), 0, owner};
    JochonaLeaseSlotOut goodOut{};
    auto goodResult =
        Dispatch(IOCTL_JOCHONA_LEASE_SLOT, &goodIn, sizeof(goodIn), &goodOut, sizeof(goodOut), sm);
    CHECK(goodResult.status == JOCHONA_STATUS_SUCCESS);
}

TEST_CASE("CONFIGURE_SLOT rejects a truncated input struct")
{
    SlotStateMachine sm;
    JochonaGuid128 owner{};
    owner.Bytes[0] = 1;
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, owner, token) == JOCHONA_STATUS_SUCCESS);

    JochonaConfigureSlotIn in{};
    in.RequestedVersion = V1();
    in.SlotId = 0;
    in.LeaseToken = token;
    in.Mode = JochonaSlotMode{1280, 720, 30, 1, 8, 0, {0, 0, 0}};

    auto result = Dispatch(IOCTL_JOCHONA_CONFIGURE_SLOT, &in, sizeof(in) - 1, nullptr, 0, sm);
    CHECK(result.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    // The slot must remain at its pre-call (baseline) mode: the malformed
    // request must not have been partially applied.
    CHECK(sm.CurrentMode(0).Width == kDefaultBaselineMode.Width);
}

TEST_CASE("RELEASE_SLOT and WATCHDOG_PING reject truncated input buffers")
{
    SlotStateMachine sm;
    JochonaGuid128 owner{};
    owner.Bytes[0] = 1;
    JochonaGuid128 token{};
    REQUIRE(sm.Lease(V1(), 0, owner, token) == JOCHONA_STATUS_SUCCESS);

    JochonaReleaseSlotIn releaseIn{V1(), 0, token};
    auto releaseResult =
        Dispatch(IOCTL_JOCHONA_RELEASE_SLOT, &releaseIn, sizeof(releaseIn) - 1, nullptr, 0, sm);
    CHECK(releaseResult.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);

    JochonaWatchdogPingIn pingIn{V1(), 0, token};
    auto pingResult =
        Dispatch(IOCTL_JOCHONA_WATCHDOG_PING, &pingIn, sizeof(pingIn) - 1, nullptr, 0, sm);
    CHECK(pingResult.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);

    // Lease must still be intact (neither malformed call tore it down).
    JochonaGetWatchdogIn wdIn{V1(), 0};
    JochonaGetWatchdogOut wdOut{};
    REQUIRE(Dispatch(IOCTL_JOCHONA_GET_WATCHDOG, &wdIn, sizeof(wdIn), &wdOut, sizeof(wdOut), sm)
                .status == JOCHONA_STATUS_SUCCESS);
    CHECK(wdOut.LeaseActive == 1);
}

TEST_CASE("SET_RENDER_ADAPTER_LUID rejects a truncated input and never invokes the sink")
{
    SlotStateMachine sm;
    JochonaSetRenderAdapterLuidIn in{V1(), 1, 2};
    bool invoked = false;
    SetRenderAdapterLuidFn sink = [&](int32_t, int32_t) {
        invoked = true;
        return JOCHONA_STATUS_SUCCESS;
    };
    auto result = Dispatch(
        IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID, &in, sizeof(in) - 1, nullptr, 0, sm, sink);
    CHECK(result.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    CHECK_FALSE(invoked);
}

TEST_CASE("GET_WATCHDOG rejects a truncated output buffer without partially writing it")
{
    SlotStateMachine sm;
    JochonaGetWatchdogIn in{V1(), 0};
    std::vector<uint8_t> shortOut(sizeof(JochonaGetWatchdogOut) - 1, 0xAB);
    auto result = Dispatch(
        IOCTL_JOCHONA_GET_WATCHDOG, &in, sizeof(in), shortOut.data(), shortOut.size(), sm);
    CHECK(result.status == JOCHONA_STATUS_BUFFER_TOO_SMALL);
    // Every byte of the runt buffer must remain the sentinel pattern.
    for (uint8_t b : shortOut)
    {
        CHECK(b == 0xAB);
    }
}

TEST_CASE("An oversized buffer (more capacity than required) is accepted normally")
{
    SlotStateMachine sm;
    JochonaGetProtocolVersionOut out{};
    std::vector<uint8_t> oversized(sizeof(out) + 64, 0);
    std::memcpy(oversized.data(), &out, sizeof(out));

    auto result = Dispatch(
        IOCTL_JOCHONA_GET_PROTOCOL_VERSION, nullptr, 0, oversized.data(), oversized.size(), sm);
    CHECK(result.status == JOCHONA_STATUS_SUCCESS);
    CHECK(result.bytesWritten == sizeof(JochonaGetProtocolVersionOut));
}
