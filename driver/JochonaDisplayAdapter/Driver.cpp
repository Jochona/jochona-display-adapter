/*++

Module Name:

    Driver.cpp

Abstract:

    Jochona Display Adapter — UMDF/IddCx indirect display driver
    implementation. See Driver.h for provenance and the protocol
    replaced (upstream XML-settings-file / named-pipe PnP toggle ->
    Jochona Display Adapter protocol v1.0).

SPDX-License-Identifier: MIT AND MS-PL
Copyright (c) 2024 Virtual Display (upstream VDD authors)
Copyright (c) Microsoft Corporation (IndirectDisplay sample driver)
Copyright (c) 2026 Jochona project contributors

--*/

#include "Driver.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <objbase.h>
#include <shlobj.h>
#include <knownfolders.h>

#include "Acl.h"
#include "IoControl.h"
#include "SlotStateMachine.h"

using namespace std;
using namespace Microsoft::WRL;
using jochona::protocol::SlotStateMachine;

//
// ===================== Logging =====================
//
// No XML/registry-driven verbosity toggle: the operational log surface
// (OutputDebugString + a rolling file under %ProgramData%) mirrors the
// upstream driver's vddlog() but carries zero runtime-configurable
// behavior, matching the "replace XML/PnP-toggle runtime control"
// requirement.
//

namespace
{
    std::mutex g_LogMutex;
    std::wstring g_LogDirectory;

    std::wstring GetLogDirectory()
    {
        if (!g_LogDirectory.empty())
        {
            return g_LogDirectory;
        }
        PWSTR programData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &programData)))
        {
            g_LogDirectory = std::wstring(programData) + L"\\JochonaDisplayAdapter\\Logs";
            CoTaskMemFree(programData);
        }
        else
        {
            g_LogDirectory = L"C:\\ProgramData\\JochonaDisplayAdapter\\Logs";
        }
        SHCreateDirectoryExW(nullptr, g_LogDirectory.c_str(), nullptr);
        return g_LogDirectory;
    }
}

void JdaLog(const char* level, const char* message)
{
    char buffer[1024];
    _snprintf_s(buffer, _TRUNCATE, "[JochonaDisplayAdapter][%s] %s\n", level, message);
    OutputDebugStringA(buffer);

    std::lock_guard<std::mutex> lock(g_LogMutex);
    const std::wstring path = GetLogDirectory() + L"\\JochonaDisplayAdapter.log";
    std::ofstream file(path, std::ios::app);
    if (file.is_open())
    {
        file << buffer;
    }
}

void JdaLogf(const char* level, const char* format, ...)
{
    char buffer[1024];
    va_list args;
    va_start(args, format);
    _vsnprintf_s(buffer, _TRUNCATE, format, args);
    va_end(args);
    JdaLog(level, buffer);
}

std::string WStringToString(const std::wstring& wstr)
{
    if (wstr.empty()) return {};
    int sizeNeeded = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(sizeNeeded, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), (int)wstr.size(), &str[0], sizeNeeded, nullptr, nullptr);
    return str;
}

//
// ===================== Baseline EDID =====================
//
// Forked from the upstream VDD hardcoded EDID (same, proven-valid header/
// chromaticity/timing bytes — the detailed timing descriptor at offset 54
// already encodes exactly 1920x1080@60, matching kDefaultBaselineMode)
// with Jochona's own manufacturer id ("JCA"), monitor name, and a
// checksum computed at build time rather than hand-encoded.
//

