/*++

Module Name:

    Acl.cpp

SPDX-License-Identifier: MIT
Copyright (c) 2026 Jochona project contributors

--*/

#include "Acl.h"
#include "Trace.h"

namespace Jochona
{

NTSTATUS ApplyDeviceAcl(_In_ PWDFDEVICE_INIT deviceInit)
{
    DECLARE_CONST_UNICODE_STRING(sddl, kJochonaDeviceSddl);
    NTSTATUS status = WdfDeviceInitAssignSDDLString(deviceInit, &sddl);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "%!FUNC! WdfDeviceInitAssignSDDLString failed: %!STATUS!", status);
    }
    return status;
}

} // namespace Jochona
