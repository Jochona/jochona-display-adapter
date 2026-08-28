/*
 * Jochona Display Adapter ABI — protocol version 1.0
 *
 * Canonical, C-compatible definition of the IOCTL surface exposed by the
 * Jochona Display Adapter UMDF driver. This header has NO dependency on
 * windows.h, wdf.h, or any Windows SDK/WDK headers so that it can be
 * consumed unmodified by non-Windows build environments (protocol/state
 * machine unit tests) as well as by the driver and by Jochona Host.
 *
 * Per the Jochona repository-boundary contract, Host MAY duplicate this
 * header verbatim into its own tree provided the copy:
 *   (a) keeps this notice and the SPDX/provenance header intact, and
 *   (b) keeps JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR/MINOR and
 *       JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID byte-for-byte identical to
 *       this copy. Host has no compile-time or link-time dependency on the
 *       display-adapter repository beyond this header.
 *
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2024 Virtual Display (upstream VDD authors)
 * Copyright (c) 2026 Jochona project contributors
 *
 * Wire format: every IOCTL uses METHOD_BUFFERED semantics — input and
 * output are flat, fixed-size, `#pragma pack(1)` structures with no
 * embedded pointers. All multi-byte integers are little-endian (native
 * x64/ARM64 byte order). Callers MUST supply buffers of AT LEAST the
 * declared struct size; the driver rejects shorter buffers with
 * JOCHONA_STATUS_BUFFER_TOO_SMALL (mapped to STATUS_BUFFER_TOO_SMALL by
 * the driver's WDF boundary) and MUST NOT read or write past the supplied
 * buffer length.
 */
#ifndef JOCHONA_DISPLAY_ADAPTER_ABI_H
#define JOCHONA_DISPLAY_ADAPTER_ABI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Protocol version and device interface identity                     */
/* ------------------------------------------------------------------ */

#define JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR 1
#define JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MINOR 0

/*
 * Device interface GUID b98f1cbd-eaec-5d98-82ab-e899d2643fa1, expressed as
 * its canonical 16-byte binary encoding (Data1 LE32, Data2 LE16, Data3
 * LE16, Data4 8 raw bytes) so it can be compared without a GUID type.
 * Windows-side code should use JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_DEFINE
 * from display_adapter_guid.h to get a real `GUID`/`DEFINE_GUID` symbol
 * instead of hand-decoding these bytes.
 */
#define JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_BYTES \
    { 0xb9, 0x8f, 0x1c, 0xbd, 0xea, 0xec, 0x5d, 0x98, \
      0x82, 0xab, 0xe8, 0x99, 0xd2, 0x64, 0x3f, 0xa1 }

#define JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_STRING \
    "b98f1cbd-eaec-5d98-82ab-e899d2643fa1"

/* Number of logical monitor slots addressable by protocol v1.0. The
 * default pool capacity is 1 (one stable default monitor slot); this
 * constant bounds the fixed-size array in JOCHONA_ENUMERATE_SLOTS_OUT.
 * A future protocol MAY raise this via a major/minor version bump; v1.0
 * wire format is frozen at 1. */
#define JOCHONA_PROTOCOL_V1_MAX_SLOTS 1

/* ------------------------------------------------------------------ */
/* IOCTL codes                                                         */
/* ------------------------------------------------------------------ */

/* Mirrors of the winioctl.h CTL_CODE building blocks, redefined here so
 * this header stays windows.h-free. Values match the Windows SDK
 * definitions exactly (FILE_DEVICE_UNKNOWN-derived custom device type,
 * METHOD_BUFFERED, FILE_ANY_ACCESS) and are byte-identical to what
 * <winioctl.h>'s CTL_CODE macro would produce. Device access is
 * restricted at the WDF device-object ACL layer (SYSTEM + Administrators
 * only, see docs/PROTOCOL.md), not via the IOCTL access bits.
 */