namespace Jochona
{

std::vector<BYTE> BuildBaselineEdid()
{
    std::vector<BYTE> edid = {
        // Row 0 (0-15): header, manufacturer id "JCA", product code, serial
        0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x28, 0x61, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00,
        // Row 1 (16-31): week/year(2026), EDID version, video input, size, gamma, features, chromaticity
        0x1c, 0x24, 0x01, 0x03, 0x80, 0x32, 0x1f, 0x78, 0x07, 0xee, 0x95, 0xa3, 0x54, 0x4c, 0x99, 0x26,
        // Row 2 (32-47): chromaticity cont'd, established/standard timings
        0x0f, 0x50, 0x54, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
        // Row 3 (48-63): standard timings cont'd, detailed timing #1 (1920x1080@60, 148.5MHz)
        0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x02, 0x3a, 0x80, 0x18, 0x71, 0x38, 0x2d, 0x40, 0x58, 0x2c,
        // Row 4 (64-79): detailed timing #1 cont'd, descriptor #2 (display range limits) start
        0x45, 0x00, 0x63, 0xc8, 0x10, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0xfd, 0x00, 0x17, 0xf0, 0x0f,
        // Row 5 (80-95): descriptor #2 cont'd, descriptor #3 (dummy)
        0xff, 0x37, 0x00, 0x0a, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00,
        // Row 6 (96-111): descriptor #3 cont'd, descriptor #4 (monitor name) tag
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xfc,
        // Row 7 (112-127): "Jochona VDA\n ", extension count=0, checksum placeholder
        0x00, 0x4A, 0x6F, 0x63, 0x68, 0x6F, 0x6E, 0x61, 0x20, 0x56, 0x44, 0x41, 0x0A, 0x20, 0x00, 0x00,
    };

    // EDID checksum: byte 127 makes the sum of all 128 bytes congruent to
    // 0 (mod 256). Computed here rather than hand-encoded so the array
    // above stays trivially editable.
    unsigned sum = 0;
    for (size_t i = 0; i + 1 < edid.size(); ++i)
    {
        sum += edid[i];
    }
    edid.back() = static_cast<BYTE>((256 - (sum % 256)) % 256);
    return edid;
}

std::vector<BYTE> IndirectDeviceContext::s_MonitorEdid = BuildBaselineEdid();
std::map<LUID, std::shared_ptr<Direct3DDevice>, LuidComparator> IndirectDeviceContext::s_DeviceCache;
std::mutex IndirectDeviceContext::s_DeviceCacheMutex;

//
// ===================== Direct3DDevice =====================
//

Direct3DDevice::Direct3DDevice(LUID AdapterLuid) : AdapterLuid(AdapterLuid) {}
Direct3DDevice::Direct3DDevice() : AdapterLuid({}) {}

HRESULT Direct3DDevice::Init()
{
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&DxgiFactory));
    if (FAILED(hr))
    {
        JdaLogf("e", "CreateDXGIFactory2 failed: 0x%08lx", hr);
        return hr;
    }

    hr = DxgiFactory->EnumAdapterByLuid(AdapterLuid, IID_PPV_ARGS(&Adapter));
    if (FAILED(hr))
    {
        JdaLogf("e", "EnumAdapterByLuid failed: 0x%08lx", hr);
        return hr;
    }

    DXGI_ADAPTER_DESC desc;
    Adapter->GetDesc(&desc);
    JdaLogf("i", "Render adapter: %ls (VendorId=0x%04x DeviceId=0x%04x)",
        desc.Description, desc.VendorId, desc.DeviceId);

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL featureLevel;
    hr = D3D11CreateDevice(Adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, featureLevels, ARRAYSIZE(featureLevels),
        D3D11_SDK_VERSION, &Device, &featureLevel, &DeviceContext);
    if (FAILED(hr))
    {
        JdaLogf("e", "D3D11CreateDevice failed: 0x%08lx", hr);
        return hr;
    }
    return S_OK;
}

//
// ===================== SwapChainProcessor =====================
//

SwapChainProcessor::SwapChainProcessor(IDDCX_SWAPCHAIN hSwapChain, shared_ptr<Direct3DDevice> Device, HANDLE NewFrameEvent)
    : m_hSwapChain(hSwapChain), m_Device(Device), m_hAvailableBufferEvent(NewFrameEvent)
{
    m_hTerminateEvent.Attach(CreateEvent(nullptr, FALSE, FALSE, nullptr));
    m_hThread.Attach(CreateThread(nullptr, 0, RunThread, this, 0, nullptr));
    if (!m_hThread.Get())
    {
        JdaLogf("e", "Failed to create swap-chain processing thread: %lu", GetLastError());
    }
}

SwapChainProcessor::~SwapChainProcessor()
{
    if (m_hTerminateEvent.Get())
    {
        SetEvent(m_hTerminateEvent.Get());
    }
    if (m_hThread.Get())
    {
        WaitForSingleObject(m_hThread.Get(), INFINITE);
    }
}

DWORD CALLBACK SwapChainProcessor::RunThread(LPVOID Argument)
{
    reinterpret_cast<SwapChainProcessor*>(Argument)->Run();
    return 0;
}

void SwapChainProcessor::Run()
{
    DWORD avTask = 0;
    HANDLE avTaskHandle = AvSetMmThreadCharacteristicsW(L"Distribution", &avTask);

    RunCore();

    if (m_hSwapChain)
    {
        WdfObjectDelete((WDFOBJECT)m_hSwapChain);
        m_hSwapChain = nullptr;
    }

    if (avTaskHandle)
    {
        AvRevertMmThreadCharacteristics(avTaskHandle);
    }
}

