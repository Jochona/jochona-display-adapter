// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jochona project contributors
//
// Pure IOCTL dispatch layer: maps a raw (ioControlCode, inputBuffer,
// outputBuffer) triple — exactly what a WDF EvtIddCxDeviceIoControl
// callback receives after WdfRequestRetrieveInputBuffer/OutputBuffer —
// onto SlotStateMachine calls. Kept free of WDF/NTSTATUS types so it is
// unit-testable on any host; driver/JochonaDisplayAdapter/IoControl.cpp
// is a thin adapter that marshals WDFREQUEST buffers into this call and
// maps JochonaStatus back to NTSTATUS.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>

#include "SlotStateMachine.h"
#include "jochona/display_adapter_abi.h"

namespace jochona::protocol {

struct DispatchResult
{
    JochonaStatus status = JOCHONA_STATUS_UNKNOWN_IOCTL;
    size_t bytesWritten = 0; // bytes actually written into outputBuffer
};

// Invoked for IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID once the request
// buffer has passed size validation. The driver's real implementation
// pins the IDDCX_ADAPTER's render adapter via IddCxAdapterSetRenderAdapter
// and returns JOCHONA_STATUS_SUCCESS, or JOCHONA_STATUS_INVALID_PARAMETER
// if no such adapter LUID is present on the system. Tests may inject a
// fake sink to observe the value without any IddCx dependency; omitting
// the sink (default) treats the call as a validated no-op, which is
// sufficient to exercise buffer-size/malformed-request handling.
using SetRenderAdapterLuidFn = std::function<JochonaStatus(int32_t luidLowPart, int32_t luidHighPart)>;

// Dispatches one buffered IOCTL against `stateMachine`.
//
// Buffer contract (mirrors METHOD_BUFFERED): `inputBuffer`/`inputLength`
// describe the caller-supplied request payload; `outputBuffer`/
// `outputLength` describe the caller-supplied (and possibly aliased with
// input, per METHOD_BUFFERED) response buffer. Any buffer shorter than
// the IOCTL's declared struct is rejected with
// JOCHONA_STATUS_BUFFER_TOO_SMALL and NEITHER buffer is read past its
// declared length NOR is outputBuffer written to. An unrecognized
// ioControlCode yields JOCHONA_STATUS_UNKNOWN_IOCTL with bytesWritten=0.
DispatchResult Dispatch(
    uint32_t ioControlCode,
    const void* inputBuffer,
    size_t inputLength,
    void* outputBuffer,
    size_t outputLength,
    SlotStateMachine& stateMachine,
    const SetRenderAdapterLuidFn& setRenderAdapterLuid = {});

} // namespace jochona::protocol
    size_t inputLength,
    void* outputBuffer,
    size_t outputLength,
    SlotStateMachine& stateMachine);

} // namespace jochona::protocol
