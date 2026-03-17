#include "pch.h"

#include "proxies/FfxApi_Proxy.h"
#include <unordered_map>

#include "fakenvapi.h"
#include "NvApiTypes.h"
#include "fakenvapi/nvapi_calls.h"
#include "fakenvapi/fakexell.h"
#include "fakenvapi/al2_proxy.h"
#include "fakenvapi/fn_vulkan_hooks.h"

#define nvapi_interface_table nvapi_interface_table_extern
#include <nvapi_interface.h>
#undef nvapi_interface_table

std::unordered_map<NvU32, void*> fakenvapi::idToFuncMapping;

void fakenvapi::init()
{
    LowLatencyCtx::init();
    LowLatencyCtxXell::init();

    auto exe_path = State::Instance().GameExe;
    to_lower_in_place(exe_path);

    // TODO: won't work for late loaded vulkan-1.dll
    auto vulkan_module = GetModuleHandleA("vulkan-1.dll");

    // HACK: Force load vulkan-1 in rtx remix
    // Avoids having to wait for the game to load it
    const bool force_load = exe_path.contains("nvremixbridge.exe");
    if (!vulkan_module && force_load)
    {
        vulkan_module = LoadLibraryA("vulkan-1.dll");
        if (!vulkan_module)
        {
            spdlog::warn("Failed to load vulkan-1.dll");
        }
        else
        {
            spdlog::info("vulkan-1.dll loaded");
        }
    }

    FnVulkanHooks::hook_vulkan(vulkan_module);

    AL2Proxy::hookAntiLag();
}

void fakenvapi::deinit()
{
    LowLatencyCtx::get()->deinit_current_tech();
    LowLatencyCtxXell::get()->deinit_current_tech();
    LowLatencyCtx::shutdown();
    LowLatencyCtxXell::shutdown();
}

// names from: https://github.com/SveSop/nvapi_standalone/blob/master/dlls/nvapi/nvapi.c
static NVAPI_INTERFACE_TABLE additional_interface_table[] = { { "NvAPI_Diag_ReportCallStart", 0x33c7358c },
                                                              { "NvAPI_Diag_ReportCallReturn", 0x593e8644 },
                                                              { "NvAPI_Unknown_1", 0xe9b009b9 },
                                                              { "NvAPI_SK_1", 0x57f7caac },
                                                              { "NvAPI_SK_2", 0x11104158 },
                                                              { "NvAPI_SK_3", 0xe3795199 },
                                                              { "NvAPI_SK_4", 0xdf0dfcdd },
                                                              { "NvAPI_SK_5", 0x932ac8fb } };

static NVAPI_INTERFACE_TABLE fakenvapi_interface_table[] = {
    { "Fake_GetLatency", 0x21372137 },       { "Fake_InformFGState", 0x21382138 },
    { "Fake_InformPresentFG", 0x21392139 },  { "Fake_GetAntiLagCtx", 0x21402140 },
    { "Fake_GetLowLatencyCtx", 0x21412141 }, { "Fake_SetLowLatencyCtx", 0x21422142 }
};