void SwapChainProcessor::RunCore()
{
    DWORD retryDelay = 1;
    constexpr DWORD maxRetryDelay = 100;
    int retryCount = 0;
    constexpr int maxRetries = 5;

    ComPtr<IDXGIDevice> dxgiDevice;
    HRESULT hr = m_Device->Device.As(&dxgiDevice);
    if (FAILED(hr))
    {
        JdaLogf("e", "Failed to get DXGI device interface: 0x%08lx", hr);
        return;
    }

    IDARG_IN_SWAPCHAINSETDEVICE setDevice = {};
    setDevice.pDevice = dxgiDevice.Get();
    hr = IddCxSwapChainSetDevice(m_hSwapChain, &setDevice);
    if (FAILED(hr))
    {
        JdaLogf("e", "IddCxSwapChainSetDevice failed: 0x%08lx", hr);
        return;
    }

    for (;;)
    {
        ComPtr<IDXGIResource> acquiredBuffer;
        IDARG_IN_RELEASEANDACQUIREBUFFER2 bufferInArgs = {};
        bufferInArgs.Size = sizeof(bufferInArgs);
        IDXGIResource* pSurface = nullptr;

        if (IDD_IS_FUNCTION_AVAILABLE(IddCxSwapChainReleaseAndAcquireBuffer2))
        {
            IDARG_OUT_RELEASEANDACQUIREBUFFER2 buffer = {};
            hr = IddCxSwapChainReleaseAndAcquireBuffer2(m_hSwapChain, &bufferInArgs, &buffer);
            pSurface = buffer.MetaData.pSurface;
        }
        else
        {
            IDARG_OUT_RELEASEANDACQUIREBUFFER buffer = {};
            hr = IddCxSwapChainReleaseAndAcquireBuffer(m_hSwapChain, &buffer);
            pSurface = buffer.MetaData.pSurface;
        }

        if (hr == E_PENDING)
        {
            HANDLE waitHandles[2] = {};
            DWORD waitHandleCount = 0;
            if (m_hAvailableBufferEvent != nullptr && m_hAvailableBufferEvent != INVALID_HANDLE_VALUE)
            {
                waitHandles[waitHandleCount++] = m_hAvailableBufferEvent;
            }
            if (m_hTerminateEvent.Get())
            {
                waitHandles[waitHandleCount++] = m_hTerminateEvent.Get();
            }
            if (waitHandleCount == 0)
            {
                break;
            }

            const DWORD waitResult = WaitForMultipleObjects(waitHandleCount, waitHandles, FALSE, INFINITE);
            if (waitResult == WAIT_OBJECT_0)
            {
                continue;
            }
            // Any other result (terminate event, wait failure) ends the loop.
            break;
        }
        else if (SUCCEEDED(hr))
        {
            retryDelay = 1;
            retryCount = 0;
            acquiredBuffer.Attach(pSurface);

            // Frame content processing (GPU copy/encode/etc.) is owned by
            // the Jochona Host encode pipeline downstream of this driver;
            // this loop's job is exclusively to keep the IddCx swap-chain
            // buffer contract satisfied.
            acquiredBuffer.Reset();

            hr = IddCxSwapChainFinishedProcessingFrame(m_hSwapChain);
            if (FAILED(hr))
            {
                break;
            }
        }
        else
        {
            if (hr == DXGI_ERROR_ACCESS_LOST && retryCount < maxRetries)
            {
                Sleep(retryDelay);
                retryDelay = (std::min)(retryDelay * 2, maxRetryDelay);
                ++retryCount;
                continue;
            }
            JdaLogf("e", "Swap-chain buffer acquisition failed: 0x%08lx", hr);
            break;
        }
    }
}

} // namespace Jochona

namespace Jochona
{

//
// ===================== IndirectDeviceContext =====================
//

shared_ptr<Direct3DDevice> IndirectDeviceContext::GetOrCreateDevice(LUID RenderAdapter)
{
    std::lock_guard<std::mutex> lock(s_DeviceCacheMutex);
    auto it = s_DeviceCache.find(RenderAdapter);
    if (it != s_DeviceCache.end() && it->second)
    {
        return it->second;
    }

    auto device = make_shared<Direct3DDevice>(RenderAdapter);
    if (FAILED(device->Init()))
    {
        return nullptr;
    }
    s_DeviceCache[RenderAdapter] = device;
    return device;
}

void IndirectDeviceContext::CleanupExpiredDevices()
{
    std::lock_guard<std::mutex> lock(s_DeviceCacheMutex);
    for (auto it = s_DeviceCache.begin(); it != s_DeviceCache.end();)
    {
        it = it->second ? std::next(it) : s_DeviceCache.erase(it);
    }
}

IndirectDeviceContext::IndirectDeviceContext(_In_ WDFDEVICE WdfDevice)
    : m_WdfDevice(WdfDevice)
    , m_Adapter(nullptr)
    , m_Monitor(nullptr)
    , m_StateMachine(
          JOCHONA_PROTOCOL_V1_MAX_SLOTS,
          jochona::protocol::kDefaultBaselineMode,
          jochona::protocol::kDefaultWatchdogTimeoutMs,
          [] {
              using namespace std::chrono;
              return static_cast<uint64_t>(duration_cast<milliseconds>(
                  steady_clock::now().time_since_epoch()).count());
          },
          [] {
              GUID guid{};
              CoCreateGuid(&guid);
              JochonaGuid128 out{};
              static_assert(sizeof(out.Bytes) == sizeof(GUID), "GUID layout mismatch");
              memcpy(out.Bytes, &guid, sizeof(guid));
              return out;
          })
{
}

IndirectDeviceContext::~IndirectDeviceContext()
{
    std::map<IDDCX_MONITOR, std::unique_ptr<SwapChainProcessor>> processingThreads;
    {
        std::lock_guard<std::mutex> lock(m_ProcessingThreadsMutex);
        processingThreads.swap(m_ProcessingThreads);
    }
}

void IndirectDeviceContext::InitAdapter()
{
    IDDCX_ADAPTER_CAPS adapterCaps = {};
    adapterCaps.Size = sizeof(adapterCaps);

    if (IDD_IS_FUNCTION_AVAILABLE(IddCxSwapChainReleaseAndAcquireBuffer2))
    {
        adapterCaps.Flags = IDDCX_ADAPTER_FLAGS_CAN_PROCESS_FP16;
        m_HdrCapable = true;
    }

    adapterCaps.MaxMonitorsSupported = JOCHONA_PROTOCOL_V1_MAX_SLOTS;
    adapterCaps.EndPointDiagnostics.Size = sizeof(adapterCaps.EndPointDiagnostics);
    adapterCaps.EndPointDiagnostics.GammaSupport = IDDCX_FEATURE_IMPLEMENTATION_NONE;
    adapterCaps.EndPointDiagnostics.TransmissionType = IDDCX_TRANSMISSION_TYPE_WIRED_OTHER;
    adapterCaps.EndPointDiagnostics.pEndPointFriendlyName = L"Jochona Display Adapter";
    adapterCaps.EndPointDiagnostics.pEndPointManufacturerName = L"Jochona";
    adapterCaps.EndPointDiagnostics.pEndPointModelName = L"Jochona Virtual Display Adapter";

    static IDDCX_ENDPOINT_VERSION version = {};
    version.Size = sizeof(version);
    version.MajorVer = JOCHONA_DISPLAY_ADAPTER_PROTOCOL_VERSION_MAJOR;
    adapterCaps.EndPointDiagnostics.pFirmwareVersion = &version;
    adapterCaps.EndPointDiagnostics.pHardwareVersion = &version;

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, IndirectDeviceContextWrapper);

