/*++

Module Name:

    Acl.cpp

SPDX-License-Identifier: MIT
Copyright (c) 2026 Jochona project contributors

--*/

#include "Acl.h"
#include "Trace.h"
#include "Acl.tmh"

namespace Jochona
{

NTSTATUS ApplyDeviceAcl(_In_ PWDFDEVICE_INIT deviceInit)
{
    // DECLARE_CONST_UNICODE_STRING expects an inline string literal (it
    // stringizes/sizeofs its argument at the macro-expansion site); it
    // cannot take a named array like kJochonaDeviceSddl. Build the
    // UNICODE_STRING from the named constant with RtlInitUnicodeString
    // instead, so the SDDL text stays declared once, in Acl.h, where the
    // audit comment lives.
    UNICODE_STRING sddl;
    RtlInitUnicodeString(&sddl, kJochonaDeviceSddl);
    NTSTATUS status = WdfDeviceInitAssignSDDLString(deviceInit, &sddl);
    if (!NT_SUCCESS(status))
    {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE,
            "%!FUNC! WdfDeviceInitAssignSDDLString failed: %!STATUS!", status);
    }
    return status;
}

} // namespace Jochona
