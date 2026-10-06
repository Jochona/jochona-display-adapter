/*++

Module Name:

    Driver.h

Abstract:

    Jochona Display Adapter — UMDF/IddCx indirect display driver.

    Forked from VirtualDrivers/Virtual-Display-Driver (itself derived from
    Microsoft's Windows-driver-samples IndirectDisplay sample), replacing
    the upstream XML-settings-file / named-pipe PnP-toggle runtime control
    surface with the Jochona Display Adapter protocol v1.0: one stable
    default monitor slot, opaque GUID lease/owner tokens, IOCTL-driven
    configure/release with restore-on-release, a watchdog that reaps
    orphaned leases, and SYSTEM+Administrators-only device access.

    See legacy/upstream-vdd/ for the pre-fork source this was derived
    from, and NOTICE.md for full provenance and license text (MIT AND
    MS-PL).

Environment:

    Windows User-Mode Driver Framework 2 / IddCx

SPDX-License-Identifier: MIT AND MS-PL
Copyright (c) 2024 Virtual Display (upstream VDD authors)
Copyright (c) Microsoft Corporation (IndirectDisplay sample driver)
Copyright (c) 2026 Jochona project contributors

--*/

#pragma once

#define NOMINMAX
#include <windows.h>
#include <bugcodes.h>
#include <wudfwdm.h>
#include <wdf.h>
#include <sddl.h>
#include <IddCx.h>

#include <dxgi1_5.h>
#include <d3d11_2.h>
#include <avrt.h>
#include <wrl.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "Trace.h"
#include "jochona/display_adapter_abi.h"
#include "jochona/display_adapter_guid.h"
#include "SlotStateMachine.h"

// Utility function declarations
std::string WStringToString(const std::wstring& wstr);

// Minimal leveled logger. Mirrors the log surface the upstream driver used
// (vddlog(level, message)) so operational behavior — a rolling log file
// plus OutputDebugString — is preserved, but with no XML/registry-driven
// configuration: verbosity is fixed at driver-build time via JDA_LOG_DEBUG.
void JdaLog(const char* level, const char* message);
void JdaLogf(const char* level, const char* format, ...);

namespace Microsoft
{
    namespace WRL
    {
        namespace Wrappers
        {
            // Adds a wrapper for thread handles to the existing set of WRL handle wrapper classes
            typedef HandleT<HandleTraits::HANDLENullTraits> Thread;
        }
    }
}

namespace Jochona
{

/// <summary>
/// Manages the creation and lifetime of a Direct3D render device pinned to
/// a specific adapter LUID (see IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID).
/// </summary>
struct Direct3DDevice
{
    explicit Direct3DDevice(LUID AdapterLuid);
    Direct3DDevice();
    HRESULT Init();

    LUID AdapterLuid;
    Microsoft::WRL::ComPtr<IDXGIFactory5> DxgiFactory;
    Microsoft::WRL::ComPtr<IDXGIAdapter1> Adapter;
    Microsoft::WRL::ComPtr<ID3D11Device> Device;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> DeviceContext;
};

/// <summary>
/// Manages a thread that consumes buffers from an indirect display swap-chain object.
/// </summary>
class SwapChainProcessor
{
public:
    SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, std::shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent);
    ~SwapChainProcessor();

private:
    static DWORD CALLBACK RunThread(LPVOID Argument);

    void Run();
    void RunCore();

public:
    IDDCX_SWAPCHAIN m_hSwapChain;
    std::shared_ptr<Direct3DDevice> m_Device;
    HANDLE m_hAvailableBufferEvent;
    Microsoft::WRL::Wrappers::Thread m_hThread;
    Microsoft::WRL::Wrappers::Event m_hTerminateEvent;
};

struct LuidComparator
{
    bool operator()(const LUID& a, const LUID& b) const
    {
        if (a.HighPart != b.HighPart)
            return a.HighPart < b.HighPart;
        return a.LowPart < b.LowPart;
    }
};