    IDARG_IN_ADAPTER_INIT adapterInit = {};
    adapterInit.WdfDevice = m_WdfDevice;
    adapterInit.pCaps = &adapterCaps;
    adapterInit.ObjectAttributes = &attr;

    IDARG_OUT_ADAPTER_INIT adapterInitOut;
    NTSTATUS status = IddCxAdapterInitAsync(&adapterInit, &adapterInitOut);
    if (NT_SUCCESS(status))
    {
        m_Adapter = adapterInitOut.AdapterObject;
        auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(adapterInitOut.AdapterObject);
        wrapper->pContext = this;
    }
    else
    {
        JdaLogf("e", "IddCxAdapterInitAsync failed: 0x%08lx", status);
    }
}

void IndirectDeviceContext::FinishInit()
{
    CreateDefaultMonitor();
}

void IndirectDeviceContext::CreateDefaultMonitor()
{
    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, IndirectDeviceContextWrapper);

    IDDCX_MONITOR_INFO monitorInfo = {};
    monitorInfo.Size = sizeof(monitorInfo);
    monitorInfo.MonitorType = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
    monitorInfo.ConnectorIndex = 0;
    monitorInfo.MonitorDescription.Size = sizeof(monitorInfo.MonitorDescription);
    monitorInfo.MonitorDescription.Type = IDDCX_MONITOR_DESCRIPTION_TYPE_EDID;
    monitorInfo.MonitorDescription.DataSize = static_cast<UINT>(s_MonitorEdid.size());
    monitorInfo.MonitorDescription.pData = s_MonitorEdid.data();

    // Stable monitor identity across Host restarts: derive the container
    // id deterministically from the fixed interface GUID rather than a
    // fresh random GUID per boot (CoCreateGuid would make Windows treat
    // every driver restart as a "new" monitor).
    static constexpr uint8_t kInterfaceGuidBytes[16] = JOCHONA_DISPLAY_ADAPTER_INTERFACE_GUID_BYTES;
    memcpy(&monitorInfo.MonitorContainerId, kInterfaceGuidBytes, sizeof(GUID));

    IDARG_IN_MONITORCREATE monitorCreate = {};
    monitorCreate.ObjectAttributes = &attr;
    monitorCreate.pMonitorInfo = &monitorInfo;

    IDARG_OUT_MONITORCREATE monitorCreateOut;
    NTSTATUS status = IddCxMonitorCreate(m_Adapter, &monitorCreate, &monitorCreateOut);
    if (!NT_SUCCESS(status))
    {
        JdaLogf("e", "IddCxMonitorCreate failed: 0x%08lx", status);
        return;
    }

    m_Monitor = monitorCreateOut.MonitorObject;
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(monitorCreateOut.MonitorObject);
    wrapper->pContext = this;

    IDARG_OUT_MONITORARRIVAL arrivalOut;
    status = IddCxMonitorArrival(m_Monitor, &arrivalOut);
    if (!NT_SUCCESS(status))
    {
        JdaLogf("e", "IddCxMonitorArrival failed: 0x%08lx", status);
    }
}

void IndirectDeviceContext::AssignSwapChain(IDDCX_MONITOR Monitor, IDDCX_SWAPCHAIN SwapChain, LUID RenderAdapter, HANDLE NewFrameEvent)
{
    static int assignmentCount = 0;
    if (++assignmentCount % 10 == 0)
    {
        CleanupExpiredDevices();
    }

    // A caller-pinned LUID (IOCTL_JOCHONA_SET_RENDER_ADAPTER_LUID) always
    // wins over whatever the OS proposes, so encode/output happens on the
    // adapter Host explicitly chose.
    LUID effectiveLuid = RenderAdapter;
    {
        std::lock_guard<std::mutex> lock(m_RenderAdapterMutex);
        if (m_PinnedRenderAdapterLuid.has_value())
        {
            effectiveLuid = m_PinnedRenderAdapterLuid.value();
        }
    }

    auto device = GetOrCreateDevice(effectiveLuid);
    if (!device)
    {
        JdaLog("e", "Failed to get or create Direct3DDevice; deleting swap chain");
        WdfObjectDelete(SwapChain);
        return;
    }

    auto newProcessor = std::make_unique<SwapChainProcessor>(SwapChain, device, NewFrameEvent);
    std::lock_guard<std::mutex> lock(m_ProcessingThreadsMutex);
    m_ProcessingThreads[Monitor] = std::move(newProcessor);
}

