#pragma once

#include <nvapi/NvApiTypes.h>

class InputReflex
{
  public:
    static NvAPI_Status D3D_SetSleepMode(IUnknown* pDev, NV_SET_SLEEP_MODE_PARAMS* pSetSleepModeParams);
    static NvAPI_Status D3D_Sleep(IUnknown* pDev);
    static NvAPI_Status D3D_GetLatency(IUnknown* pDev, NV_LATENCY_RESULT_PARAMS* pGetLatencyParams);
    static NvAPI_Status D3D_SetLatencyMarker(IUnknown* pDev, NV_LATENCY_MARKER_PARAMS* pSetLatencyMarkerParams);
    static NvAPI_Status D3D12_SetAsyncFrameMarker(ID3D12CommandQueue* pCommandQueue,
                                                  NV_ASYNC_FRAME_MARKER_PARAMS* pSetAsyncFrameMarkerParams);
};
