/*
 * Jochona Display Adapter — Windows device interface GUID.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jochona project contributors
 *
 * Separated from display_adapter_abi.h so that the wire-protocol header
 * stays free of any Windows SDK dependency. Include this header only from
 * Windows-side code (driver, Host) that links against a `GUID` type.
 *
 * This is the SAME device interface GUID advertised by the driver's INF
 * (see driver/JochonaDisplayAdapter/JochonaDisplayAdapter.inf) and MUST
 * stay byte-identical to JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_BYTES in
 * display_adapter_abi.h. Host may duplicate this header verbatim with
 * provenance per the Jochona repository-boundary contract.
 */
#ifndef JOCHONA_DISPLAY_ADAPTER_GUID_H
#define JOCHONA_DISPLAY_ADAPTER_GUID_H

#include <initguid.h>
#include <guiddef.h>

/* {b98f1cbd-eaec-5d98-82ab-e899d2643fa1} */
DEFINE_GUID(GUID_DEVINTERFACE_JOCHONA_DISPLAY_ADAPTER,
    0xb98f1cbd, 0xeaec, 0x5d98, 0x82, 0xab, 0xe8, 0x99, 0xd2, 0x64, 0x3f, 0xa1);

#endif /* JOCHONA_DISPLAY_ADAPTER_GUID_H */