void* __cdecl fakenvapi::queryInterface(NvU32 id)
{
    auto entry = idToFuncMapping.find(id);
    if (entry != idToFuncMapping.end())
        return entry->second;

    constexpr auto original_size = sizeof(nvapi_interface_table_extern) / sizeof(nvapi_interface_table_extern[0]);
    constexpr auto additional_size = sizeof(additional_interface_table) / sizeof(additional_interface_table[0]);
    constexpr auto fakenvapi_size = sizeof(fakenvapi_interface_table) / sizeof(fakenvapi_interface_table[0]);

    constexpr auto total_size = original_size + additional_size + fakenvapi_size;

    struct NVAPI_INTERFACE_TABLE extended_interface_table[total_size] {};
    memcpy(extended_interface_table, nvapi_interface_table_extern,
           sizeof(nvapi_interface_table_extern)); // copy original table

    for (unsigned int i = 0; i < additional_size; i++)
    {
        extended_interface_table[original_size + i] = additional_interface_table[i];
    }

    for (unsigned int i = 0; i < fakenvapi_size; i++)
    {
        extended_interface_table[original_size + additional_size + i].func = fakenvapi_interface_table[i].func;
        extended_interface_table[original_size + additional_size + i].id = fakenvapi_interface_table[i].id;
    }

    auto it = std::find_if(std::begin(extended_interface_table), std::end(extended_interface_table),
                           [id](const auto& item) { return item.id == id; });

    if (it == std::end(extended_interface_table))
    {
        spdlog::debug("NvAPI_QueryInterface (0x{:x}): Unknown interface ID", id);
        return idToFuncMapping.insert({ id, nullptr }).first->second;
    }

    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Initialize)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetInterfaceVersionString)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_EnumNvidiaDisplayHandle)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetLogicalGPUFromPhysicalGPU)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_EnumPhysicalGPUs)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_EnumLogicalGPUs)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetGPUIDfromPhysicalGPU)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetPhysicalGPUFromGPUID)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetPhysicalGPUsFromDisplay)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetPhysicalGPUsFromLogicalGPU)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetErrorMessage)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GetDisplayDriverVersion)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetLogicalGpuInfo)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetConnectedDisplayIds)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_CudaEnumComputeCapableGpus)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetArchInfo)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetPCIIdentifiers)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetFullName)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetGpuCoreCount)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetAllClockFrequencies)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetAdapterIdFromPhysicalGpu)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_GPU_GetPstates20)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DISP_GetDisplayIdByDisplayName)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DISP_GetGDIPrimaryDisplayId)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_SetOutputMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_GetOutputMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_GetHdrCapabilities)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Disp_HdrColorControl)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Mosaic_GetDisplayViewportsByResolution)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetDisplayDriverInfo)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetDriverAndBranchVersion)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetDisplayIdFromGpuAndOutputId)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SYS_GetGpuAndOutputIdFromDisplayId)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetResourceHint)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_GetObjectHandleForResource)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_GetSleepStatus)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_GetLatency)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetSleepMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetLatencyMarker)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_Sleep)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D_SetReflexSync)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_IsNvShaderExtnOpCodeSupported)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_BeginUAVOverlap)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_EndUAVOverlap)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D11_SetDepthBoundsTest)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_GetRaytracingCaps)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_IsNvShaderExtnOpCodeSupported)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_SetNvShaderExtnSlotSpaceLocalThread)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_GetRaytracingAccelerationStructurePrebuildInfoEx)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_BuildRaytracingAccelerationStructureEx)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_NotifyOutOfBandCommandQueue)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_D3D12_SetAsyncFrameMarker)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_InitLowLatencyDevice)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_DestroyLowLatencyDevice)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_GetSleepStatus)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_SetSleepMode)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_Sleep)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_GetLatency)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_SetLatencyMarker)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Vulkan_NotifyOutOfBandVkQueue)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_CreateSession)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_LoadSettings)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_SaveSettings)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_GetBaseProfile)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_GetSetting)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_SetSetting)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_DRS_DestroySession)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Unknown_1)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_1)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_2)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_3)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_4)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_SK_5)
    INSERT_AND_RETURN_WHEN_EQUALS(NvAPI_Unload)
    INSERT_AND_RETURN_WHEN_EQUALS(Fake_GetLatency)
    INSERT_AND_RETURN_WHEN_EQUALS(Fake_InformFGState)
    INSERT_AND_RETURN_WHEN_EQUALS(Fake_InformPresentFG)
    INSERT_AND_RETURN_WHEN_EQUALS(Fake_GetAntiLagCtx)
    INSERT_AND_RETURN_WHEN_EQUALS(Fake_GetLowLatencyCtx)
    INSERT_AND_RETURN_WHEN_EQUALS(Fake_SetLowLatencyCtx)

    spdlog::debug("{}: not implemented, placeholder given", it->func);
    return idToFuncMapping.insert({ id, (void*) placeholder }).first->second;
    // return registry.insert({ id, nullptr }).first->second;
}

// Inform AntiLag 2 when present of interpolated frames starts
void fakenvapi::reportFGPresent(IDXGISwapChain* pSwapChain, bool fg_state, bool frame_interpolated)
{
    if (!isUsingFakenvapi())
        return;

    // Lets fakenvapi log and reset correctly
    nvapi_calls::Fake_InformFGState(fg_state);

    if (fg_state)
    {
        if (State::Instance().activeFgOutput == FGOutput::FSRFG)
        {
            // Starting with FSR 3.1.1 we can provide an AntiLag 2 context to FSR FG
            // and it will call SetFrameGenFrameType for us
            auto static ffxApiVersion = FfxApiProxy::VersionDx12();
            constexpr feature_version requiredVersion = { 3, 1, 1 };
            if (ffxApiVersion >= requiredVersion && updateModeAndContext())
            {
                antilag2_data.enabled = _lowLatencyContext != nullptr && _lowLatencyMode == LowLatencyMode::AntiLag2;
                antilag2_data.context = antilag2_data.enabled ? _lowLatencyContext : nullptr;

                pSwapChain->SetPrivateData(IID_IFfxAntiLag2Data, sizeof(antilag2_data), &antilag2_data);
            }
            else
            {
                // Tell fakenvapi to call SetFrameGenFrameType by itself
                // Reflex frame id might get used in the future
                LOG_TRACE("Fake_InformPresentFG: {}", frame_interpolated);
                nvapi_calls::Fake_InformPresentFG(frame_interpolated, 0);
            }
        }
        else if (State::Instance().activeFgOutput == FGOutput::XeFG)
        {
            if (updateModeAndContext())
            {
                // Tell fakenvapi to call SetFrameGenFrameType by itself
                // Reflex frame id might get used in the future
                LOG_TRACE("Fake_InformPresentFG: {}", frame_interpolated);
                nvapi_calls::Fake_InformPresentFG(frame_interpolated, 0);
            }
        }
    }
    else
    {
        // Remove it or other FG mods won't work with AL2
        pSwapChain->SetPrivateData(IID_IFfxAntiLag2Data, 0, nullptr);
    }
}

