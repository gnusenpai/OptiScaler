#pragma once

#include <ffx_antilag2_dx12.h>
// #include <ffx_antilag2_dx11.h>

struct AmdExtAntiLagApi : public AMD::AntiLag2DX12::IAmdExtAntiLagApi
{
    enum AntiLag2eMode : uint32_t
    {
        AntiLag2Mode_Invalid = 0,
        AntiLag2Mode_On = 1,
        AntiLag2Mode_Off = 2,
    };
    AntiLag2eMode eMode = AntiLag2Mode_Invalid;

    const char* sControlStr;
    uint32_t uiControlStrLength;

    uint32_t maxFPS;

    HRESULT STDMETHODCALLTYPE UpdateAntiLagState(VOID* pData) override;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG STDMETHODCALLTYPE AddRef(void) override { return 0; }
    ULONG STDMETHODCALLTYPE Release(void) override { return 0; }
};

class InputAntiLag2
{
};
