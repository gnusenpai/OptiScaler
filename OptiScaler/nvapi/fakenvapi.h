#pragma once

#include <dxgi.h>
#include <d3d12.h>
#include <unordered_map>
#include "fakenvapi/fn_util.h"

class fakenvapi
{
    inline static struct AntiLag2Data
    {
        void* context;
        bool enabled;
    } antilag2_data;

    inline static bool _usingFakenvapiAsMainNvapi = false;
    inline static bool _initedForNvidia = false;
    inline static void* _lowLatencyContext = nullptr;
    inline static LowLatencyMode _lowLatencyMode = LowLatencyMode::LatencyFlex;
    inline static HMODULE _dllForNvidia = nullptr;

    static std::unordered_map<NvU32, void*> idToFuncMapping;

    static NvAPI_Status __cdecl placeholder()
    {
        // return OK();
        // return ERROR(NVAPI_NO_IMPLEMENTATION);
        return NVAPI_NO_IMPLEMENTATION; // no logging
    }

  public:
    inline static const GUID IID_IFfxAntiLag2Data = {
        0x5083ae5b, 0x8070, 0x4fca, { 0x8e, 0xe5, 0x35, 0x82, 0xdd, 0x36, 0x7d, 0x13 }
    };

    inline static decltype(&NvAPI_D3D_SetSleepMode) ForNvidia_SetSleepMode = nullptr;
    inline static decltype(&NvAPI_D3D_Sleep) ForNvidia_Sleep = nullptr;
    inline static decltype(&NvAPI_D3D_GetLatency) ForNvidia_GetLatency = nullptr;
    inline static decltype(&NvAPI_D3D_SetLatencyMarker) ForNvidia_SetLatencyMarker = nullptr;
    inline static decltype(&NvAPI_D3D12_SetAsyncFrameMarker) ForNvidia_SetAsyncFrameMarker = nullptr;

    static void init();
    static void deinit();
    static void* queryInterface(NvU32 id);
    static void reportFGPresent(IDXGISwapChain* pSwapChain, bool fg_state, bool frame_interpolated);
    static bool updateModeAndContext();
    static bool setModeAndContext(void* context, LowLatencyMode mode);
    static bool loadForNvidia();
    static LowLatencyMode getCurrentMode();
    static bool isUsingAsMainNvapi();
    static void setUsingAsMainNvapi(bool usingAsMain);
    static bool isUsingFakenvapiOnNvidia();
};
