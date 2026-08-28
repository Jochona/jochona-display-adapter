/*++

Module Name:

    Acl.h

Abstract:

    Device-object access control for the Jochona Display Adapter. The
    device MUST be reachable only by SYSTEM and members of the local
    Administrators group — never "world" read/write, unlike a typical
    PnP class-default ACL. Applied once, in DeviceAdd, before the WDF
    device object is created.

SPDX-License-Identifier: MIT
Copyright (c) 2026 Jochona project contributors

--*/

#pragma once

#include <wdf.h>

namespace Jochona
{

// SDDL restricting the device object to SYSTEM (full control) and the
// built-in Administrators group (full control) only — no access for
// Everyone/Users/Authenticated Users. Equivalent to the WDK's predefined
// SDDL_DEVOBJ_SYS_ALL_ADM_ALL macro (kept spelled out here so the ACL
// intent is auditable without cross-referencing sddl.h):
//   D:P              -- discretionary ACL, protected (not inheritable)
//   (A;;GA;;;SY)     -- Allow, Generic-All, SYSTEM
//   (A;;GA;;;BA)     -- Allow, Generic-All, Built-in Administrators
inline constexpr wchar_t kJochonaDeviceSddl[] =
    L"D:P(A;;GA;;;SY)(A;;GA;;;BA)";

// Applies kJochonaDeviceSddl to `deviceInit` (must be called before
// WdfDeviceCreate). Returns the NTSTATUS from
// WdfDeviceInitAssignSDDLString.
NTSTATUS ApplyDeviceAcl(_In_ PWDFDEVICE_INIT deviceInit);

} // namespace Jochona
