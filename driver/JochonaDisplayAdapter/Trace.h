/*++

Module Name:

    Trace.h

Abstract:

    WPP tracing definitions for the Jochona Display Adapter UMDF driver.
    Adapted from the upstream Microsoft IndirectDisplay sample driver's
    Trace.h (see legacy/upstream-vdd for the pre-fork source) with a
    project-specific tracing GUID and provider name.

Environment:

    Windows User-Mode Driver Framework 2

SPDX-License-Identifier: MIT AND MS-PL
Copyright (c) 2024 Virtual Display (upstream VDD authors)
Copyright (c) Microsoft Corporation (WPP tracing pattern, IddSampleDriver)
Copyright (c) 2026 Jochona project contributors

--*/

#pragma once

//
// Tracing GUID - 1d1b843f-0e2e-42cc-9176-6e4060a3ba66 (Jochona-specific;
// distinct from the upstream VDD/IddSampleDriver tracing GUID so both can
// coexist on a machine that also has the legacy driver installed).
//

#define WPP_CONTROL_GUIDS                                              \
    WPP_DEFINE_CONTROL_GUID(                                           \
        JochonaDisplayAdapterTraceGuid, (1d1b843f,0e2e,42cc,9176,6e4060a3ba66), \
                                                                         \
        WPP_DEFINE_BIT(JOCHONA_ALL_INFO)                                \
        WPP_DEFINE_BIT(TRACE_DRIVER)                                    \
        WPP_DEFINE_BIT(TRACE_DEVICE)                                    \
        WPP_DEFINE_BIT(TRACE_IOCTL)                                     \
        WPP_DEFINE_BIT(TRACE_WATCHDOG)                                  \
        )

#define WPP_FLAG_LEVEL_LOGGER(flag, level)                             \
    WPP_LEVEL_LOGGER(flag)

#define WPP_FLAG_LEVEL_ENABLED(flag, level)                            \
    (WPP_LEVEL_ENABLED(flag) &&                                        \
     WPP_CONTROL(WPP_BIT_ ## flag).Level >= level)

#define WPP_LEVEL_FLAGS_LOGGER(lvl,flags)                              \
           WPP_LEVEL_LOGGER(flags)

#define WPP_LEVEL_FLAGS_ENABLED(lvl, flags)                            \
           (WPP_LEVEL_ENABLED(flags) && WPP_CONTROL(WPP_BIT_ ## flags).Level >= lvl)

//
// This comment block is scanned by the trace preprocessor to define our
// Trace function.
//
// begin_wpp config
// FUNC Trace{FLAG=JOCHONA_ALL_INFO}(LEVEL, MSG, ...);
// FUNC TraceEvents(LEVEL, FLAGS, MSG, ...);
// end_wpp

#if UMDF_VERSION_MAJOR == 2 && UMDF_VERSION_MINOR == 0
#define JOCHONA_TRACING_ID L"Jochona\\UMDF2.0\\JochonaDisplayAdapter V1.0"
#endif
