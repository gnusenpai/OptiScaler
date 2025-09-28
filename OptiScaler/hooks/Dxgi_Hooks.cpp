#include "Dxgi_Hooks.h"

#include <proxies/Dxgi_Proxy.h>
#include <wrapped/wrapped_factory.h>

static DxgiProxy::PFN_CreateDxgiFactory o_CreateDXGIFactory = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory1 o_CreateDXGIFactory1 = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory2 o_CreateDXGIFactory2 = nullptr;

#pragma intrinsic(_ReturnAddress)

static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    State::DisableChecks(97, "dxgi");
#ifndef ENABLE_DEBUG_LAYER_DX12
    auto result = o_CreateDXGIFactory(riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(97);

    if (result != S_OK)
        return result;

    IDXGIFactory* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    real = (IDXGIFactory*) (*ppFactory);
    *ppFactory = (IDXGIFactory*) (new WrappedIDXGIFactory7(real));

    return result;
}

static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    State::DisableChecks(98, "dxgi");
#ifndef ENABLE_DEBUG_LAYER_DX12
    auto result = o_CreateDXGIFactory1(riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, riid, (IDXGIFactory2**) ppFactory);
#endif
    State::EnableChecks(98);

    if (result != S_OK)
        return result;

    IDXGIFactory1* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    real = (IDXGIFactory1*) (*ppFactory);
    *ppFactory = (IDXGIFactory1*) (new WrappedIDXGIFactory7(real));

    return result;
}

static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    State::DisableChecks(99, "dxgi");
#ifndef ENABLE_DEBUG_LAYER_DX12
    auto result = o_CreateDXGIFactory2(Flags, riid, ppFactory);
#else
    auto result = o_CreateDXGIFactory2(Flags | DXGI_CREATE_FACTORY_DEBUG, riid, ppFactory);
#endif
    State::EnableChecks(99);

    if (result != S_OK)
        return result;

    IDXGIFactory2* real = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, *ppFactory, (IUnknown**) &real))
        *ppFactory = real;

    real = (IDXGIFactory2*) (*ppFactory);
    *ppFactory = (IDXGIFactory2*) (new WrappedIDXGIFactory7(real));

    return result;
}

void DxgiHooks::Hook()
{
    if (o_CreateDXGIFactory != nullptr)
        return;

    LOG_DEBUG("");

    o_CreateDXGIFactory = DxgiProxy::Hook_CreateDxgiFactory(hkCreateDXGIFactory);
    o_CreateDXGIFactory1 = DxgiProxy::Hook_CreateDxgiFactory1(hkCreateDXGIFactory1);
    o_CreateDXGIFactory2 = DxgiProxy::Hook_CreateDxgiFactory2(hkCreateDXGIFactory2);
}