/// <summary>
/// Owns the IddCx adapter/monitor objects for the single stable default
/// slot and bridges IOCTL_JOCHONA_* requests onto
/// jochona::protocol::SlotStateMachine.
/// </summary>
class IndirectDeviceContext
{
public:
    explicit IndirectDeviceContext(_In_ WDFDEVICE WdfDevice);
    virtual ~IndirectDeviceContext();

    void InitAdapter();
    void FinishInit();

    void CreateDefaultMonitor();

    void AssignSwapChain(IDDCX_MONITOR Monitor, IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent);
    void UnassignSwapChain(IDDCX_MONITOR Monitor);

    // Jochona protocol integration -----------------------------------

    jochona::protocol::SlotStateMachine& StateMachine() { return m_StateMachine; }

    // Pushes the slot's *current* mode (post CONFIGURE_SLOT, or the
    // baseline after RELEASE_SLOT / a watchdog reap) to the OS via
    // IddCxMonitorUpdateModes(2). No-op until the monitor has been
    // created (FinishInit has run).
    void PushCurrentModeToOs();

    // IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID: pins the adapter's render
    // adapter. Returns JOCHONA_STATUS_SUCCESS or
    // JOCHONA_STATUS_INVALID_PARAMETER if no matching adapter exists.
    JochonaStatus SetRenderAdapterLuid(LUID luid);

    // Invoked by the WDF watchdog timer: reaps any orphaned lease and
    // pushes each reaped slot's restored baseline mode to the OS.
    void OnWatchdogTick();

    IDDCX_ADAPTER AdapterHandle() const { return m_Adapter; }

protected:

    WDFDEVICE m_WdfDevice;
    IDDCX_ADAPTER m_Adapter;
    IDDCX_MONITOR m_Monitor;

    std::map<IDDCX_MONITOR, std::unique_ptr<SwapChainProcessor>> m_ProcessingThreads;
    std::mutex m_ProcessingThreadsMutex;

    jochona::protocol::SlotStateMachine m_StateMachine;

    std::mutex m_RenderAdapterMutex;
    std::optional<LUID> m_PinnedRenderAdapterLuid;

    // Whether the adapter negotiated FP16/HDR-capable mode reporting
    // (IddCxMonitorUpdateModes2 / IDDCX_TARGET_MODE2), matching the
    // capability check IddCx exposes for IDDCX_ADAPTER_FLAGS_CAN_PROCESS_FP16.
    bool m_HdrCapable = false;

public:
    static std::vector<BYTE> s_MonitorEdid;

private:
    static std::map<LUID, std::shared_ptr<Direct3DDevice>, LuidComparator> s_DeviceCache;
    static std::mutex s_DeviceCacheMutex;
    static std::shared_ptr<Direct3DDevice> GetOrCreateDevice(LUID RenderAdapter);
    static void CleanupExpiredDevices();
};

// Builds the driver's single baseline EDID (a fork of the upstream
// hardcoded EDID with Jochona's own manufacturer id and monitor name) and
// computes its checksum. Not cached as a translation-unit-scope global so
// unit code outside the driver can call it deterministically.
std::vector<BYTE> BuildBaselineEdid();

} // namespace Jochona

// WDF context wrapper bridging IDDCX_ADAPTER/IDDCX_MONITOR/WDFDEVICE
// objects to the Jochona IndirectDeviceContext instance. Declared here
// (not in Driver.cpp) so every translation unit that needs
// WdfObjectGet_IndirectDeviceContextWrapper() — Driver.cpp, IoControl.cpp —
// sees the same WDF_DECLARE_CONTEXT_TYPE-generated accessor.
struct IndirectDeviceContextWrapper
{
    Jochona::IndirectDeviceContext* pContext;

    void Cleanup()
    {
        delete pContext;
        pContext = nullptr;
    }
};

WDF_DECLARE_CONTEXT_TYPE(IndirectDeviceContextWrapper);