bool fakenvapi::updateModeAndContext()
{
    if (!isUsingFakenvapi() && State::Instance().activeFgOutput == FGOutput::XeFG &&
        !Config::Instance()->DontUseFakenvapiForXeLLOnNvidia.value_or_default())
    {
        auto loaded = fakenvapi::loadForNvidia();
    }

    if (!isUsingFakenvapi() && !isUsingFakenvapiOnNvidia())
        return false;

    LOG_FUNC();

    if (nvapi_calls::Fake_GetLowLatencyCtx)
    {
        auto result = nvapi_calls::Fake_GetLowLatencyCtx(&_lowLatencyContext, &_lowLatencyMode);

        if (result != NVAPI_OK)
            LOG_ERROR("Can't get Low Latency context from fakenvapi");

        return result == NVAPI_OK;
    }

    // fallback for older fakenvapi builds
    if (nvapi_calls::Fake_GetAntiLagCtx)
    {
        auto result = nvapi_calls::Fake_GetAntiLagCtx(&_lowLatencyContext);

        if (result != NVAPI_OK)
            _lowLatencyMode = LowLatencyMode::LatencyFlex;
        else
            _lowLatencyMode = LowLatencyMode::AntiLag2;

        return result == NVAPI_OK;
    }

    _lowLatencyContext = nullptr;
    _lowLatencyMode = LowLatencyMode::LatencyFlex;

    return false;
}

bool fakenvapi::setModeAndContext(void* context, LowLatencyMode mode)
{
    if (!isUsingFakenvapi() && State::Instance().activeFgOutput == FGOutput::XeFG &&
        !Config::Instance()->DontUseFakenvapiForXeLLOnNvidia.value_or_default())
    {
        auto loaded = fakenvapi::loadForNvidia();
    }

    if (!isUsingFakenvapi() && !isUsingFakenvapiOnNvidia())
        return false;

    LOG_FUNC();

    if (nvapi_calls::Fake_SetLowLatencyCtx)
    {
        auto result = nvapi_calls::Fake_SetLowLatencyCtx(context, mode);

        if (result != NVAPI_OK)
            LOG_ERROR("Can't set Low Latency context from fakenvapi");

        return result == NVAPI_OK;
    }

    return false;
}

bool fakenvapi::loadForNvidia()
{
    if (!State::Instance().isRunningOnNvidia)
        return false;

    if (_dllForNvidia != nullptr)
        return true;

    _dllForNvidia = NtdllProxy::LoadLibraryExW_Ldr(L"fakenvapi.dll", NULL, 0);

    if (!_dllForNvidia)
        return false;

    auto queryInterface =
        (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(_dllForNvidia, "nvapi_QueryInterface");

    if (queryInterface == nullptr)
    {
        _dllForNvidia = nullptr;
        return false;
    }

    ForNvidia_SetSleepMode = GET_INTERFACE(NvAPI_D3D_SetSleepMode, queryInterface);
    ForNvidia_Sleep = GET_INTERFACE(NvAPI_D3D_Sleep, queryInterface);
    ForNvidia_GetLatency = GET_INTERFACE(NvAPI_D3D_GetLatency, queryInterface);
    ForNvidia_SetLatencyMarker = GET_INTERFACE(NvAPI_D3D_SetLatencyMarker, queryInterface);
    ForNvidia_SetAsyncFrameMarker = GET_INTERFACE(NvAPI_D3D12_SetAsyncFrameMarker, queryInterface);

    if (nvapi_calls::Fake_SetLowLatencyCtx)
    {
        _initedForNvidia = true;
        LOG_INFO("fakenvapi initialized for Nvidia");
        return true;
    }

    LOG_INFO("Failed to initialize fakenvapi for Nvidia");
    return false;
}

// updateModeAndContext needs to be called before that
LowLatencyMode fakenvapi::getCurrentMode() { return _lowLatencyMode; }

// TODO: remove
bool fakenvapi::isUsingFakenvapi() { return true; }

bool fakenvapi::isUsingFakenvapiOnNvidia() { return _initedForNvidia; }
