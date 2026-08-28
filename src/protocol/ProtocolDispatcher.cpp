// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
#include "ProtocolDispatcher.h"

#include <cstring>

namespace jochona::protocol {

namespace {

template <typename T>
const T* AsInput(const void* buffer, size_t length)
{
    if (buffer == nullptr || length < sizeof(T))
    {
        return nullptr;
    }
    return reinterpret_cast<const T*>(buffer);
}

template <typename T>
T* AsOutput(void* buffer, size_t length)
{
    if (buffer == nullptr || length < sizeof(T))
    {
        return nullptr;
    }
    return reinterpret_cast<T*>(buffer);
}

DispatchResult BufferTooSmall()
{
    return DispatchResult{JOCHONA_STATUS_BUFFER_TOO_SMALL, 0};
}

} // namespace

DispatchResult Dispatch(
    uint32_t ioControlCode,
    const void* inputBuffer,
    size_t inputLength,
    void* outputBuffer,
    size_t outputLength,
    SlotStateMachine& stateMachine,
    const SetRenderAdapterLuidFn& setRenderAdapterLuid)
{
    switch (ioControlCode)
    {
    case IOCTL_JOCHONA_GET_PROTOCOL_VERSION:
    {
        auto* out = AsOutput<JochonaGetProtocolVersionOut>(outputBuffer, outputLength);
        if (out == nullptr)
        {
            return BufferTooSmall();
        }
        const JochonaStatus status = stateMachine.GetProtocolVersion(*out);
        return DispatchResult{status, status == JOCHONA_STATUS_SUCCESS ? sizeof(*out) : 0};
    }

    case IOCTL_JOCHONA_ENUMERATE_SLOTS:
    {
        const auto* in = AsInput<JochonaEnumerateSlotsIn>(inputBuffer, inputLength);
        auto* out = AsOutput<JochonaEnumerateSlotsOut>(outputBuffer, outputLength);
        if (in == nullptr || out == nullptr)
        {
            return BufferTooSmall();
        }
        uint32_t count = 0;
        const JochonaStatus status = stateMachine.Enumerate(
            in->RequestedVersion, out->Slots, JOCHONA_PROTOCOL_V1_MAX_SLOTS, count);
        if (status != JOCHONA_STATUS_SUCCESS)
        {
            return DispatchResult{status, 0};
        }
        out->SlotCount = count;
        return DispatchResult{status, sizeof(*out)};
    }

    case IOCTL_JOCHONA_LEASE_SLOT:
    {
        const auto* in = AsInput<JochonaLeaseSlotIn>(inputBuffer, inputLength);
        auto* out = AsOutput<JochonaLeaseSlotOut>(outputBuffer, outputLength);
        if (in == nullptr || out == nullptr)
        {
            return BufferTooSmall();
        }
        JochonaGuid128 token{};
        const JochonaStatus status =
            stateMachine.Lease(in->RequestedVersion, in->SlotId, in->OwnerId, token);
        if (status != JOCHONA_STATUS_SUCCESS)
        {
            return DispatchResult{status, 0};
        }
        out->LeaseToken = token;
        return DispatchResult{status, sizeof(*out)};
    }

    case IOCTL_JOCHONA_CONFIGURE_SLOT:
    {
        const auto* in = AsInput<JochonaConfigureSlotIn>(inputBuffer, inputLength);
        if (in == nullptr)
        {
            return BufferTooSmall();
        }
        JochonaSlotMode applied{};
        const JochonaStatus status = stateMachine.Configure(
            in->RequestedVersion, in->SlotId, in->LeaseToken, in->Mode, applied);
        return DispatchResult{status, 0};
    }

    case IOCTL_JOCHONA_RELEASE_SLOT:
    {
        const auto* in = AsInput<JochonaReleaseSlotIn>(inputBuffer, inputLength);
        if (in == nullptr)
        {
            return BufferTooSmall();
        }
        JochonaSlotMode restored{};
        const JochonaStatus status =
            stateMachine.Release(in->RequestedVersion, in->SlotId, in->LeaseToken, restored);
        return DispatchResult{status, 0};
    }

    case IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID:
    {
        const auto* in = AsInput<JochonaSetRenderAdapterLuidIn>(inputBuffer, inputLength);
        if (in == nullptr)
        {
            return BufferTooSmall();
        }
        const JochonaStatus status = setRenderAdapterLuid
            ? setRenderAdapterLuid(in->LuidLowPart, in->LuidHighPart)
            : JOCHONA_STATUS_SUCCESS;
        return DispatchResult{status, 0};
    }

    case IOCTL_JOCHONA_GET_WATCHDOG:
    {
        const auto* in = AsInput<JochonaGetWatchdogIn>(inputBuffer, inputLength);
        auto* out = AsOutput<JochonaGetWatchdogOut>(outputBuffer, outputLength);
        if (in == nullptr || out == nullptr)
        {
            return BufferTooSmall();
        }
        const JochonaStatus status = stateMachine.GetWatchdog(in->RequestedVersion, in->SlotId, *out);
        return DispatchResult{status, status == JOCHONA_STATUS_SUCCESS ? sizeof(*out) : 0};
    }

    case IOCTL_JOCHONA_WATCHDOG_PING:
    {
        const auto* in = AsInput<JochonaWatchdogPingIn>(inputBuffer, inputLength);
        if (in == nullptr)
        {
            return BufferTooSmall();
        }
        const JochonaStatus status =
            stateMachine.Ping(in->RequestedVersion, in->SlotId, in->LeaseToken);
        return DispatchResult{status, 0};
    }

    default:
        return DispatchResult{JOCHONA_STATUS_UNKNOWN_IOCTL, 0};
    }
}

} // namespace jochona::protocol