#define JOCHONA_DEVICE_TYPE       0x8000u /* FILE_DEVICE_* user range starts at 0x8000 */
#define JOCHONA_METHOD_BUFFERED   0u
#define JOCHONA_FILE_ANY_ACCESS   0u

#define JOCHONA_CTL_CODE(function) \
    ( (JOCHONA_DEVICE_TYPE) << 16 | (JOCHONA_FILE_ANY_ACCESS) << 14 | \
      ((function) & 0x0FFFu) << 2 | (JOCHONA_METHOD_BUFFERED) )

#define IOCTL_JOCHONA_GET_PROTOCOL_VERSION     JOCHONA_CTL_CODE(0x800)
#define IOCTL_JOCHONA_ENUMERATE_SLOTS          JOCHONA_CTL_CODE(0x801)
#define IOCTL_JOCHONA_LEASE_SLOT               JOCHONA_CTL_CODE(0x802)
#define IOCTL_JOCHONA_CONFIGURE_SLOT           JOCHONA_CTL_CODE(0x803)
#define IOCTL_JOCHONA_RELEASE_SLOT             JOCHONA_CTL_CODE(0x804)
#define IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID  JOCHONA_CTL_CODE(0x805)
#define IOCTL_JOCHONA_GET_WATCHDOG             JOCHONA_CTL_CODE(0x806)
#define IOCTL_JOCHONA_WATCHDOG_PING            JOCHONA_CTL_CODE(0x807)

/* ------------------------------------------------------------------ */
/* Status codes returned in-band by the protocol dispatcher            */
/* ------------------------------------------------------------------ */

typedef enum JochonaStatus {
    JOCHONA_STATUS_SUCCESS = 0,
    JOCHONA_STATUS_UNKNOWN_IOCTL = 1,
    JOCHONA_STATUS_BUFFER_TOO_SMALL = 2,
    JOCHONA_STATUS_PROTOCOL_VERSION_MISMATCH = 3,
    JOCHONA_STATUS_SLOT_NOT_FOUND = 4,
    JOCHONA_STATUS_SLOT_BUSY = 5,
    JOCHONA_STATUS_SLOT_NOT_LEASED = 6,
    JOCHONA_STATUS_LEASE_TOKEN_MISMATCH = 7,
    JOCHONA_STATUS_INVALID_PARAMETER = 8,
} JochonaStatus;

/* ------------------------------------------------------------------ */
/* Common wire types                                                   */
/* ------------------------------------------------------------------ */

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define JOCHONA_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define JOCHONA_PACKED __attribute__((packed))
#else
#define JOCHONA_PACKED
#endif

/* Opaque 128-bit identifier. Used for both lease-owner ids (supplied by
 * the caller, e.g. Jochona Host's own process/session identity) and
 * lease tokens (generated by the driver on LEASE_SLOT). Byte layout
 * matches the standard Windows GUID binary encoding so Windows callers
 * may reinterpret_cast<GUID*> a JochonaGuid128 freely, but no GUID type
 * is required to use this header. */
typedef struct JOCHONA_PACKED JochonaGuid128 {
    uint8_t Bytes[16];
} JochonaGuid128;

typedef struct JOCHONA_PACKED JochonaProtocolVersion {
    uint16_t Major;
    uint16_t Minor;
} JochonaProtocolVersion;

typedef enum JochonaSlotState {
    JochonaSlotStateFree = 0,
    JochonaSlotStateLeased = 1,
    JochonaSlotStateConfigured = 2,
} JochonaSlotState;

typedef struct JOCHONA_PACKED JochonaSlotMode {
    uint32_t Width;
    uint32_t Height;
    uint32_t RefreshNumerator;
    uint32_t RefreshDenominator;
    uint32_t BitsPerChannel;
    uint8_t  HdrEnabled;
    uint8_t  Reserved[3];
} JochonaSlotMode;

