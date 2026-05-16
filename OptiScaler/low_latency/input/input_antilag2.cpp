#include "pch.h"
#include "input_antilag2.h"

HRESULT STDMETHODCALLTYPE AmdExtAntiLagApi::UpdateAntiLagState(VOID* pData)
{
    while (!IsDebuggerPresent())
        Sleep(100);

    if (!pData)
    {
        // TODO: send sleep
        return S_OK;
    }

    auto ver1Struct = (AMD::AntiLag2DX12::APIData_v1*) pData;
    AMD::AntiLag2DX12::APIData_v2* ver2Struct = nullptr;

    const uint32_t structVersion = ver1Struct->uiVersion;

    if (structVersion < 1 || structVersion > 2)
        return E_INVALIDARG;

    if (structVersion == 1)
    {
        if (ver1Struct->uiSize != sizeof(AMD::AntiLag2DX12::APIData_v1))
        {
            LOG_ERROR("Wrong v1 struct size");
            return E_INVALIDARG;
        }

        // No validation
        eMode = (AntiLag2eMode) ver1Struct->eMode;
        sControlStr = ver1Struct->sControlStr;
        uiControlStrLength = ver1Struct->uiControlStrLength;
        maxFPS = ver1Struct->maxFPS;

        // TODO: call some update

        return S_OK;
    }
    else if (structVersion == 2)
    {
        ver1Struct = nullptr;
        ver2Struct = (AMD::AntiLag2DX12::APIData_v2*) pData;

        if (ver2Struct->uiSize != sizeof(AMD::AntiLag2DX12::APIData_v2))
        {
            LOG_ERROR("Wrong v2 struct size");
            return E_INVALIDARG;
        }

        const uint64_t frameId = ver2Struct->iiFrameIdx; // Usually not used

        if (ver2Struct->flags.signalEndOfFrameIdx == 1)
        {
            // MarkEndOfFrameRendering
            // TODO: fakenvapi calls it on PRESENT_START
        }
        else if (ver2Struct->flags.signalFgFrameType == 1)
        {
            // SetFrameGenFrameType
            // TODO: fakenvapi calls it on OUT_OF_BAND_PRESENT_START
            const bool fakeFrame = ver2Struct->flags.isInterpolatedFrame == 1;
        }

        return S_OK;
    }

    return E_INVALIDARG;
}
