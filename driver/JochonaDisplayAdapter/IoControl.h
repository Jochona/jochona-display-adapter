/*++

Module Name:

    IoControl.h

Abstract:

    WDF boundary for the Jochona Display Adapter protocol. Implements
    EVT_IDD_CX_DEVICE_IO_CONTROL: marshals WDFREQUEST buffers into
    jochona::protocol::Dispatch() calls against the adapter's
    SlotStateMachine, and maps JochonaStatus back to NTSTATUS.

SPDX-License-Identifier: MIT
Copyright (c) 2026 Jochona project contributors

--*/

#pragma once

#include "Driver.h"

namespace Jochona
{

// Registered as IDD_CX_CLIENT_CONFIG::EvtIddCxDeviceIoControl in DeviceAdd.
// This is the ONLY place the driver's 8 IOCTL_JOCHONA_* codes are handled;
// every other IOCTL code is rejected with STATUS_INVALID_DEVICE_REQUEST.
VOID EvtJochonaDeviceIoControl(
    _In_ WDFDEVICE Device,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode);

// Maps a jochona::protocol status to the NTSTATUS surfaced to the caller.
// This table is also documented in docs/PROTOCOL.md; keep both in sync.
NTSTATUS JochonaStatusToNtStatus(JochonaStatus status);

} // namespace Jochona