void IndirectDeviceContext::UnassignSwapChain(IDDCX_MONITOR Monitor)
{
    std::unique_ptr<SwapChainProcessor> processorToStop;
    {
        std::lock_guard<std::mutex> lock(m_ProcessingThreadsMutex);
        auto it = m_ProcessingThreads.find(Monitor);
        if (it != m_ProcessingThreads.end())
        {
            processorToStop = std::move(it->second);
            m_ProcessingThreads.erase(it);
        }
    }
}

namespace
{
    IDDCX_BITS_PER_COMPONENT BitsPerComponentFlag(uint32_t bitsPerChannel)
    {
        switch (bitsPerChannel)
        {
        case 12: return IDDCX_BITS_PER_COMPONENT_12;
        case 10: return IDDCX_BITS_PER_COMPONENT_10;
        default: return IDDCX_BITS_PER_COMPONENT_8;
        }
    }

    void FillVideoSignalInfo(DISPLAYCONFIG_VIDEO_SIGNAL_INFO& info, const JochonaSlotMode& mode)
    {
        info.totalSize.cx = info.activeSize.cx = mode.Width;
        info.totalSize.cy = info.activeSize.cy = mode.Height;
        info.AdditionalSignalInfo.vSyncFreqDivider = 1;
        info.AdditionalSignalInfo.videoStandard = 255;
        info.vSyncFreq.Numerator = mode.RefreshNumerator;
        info.vSyncFreq.Denominator = mode.RefreshDenominator;
        info.hSyncFreq.Numerator = mode.RefreshNumerator * mode.Height;
        info.hSyncFreq.Denominator = mode.RefreshDenominator;
        info.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
        info.pixelRate = static_cast<UINT64>(mode.RefreshNumerator) * mode.Width * mode.Height / mode.RefreshDenominator;
    }
}

void IndirectDeviceContext::PushCurrentModeToOs()
{
    if (m_Monitor == nullptr)
    {
        return; // Not created yet; FinishInit's CreateDefaultMonitor will
                // seed the OS with the initial mode via QueryTargetModes.
    }

    const JochonaSlotMode mode = m_StateMachine.CurrentMode(0);
    const IDDCX_BITS_PER_COMPONENT bits = BitsPerComponentFlag(mode.BitsPerChannel);

    if (m_HdrCapable && IDD_IS_FUNCTION_AVAILABLE(IddCxMonitorUpdateModes2))
    {
        IDDCX_TARGET_MODE2 targetMode = {};
        targetMode.Size = sizeof(targetMode);
        FillVideoSignalInfo(targetMode.TargetVideoSignalInfo.targetVideoSignalInfo, mode);
        targetMode.BitsPerComponent.Rgb = bits;

        IDARG_IN_UPDATEMODES2 updateArgs = {};
        updateArgs.Reason = IDDCX_UPDATE_REASON_CONFIGURATION_CONSTRAINTS;
        updateArgs.TargetModeCount = 1;
        updateArgs.pTargetModes = &targetMode;

        NTSTATUS status = IddCxMonitorUpdateModes2(m_Monitor, &updateArgs);
        if (!NT_SUCCESS(status))
        {
            JdaLogf("e", "IddCxMonitorUpdateModes2 failed: 0x%08lx", status);
        }
    }
    else
    {
        IDDCX_TARGET_MODE targetMode = {};
        targetMode.Size = sizeof(targetMode);
        FillVideoSignalInfo(targetMode.TargetVideoSignalInfo.targetVideoSignalInfo, mode);

        IDARG_IN_UPDATEMODES updateArgs = {};
        updateArgs.Reason = IDDCX_UPDATE_REASON_CONFIGURATION_CONSTRAINTS;
        updateArgs.TargetModeCount = 1;
        updateArgs.pTargetModes = &targetMode;

        NTSTATUS status = IddCxMonitorUpdateModes(m_Monitor, &updateArgs);
        if (!NT_SUCCESS(status))
        {
            JdaLogf("e", "IddCxMonitorUpdateModes failed: 0x%08lx", status);
        }
    }

    JdaLogf("i", "Slot 0 mode pushed to OS: %ux%u@%u/%u %u-bit HDR=%d",
        mode.Width, mode.Height, mode.RefreshNumerator, mode.RefreshDenominator,
        mode.BitsPerChannel, mode.HdrEnabled);
}