typedef struct JOCHONA_PACKED JochonaSlotInfo {
    uint32_t         SlotId;
    JochonaSlotState State;
    JochonaGuid128   OwnerId;    /* zero when State == Free */
    JochonaGuid128   LeaseToken; /* zero when State == Free */
    JochonaSlotMode  Mode;       /* baseline mode when Free, current mode otherwise */
} JochonaSlotInfo;

/* ------------------------------------------------------------------ */
/* GET_PROTOCOL_VERSION — no input; output identifies protocol + GUID  */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaGetProtocolVersionOut {
    JochonaProtocolVersion Version;
    JochonaGuid128          InterfaceGuid;
} JochonaGetProtocolVersionOut;

/* ------------------------------------------------------------------ */
/* ENUMERATE_SLOTS                                                     */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaEnumerateSlotsIn {
    JochonaProtocolVersion RequestedVersion;
} JochonaEnumerateSlotsIn;

typedef struct JOCHONA_PACKED JochonaEnumerateSlotsOut {
    uint32_t        SlotCount;
    JochonaSlotInfo Slots[JOCHONA_PROTOCOL_V1_MAX_SLOTS];
} JochonaEnumerateSlotsOut;

/* ------------------------------------------------------------------ */
/* LEASE_SLOT                                                          */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaLeaseSlotIn {
    JochonaProtocolVersion RequestedVersion;
    uint32_t               SlotId;
    JochonaGuid128          OwnerId;
} JochonaLeaseSlotIn;

typedef struct JOCHONA_PACKED JochonaLeaseSlotOut {
    JochonaGuid128 LeaseToken;
} JochonaLeaseSlotOut;

/* ------------------------------------------------------------------ */
/* CONFIGURE_SLOT                                                      */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaConfigureSlotIn {
    JochonaProtocolVersion RequestedVersion;
    uint32_t               SlotId;
    JochonaGuid128          LeaseToken;
    JochonaSlotMode         Mode;
} JochonaConfigureSlotIn;

/* No output payload beyond the JochonaStatus; STATUS_SUCCESS confirms
 * the mode is now live. */

/* ------------------------------------------------------------------ */
/* RELEASE_SLOT                                                        */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaReleaseSlotIn {
    JochonaProtocolVersion RequestedVersion;
    uint32_t               SlotId;
    JochonaGuid128          LeaseToken;
} JochonaReleaseSlotIn;

/* ------------------------------------------------------------------ */
/* SET_RENDER_ADAPTER_LUID                                             */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaSetRenderAdapterLuidIn {
    JochonaProtocolVersion RequestedVersion;
    int32_t                 LuidLowPart;  /* LUID.LowPart is unsigned on the
                                              Windows side; carried as a raw
                                              32-bit pattern here. */
    int32_t                 LuidHighPart; /* LUID.HighPart is signed LONG. */
} JochonaSetRenderAdapterLuidIn;

/* ------------------------------------------------------------------ */
/* GET_WATCHDOG                                                        */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaGetWatchdogIn {
    JochonaProtocolVersion RequestedVersion;
    uint32_t               SlotId;
} JochonaGetWatchdogIn;

typedef struct JOCHONA_PACKED JochonaGetWatchdogOut {
    uint32_t TimeoutMilliseconds;
    uint32_t MillisecondsSinceLastPing; /* UINT32_MAX when LeaseActive == 0 */
    uint8_t  LeaseActive;
    uint8_t  Reserved[3];
} JochonaGetWatchdogOut;

/* ------------------------------------------------------------------ */
/* WATCHDOG_PING                                                       */
/* ------------------------------------------------------------------ */

typedef struct JOCHONA_PACKED JochonaWatchdogPingIn {
    JochonaProtocolVersion RequestedVersion;
    uint32_t               SlotId;
    JochonaGuid128          LeaseToken;
} JochonaWatchdogPingIn;

#if defined(_MSC_VER)
#pragma pack(pop)
#endif
#undef JOCHONA_PACKED

#ifdef __cplusplus
}
#endif

#endif /* JOCHONA_DISPLAY_ADAPTER_ABI_H */
