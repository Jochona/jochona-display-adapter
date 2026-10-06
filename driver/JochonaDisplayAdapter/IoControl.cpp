/*++

Module Name:

    IoControl.cpp

SPDX-License-Identifier: MIT
Copyright (c) 2026 Jochona project contributors

--*/

#include "IoControl.h"

#include "ProtocolDispatcher.h"
#include "IoControl.tmh"

using jochona::protocol::Dispatch;
using jochona::protocol::DispatchResult;
using jochona::protocol::SetRenderAdapterLuidFn;

namespace Jochona
{

NTSTATUS JochonaStatusToNtStatus(JochonaStatus status)
{
    // Keep byte-for-byte in sync with docs/PROTOCOL.md's status table —
    // Jochona Host depends on this exact mapping via GetLastError() after
    // DeviceIoControl() returns FALSE.
    switch (status)
    {
    case JOCHONA_STATUS_SUCCESS:
        return STATUS_SUCCESS;
    case JOCHONA_STATUS_UNKNOWN_IOCTL:
        return STATUS_INVALID_DEVICE_REQUEST;
    case JOCHONA_STATUS_BUFFER_TOO_SMALL:
        return STATUS_BUFFER_TOO_SMALL;
    case JOCHONA_STATUS_PROTOCOL_VERSION_MISMATCH:
        return STATUS_REVISION_MISMATCH;
    case JOCHONA_STATUS_SLOT_NOT_FOUND:
        return STATUS_NOT_FOUND;
    case JOCHONA_STATUS_SLOT_BUSY:
        return STATUS_DEVICE_BUSY;
    case JOCHONA_STATUS_SLOT_NOT_LEASED:
        return STATUS_INVALID_DEVICE_STATE;
    case JOCHONA_STATUS_LEASE_TOKEN_MISMATCH:
        return STATUS_ACCESS_DENIED;
    case JOCHONA_STATUS_INVALID_PARAMETER:
        return STATUS_INVALID_PARAMETER;
    default:
        return STATUS_INVALID_DEVICE_REQUEST;
    }
}

VOID EvtJochonaDeviceIoControl(
    WDFDEVICE Device,
    WDFREQUEST Request,
    size_t OutputBufferLength,
    size_t InputBufferLength,
    ULONG IoControlCode)
{
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    if (wrapper == nullptr || wrapper->pContext == nullptr)
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_IOCTL,
            "%!FUNC! adapter context missing for IoControlCode=0x%x", IoControlCode);
        WdfRequestCompleteWithInformation(Request, STATUS_DEVICE_NOT_READY, 0);
        return;
    }
    IndirectDeviceContext* context = wrapper->pContext;

    // METHOD_BUFFERED (all Jochona IOCTLs use it): input and output are
    // both retrieved from the same underlying WDF-managed buffer.
    void* inputBuffer = nullptr;
    size_t inputBufferSize = 0;
    if (InputBufferLength > 0)
    {
        NTSTATUS status = WdfRequestRetrieveInputBuffer(Request, InputBufferLength, &inputBuffer, &inputBufferSize);
        if (!NT_SUCCESS(status))
        {
            inputBuffer = nullptr;
            inputBufferSize = 0;
        }
    }

    void* outputBuffer = nullptr;
    size_t outputBufferSize = 0;
    if (OutputBufferLength > 0)
    {
        NTSTATUS status = WdfRequestRetrieveOutputBuffer(Request, OutputBufferLength, &outputBuffer, &outputBufferSize);
        if (!NT_SUCCESS(status))
        {
            outputBuffer = nullptr;
            outputBufferSize = 0;
        }
    }

    SetRenderAdapterLuidFn luidSink = [context](int32_t low, int32_t high) {
        LUID luid;
        luid.LowPart = static_cast<ULONG>(low);
        luid.HighPart = static_cast<LONG>(high);
        return context->SetRenderAdapterLuid(luid);
    };

    const DispatchResult result = Dispatch(
        IoControlCode, inputBuffer, inputBufferSize, outputBuffer, outputBufferSize,
        context->StateMachine(), luidSink);

    // CONFIGURE_SLOT and RELEASE_SLOT mutate the slot's live mode; push it
    // to the OS immediately so the acceptance contract ("lease/configure/
    // release changes actual driver state") holds for real, not just in
    // the state machine.
    if (result.status == JOCHONA_STATUS_SUCCESS &&
        (IoControlCode == IOCTL_JOCHONA_CONFIGURE_SLOT || IoControlCode == IOCTL_JOCHONA_RELEASE_SLOT))
    {
        context->PushCurrentModeToOs();
    }

    const NTSTATUS ntStatus = JochonaStatusToNtStatus(result.status);
    TraceEvents(TRACE_LEVEL_VERBOSE, TRACE_IOCTL,
        "%!FUNC! IoControlCode=0x%x status=%d bytesWritten=%Iu",
        IoControlCode, static_cast<int>(result.status), result.bytesWritten);

    WdfRequestCompleteWithInformation(Request, ntStatus, result.bytesWritten);
    return;
}

} // namespace Jochona