JochonaStatus IndirectDeviceContext::SetRenderAdapterLuid(LUID luid)
{
    if (luid.LowPart == 0 && luid.HighPart == 0)
    {
        return JOCHONA_STATUS_INVALID_PARAMETER;
    }

    if (m_Adapter != nullptr && IDD_IS_FUNCTION_AVAILABLE(IddCxAdapterSetRenderAdapter))
    {
        // IddCxAdapterSetRenderAdapter returns VOID: the OS applies the
        // preference best-effort (it may fall back to a different adapter,
        // e.g. if the preferred one is PnP-stopped) and reports the actual
        // adapter used later via EvtIddCxMonitorAssignSwapChain.
        IDARG_IN_ADAPTERSETRENDERADAPTER arg{};
        arg.PreferredRenderAdapter = luid;
        IddCxAdapterSetRenderAdapter(m_Adapter, &arg);
    }

    std::lock_guard<std::mutex> lock(m_RenderAdapterMutex);
    m_PinnedRenderAdapterLuid = luid;
    JdaLogf("i", "Render adapter LUID pinned: %ld-%lu", luid.HighPart, luid.LowPart);
    return JOCHONA_STATUS_SUCCESS;
}

void IndirectDeviceContext::OnWatchdogTick()
{
    const auto reaped = m_StateMachine.ReapOrphans();
    if (!reaped.empty())
    {
        JdaLogf("w", "Watchdog reaped %zu orphaned lease(s); restoring baseline mode", reaped.size());
        PushCurrentModeToOs();
    }
}

} // namespace Jochona

//
// ===================== IddCx DDI callbacks =====================
//

using namespace Jochona;

NTSTATUS JochonaAdapterInitFinished(IDDCX_ADAPTER AdapterObject, const IDARG_IN_ADAPTER_INIT_FINISHED* pInArgs)
{
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(AdapterObject);
    if (NT_SUCCESS(pInArgs->AdapterInitStatus))
    {
        wrapper->pContext->FinishInit();
    }
    else
    {
        JdaLogf("e", "Adapter init failed: 0x%08lx", pInArgs->AdapterInitStatus);
    }
    return STATUS_SUCCESS;
}

NTSTATUS JochonaAdapterCommitModes(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES* pInArgs)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(pInArgs);
    return STATUS_SUCCESS;
}

NTSTATUS JochonaAdapterCommitModes2(IDDCX_ADAPTER AdapterObject, const IDARG_IN_COMMITMODES2* pInArgs)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(pInArgs);
    return STATUS_SUCCESS;
}

NTSTATUS JochonaParseMonitorDescription(const IDARG_IN_PARSEMONITORDESCRIPTION* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    // Never invoked: the monitor is always created with an explicit EDID
    // (IDDCX_MONITOR_DESCRIPTION_TYPE_EDID), and on hosts where
    // EvtIddCxParseMonitorDescription2 is available that path is used
    // instead (see JochonaParseMonitorDescription2).
    UNREFERENCED_PARAMETER(pInArgs);
    UNREFERENCED_PARAMETER(pOutArgs);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS JochonaMonitorGetDefaultModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_GETDEFAULTDESCRIPTIONMODES* pInArgs, IDARG_OUT_GETDEFAULTDESCRIPTIONMODES* pOutArgs)
{
    // Never invoked: the monitor always reports an EDID.
    UNREFERENCED_PARAMETER(MonitorObject);
    UNREFERENCED_PARAMETER(pInArgs);
    UNREFERENCED_PARAMETER(pOutArgs);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS JochonaMonitorQueryModes(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(MonitorObject);
    const JochonaSlotMode mode = wrapper->pContext->StateMachine().CurrentMode(0);

    IDDCX_TARGET_MODE targetMode = {};
    targetMode.Size = sizeof(targetMode);
    DISPLAYCONFIG_VIDEO_SIGNAL_INFO& info = targetMode.TargetVideoSignalInfo.targetVideoSignalInfo;
    info.totalSize.cx = info.activeSize.cx = mode.Width;
    info.totalSize.cy = info.activeSize.cy = mode.Height;
    info.AdditionalSignalInfo.vSyncFreqDivider = 1;
    info.AdditionalSignalInfo.videoStandard = 255;
    info.vSyncFreq.Numerator = mode.RefreshNumerator;
    info.vSyncFreq.Denominator = mode.RefreshDenominator;
    info.hSyncFreq.Numerator = mode.RefreshNumerator * mode.Height;
    info.hSyncFreq.Denominator = mode.RefreshDenominator;
    info.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    info.pixelRate = static_cast<UINT64>(mode.RefreshNumerator) * mode.Width * mode.Height / mode.RefreshDenominator;

    pOutArgs->TargetModeBufferOutputCount = 1;
    if (pInArgs->TargetModeBufferInputCount >= 1)
    {
        pInArgs->pTargetModes[0] = targetMode;
    }
    return STATUS_SUCCESS;
}

NTSTATUS JochonaMonitorAssignSwapChain(IDDCX_MONITOR MonitorObject, const IDARG_IN_SETSWAPCHAIN* pInArgs)
{
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(MonitorObject);
    wrapper->pContext->AssignSwapChain(MonitorObject, pInArgs->hSwapChain, pInArgs->RenderAdapterLuid, pInArgs->hNextSurfaceAvailable);
    return STATUS_SUCCESS;
}

NTSTATUS JochonaMonitorUnassignSwapChain(IDDCX_MONITOR MonitorObject)
{
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(MonitorObject);
    wrapper->pContext->UnassignSwapChain(MonitorObject);
    return STATUS_SUCCESS;
}

NTSTATUS JochonaAdapterQueryTargetInfo(IDDCX_ADAPTER AdapterObject, IDARG_IN_QUERYTARGET_INFO* pInArgs, IDARG_OUT_QUERYTARGET_INFO* pOutArgs)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(pInArgs);
    pOutArgs->TargetCaps = IDDCX_TARGET_CAPS_HIGH_COLOR_SPACE | IDDCX_TARGET_CAPS_WIDE_COLOR_SPACE;
    pOutArgs->DitheringSupport.Rgb = IDDCX_BITS_PER_COMPONENT_8 | IDDCX_BITS_PER_COMPONENT_10;
    return STATUS_SUCCESS;
}

