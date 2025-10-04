#include "Dxgi_Hooks.h"

#include "DxgiFactory_Hooks.h"

#include <proxies/Dxgi_Proxy.h>
#include <wrapped/wrapped_factory.h>

#include <DllNames.h>

static DxgiProxy::PFN_CreateDxgiFactory o_CreateDXGIFactory = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory1 o_CreateDXGIFactory1 = nullptr;
static DxgiProxy::PFN_CreateDxgiFactory2 o_CreateDXGIFactory2 = nullptr;

#pragma intrinsic(_ReturnAddress)

inline static HRESULT hkCreateDXGIFactory(REFIID riid, IDXGIFactory** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return o_CreateDXGIFactory(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

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

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
        *ppFactory = (IDXGIFactory*) (new WrappedIDXGIFactory7(real));
    else
        DxgiFactoryHooks::HookToFactory(real);

    return result;
}

inline static HRESULT hkCreateDXGIFactory1(REFIID riid, IDXGIFactory1** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return o_CreateDXGIFactory1(riid, ppFactory);
    }

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

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

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
        *ppFactory = (IDXGIFactory1*) (new WrappedIDXGIFactory7(real));
    else
        DxgiFactoryHooks::HookToFactory(real);

    return result;
}

inline static HRESULT hkCreateDXGIFactory2(UINT Flags, REFIID riid, IDXGIFactory2** ppFactory)
{
    auto caller = Util::WhoIsTheCaller(_ReturnAddress());
    LOG_DEBUG("Caller: {}", caller);

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() && CheckDllName(&caller, &skipDxgiWrappingNames))
    {
        LOG_INFO("Skipping wrapping for: {}", caller);
        return o_CreateDXGIFactory2(Flags, riid, ppFactory);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default() &&
        Util::GetCallerModule(_ReturnAddress()) == slInterposerModule)
    {
        LOG_DEBUG("Delaying 500ms");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

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

    if (Config::Instance()->DxgiFactoryWrapping.value_or_default())
        *ppFactory = (IDXGIFactory2*) (new WrappedIDXGIFactory7(real));
    else
        DxgiFactoryHooks::HookToFactory(real);

    return result;
}

void DxgiHooks::Hook()
{
    // If not spoofing and
    // using no frame generation (or Nukem's) and
    // not using DXGI spoofing we don't need DXGI hooks
    // Probably I forgot something but we can add it later
    if (!Config::Instance()->OverlayMenu.value_or_default() &&
        (Config::Instance()->FGInput.value_or_default() == FGInput::NoFG ||
         Config::Instance()->FGInput.value_or_default() == FGInput::Nukems) &&
        !Config::Instance()->DxgiSpoofing.value_or_default())
    {
        return;
    }

    if (o_CreateDXGIFactory != nullptr)
        return;

    LOG_DEBUG("");

    o_CreateDXGIFactory = DxgiProxy::Hook_CreateDxgiFactory(hkCreateDXGIFactory);
    o_CreateDXGIFactory1 = DxgiProxy::Hook_CreateDxgiFactory1(hkCreateDXGIFactory1);
    o_CreateDXGIFactory2 = DxgiProxy::Hook_CreateDxgiFactory2(hkCreateDXGIFactory2);
}