NTSTATUS JochonaMonitorSetDefaultHdrMetadata(IDDCX_MONITOR MonitorObject, const IDARG_IN_MONITOR_SET_DEFAULT_HDR_METADATA* pInArgs)
{
    // Jochona v1.0 drives HDR purely through CONFIGURE_SLOT's HdrEnabled
    // flag and BitsPerComponent negotiation; SMPTE ST.2086 static
    // metadata is intentionally left at the OS/Host-negotiated default
    // (no per-panel EDID-derived override, since the monitor is virtual).
    UNREFERENCED_PARAMETER(MonitorObject);
    UNREFERENCED_PARAMETER(pInArgs);
    return STATUS_SUCCESS;
}

NTSTATUS JochonaParseMonitorDescription2(const IDARG_IN_PARSEMONITORDESCRIPTION2* pInArgs, IDARG_OUT_PARSEMONITORDESCRIPTION* pOutArgs)
{
    // Never invoked: the monitor is always created with an explicit EDID.
    UNREFERENCED_PARAMETER(pInArgs);
    UNREFERENCED_PARAMETER(pOutArgs);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS JochonaMonitorQueryTargetModes2(IDDCX_MONITOR MonitorObject, const IDARG_IN_QUERYTARGETMODES2* pInArgs, IDARG_OUT_QUERYTARGETMODES* pOutArgs)
{
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(MonitorObject);
    const JochonaSlotMode mode = wrapper->pContext->StateMachine().CurrentMode(0);

    IDDCX_TARGET_MODE2 targetMode = {};
    targetMode.Size = sizeof(targetMode);
    targetMode.BitsPerComponent.Rgb = BitsPerComponentFlag(mode.BitsPerChannel);
    DISPLAYCONFIG_VIDEO_SIGNAL_INFO& info = targetMode.TargetVideoSignalInfo.targetVideoSignalInfo;
    info.totalSize.cx = info.activeSize.cx = mode.Width;
    info.totalSize.cy = info.activeSize.cy = mode.Height;
    info.AdditionalSignalInfo.vSyncFreqDivider = 1;
    info.AdditionalSignalInfo.videoStandard = 255;
    info.vSyncFreq.Numerator = mode.RefreshNumerator;
    info.vSyncFreq.Denominator = mode.RefreshDenominator;
    info.hSyncFreq.Numerator = mode.RefreshNumerator * mode.Height;
    info.hSyncFreq.Denominator = mode.RefreshDenominator;
    info.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
    info.pixelRate = static_cast<UINT64>(mode.RefreshNumerator) * mode.Width * mode.Height / mode.RefreshDenominator;

    pOutArgs->TargetModeBufferOutputCount = 1;
    if (pInArgs->TargetModeBufferInputCount >= 1)
    {
        pInArgs->pTargetModes[0] = targetMode;
    }
    return STATUS_SUCCESS;
}

NTSTATUS JochonaMonitorSetGammaRamp(IDDCX_MONITOR MonitorObject, const IDARG_IN_SET_GAMMARAMP* pInArgs)
{
    UNREFERENCED_PARAMETER(MonitorObject);
    UNREFERENCED_PARAMETER(pInArgs);
    return STATUS_SUCCESS;
}

//
// ===================== Watchdog timer =====================
//

VOID EvtJochonaWatchdogTimer(_In_ WDFTIMER Timer)
{
    WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(device);
    if (wrapper && wrapper->pContext)
    {
        wrapper->pContext->OnWatchdogTick();
    }
}

//
// ===================== WDF driver plumbing =====================
//

EVT_WDF_DRIVER_DEVICE_ADD JochonaDeviceAdd;
EVT_WDF_DEVICE_D0_ENTRY JochonaDeviceD0Entry;
EVT_WDF_DRIVER_UNLOAD JochonaDriverUnload;

extern "C" DRIVER_INITIALIZE DriverEntry;

_Use_decl_annotations_
NTSTATUS JochonaDeviceAdd(WDFDRIVER Driver, PWDFDEVICE_INIT pDeviceInit)
{
    UNREFERENCED_PARAMETER(Driver);

    // ACL first: SYSTEM + Administrators only, applied before the device
    // object is created so there is no window where a broader default
    // ACL is momentarily in effect.
    NTSTATUS status = ApplyDeviceAcl(pDeviceInit);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);
    pnpPowerCallbacks.EvtDeviceD0Entry = JochonaDeviceD0Entry;
    WdfDeviceInitSetPnpPowerEventCallbacks(pDeviceInit, &pnpPowerCallbacks);

    IDD_CX_CLIENT_CONFIG iddConfig;
    IDD_CX_CLIENT_CONFIG_INIT(&iddConfig);

    // The IOCTL surface: unlike the upstream sample (which left this
    // commented out because it had no runtime control protocol), this is
    // the entire Jochona Display Adapter protocol v1.0 dispatch point.
    iddConfig.EvtIddCxDeviceIoControl = EvtJochonaDeviceIoControl;

    iddConfig.EvtIddCxAdapterInitFinished = JochonaAdapterInitFinished;
    iddConfig.EvtIddCxMonitorAssignSwapChain = JochonaMonitorAssignSwapChain;
    iddConfig.EvtIddCxMonitorUnassignSwapChain = JochonaMonitorUnassignSwapChain;

    if (IDD_IS_FIELD_AVAILABLE(IDD_CX_CLIENT_CONFIG, EvtIddCxAdapterQueryTargetInfo))
    {
        // IddCx 1.10+ (Windows 11 23H2+): the modern HDR/WCG-aware path.
        iddConfig.EvtIddCxAdapterQueryTargetInfo = JochonaAdapterQueryTargetInfo;
        iddConfig.EvtIddCxMonitorSetDefaultHdrMetaData = JochonaMonitorSetDefaultHdrMetadata;
        iddConfig.EvtIddCxParseMonitorDescription2 = JochonaParseMonitorDescription2;
        iddConfig.EvtIddCxMonitorQueryTargetModes2 = JochonaMonitorQueryTargetModes2;
        iddConfig.EvtIddCxAdapterCommitModes2 = JochonaAdapterCommitModes2;
        iddConfig.EvtIddCxMonitorSetGammaRamp = JochonaMonitorSetGammaRamp;
    }
    else
    {
        iddConfig.EvtIddCxParseMonitorDescription = JochonaParseMonitorDescription;
        iddConfig.EvtIddCxMonitorQueryTargetModes = JochonaMonitorQueryModes;
        iddConfig.EvtIddCxAdapterCommitModes = JochonaAdapterCommitModes;
        iddConfig.EvtIddCxMonitorGetDefaultDescriptionModes = JochonaMonitorGetDefaultModes;
    }

    status = IddCxDeviceInitConfig(pDeviceInit, &iddConfig);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    WDF_OBJECT_ATTRIBUTES attr;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attr, IndirectDeviceContextWrapper);
    attr.EvtCleanupCallback = [](WDFOBJECT object) {
        auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(object);
        if (wrapper)
        {
            wrapper->Cleanup();
        }
    };

    WDFDEVICE device = nullptr;
    status = WdfDeviceCreate(&pDeviceInit, &attr, &device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    status = IddCxDeviceInitialize(device);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(device);
    if (!wrapper)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    wrapper->pContext = new IndirectDeviceContext(device);

    // Watchdog: fires every 1 second, reaps any lease that has been
    // silent for longer than kDefaultWatchdogTimeoutMs.
    WDF_TIMER_CONFIG timerConfig;
    WDF_TIMER_CONFIG_INIT_PERIODIC(&timerConfig, EvtJochonaWatchdogTimer, 1000);
    WDF_OBJECT_ATTRIBUTES timerAttr;
    WDF_OBJECT_ATTRIBUTES_INIT(&timerAttr);
    timerAttr.ParentObject = device;
    WDFTIMER watchdogTimer = nullptr;
    status = WdfTimerCreate(&timerConfig, &timerAttr, &watchdogTimer);
    if (NT_SUCCESS(status))
    {
        WdfTimerStart(watchdogTimer, WDF_REL_TIMEOUT_IN_MS(1000));
    }
    else
    {
        JdaLogf("e", "WdfTimerCreate (watchdog) failed: 0x%08lx", status);
    }

    return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS JochonaDeviceD0Entry(WDFDEVICE Device, WDF_POWER_DEVICE_STATE PreviousState)
{
    UNREFERENCED_PARAMETER(PreviousState);
    auto* wrapper = WdfObjectGet_IndirectDeviceContextWrapper(Device);
    if (wrapper && wrapper->pContext)
    {
        wrapper->pContext->InitAdapter();
        return STATUS_SUCCESS;
    }
    return STATUS_INSUFFICIENT_RESOURCES;
}

VOID JochonaDriverUnload(_In_ WDFDRIVER Driver)
{
    UNREFERENCED_PARAMETER(Driver);
}

_Use_decl_annotations_
extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT pDriverObject, PUNICODE_STRING pRegistryPath)
{
    WDF_DRIVER_CONFIG config;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

    WDF_DRIVER_CONFIG_INIT(&config, JochonaDeviceAdd);
    config.EvtDriverUnload = JochonaDriverUnload;

    NTSTATUS status = WdfDriverCreate(pDriverObject, pRegistryPath, &attributes, &config, WDF_NO_HANDLE);
    if (!NT_SUCCESS(status))
    {
        return status;
    }

    JdaLog("i", "Jochona Display Adapter driver starting");
    return STATUS_SUCCESS;
}

extern "C" BOOL WINAPI DllMain(_In_ HINSTANCE hInstance, _In_ DWORD dwReason, _In_opt_ LPVOID lpReserved)
{
    UNREFERENCED_PARAMETER(hInstance);
    UNREFERENCED_PARAMETER(dwReason);
    UNREFERENCED_PARAMETER(lpReserved);
    return TRUE;
}
