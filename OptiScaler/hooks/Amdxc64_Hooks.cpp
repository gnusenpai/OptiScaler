#include "pch.h"
#include "Amdxc64_Hooks.h"

#include <fsr4/FSR4Upgrade.h>
#include <ffx_antilag2_dx12.h>
#include <proxies/KernelBase_Proxy.h>
#include <proxies/Ntdll_Proxy.h>
#include <low_latency/input/input_antilag2.h>
#include <misc/IdentifyGpu.h>

#pragma endregion

struct AmdExtD3DFactory : public IAmdExtD3DFactory
{
    HRESULT STDMETHODCALLTYPE CreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IAmdExtD3DShaderIntrinsics))
        {
            if (Amdxc64Hooks::amdExtD3DShaderIntrinsics == nullptr)
                Amdxc64Hooks::amdExtD3DShaderIntrinsics = new AmdExtD3DShaderIntrinsics();

            *ppvObject = Amdxc64Hooks::amdExtD3DShaderIntrinsics;

            LOG_INFO("Custom IAmdExtD3DShaderIntrinsics queried, returning custom AmdExtD3DShaderIntrinsics");

            return S_OK;
        }
        else if (riid == __uuidof(IAmdExtD3DDevice8))
        {
            if (Amdxc64Hooks::amdExtD3DDevice8 == nullptr)
                Amdxc64Hooks::amdExtD3DDevice8 = new AmdExtD3DDevice8();

            *ppvObject = Amdxc64Hooks::amdExtD3DDevice8;

            LOG_INFO("Custom IAmdExtD3DDevice8 queried, returning custom AmdExtD3DDevice8");

            return S_OK;
        }
        else if (Amdxc64Hooks::o_amdExtD3DFactory)
        {
            return Amdxc64Hooks::o_amdExtD3DFactory->CreateInterface(pOuter, riid, ppvObject);
        }

        return E_NOINTERFACE;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (Amdxc64Hooks::o_amdExtD3DFactory)
            return Amdxc64Hooks::o_amdExtD3DFactory->QueryInterface(riid, ppvObject);

        return E_NOTIMPL;
    }
    ULONG __stdcall AddRef(void) override
    {
        if (Amdxc64Hooks::o_amdExtD3DFactory)
            return Amdxc64Hooks::o_amdExtD3DFactory->AddRef();

        return 0;
    }
    ULONG __stdcall Release(void) override
    {
        if (Amdxc64Hooks::o_amdExtD3DFactory)
        {
            auto result = Amdxc64Hooks::o_amdExtD3DFactory->Release();

            if (result == 0)
                Amdxc64Hooks::o_amdExtD3DFactory = nullptr;

            return result;
        }

        return 0;
    }
};

void Amdxc64Hooks::Init()
{
    if (o_AmdExtD3DCreateInterface != nullptr)
        return;

    LOG_DEBUG("");

    moduleAmdxc64 = KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll");
    if (moduleAmdxc64 == nullptr)
        moduleAmdxc64 = NtdllProxy::LoadLibraryExW_Ldr(L"amdxc64.dll", NULL, 0);

    if (moduleAmdxc64 != nullptr)
    {
        LOG_INFO("amdxc64.dll loaded");
        o_AmdExtD3DCreateInterface = (PFN_AmdExtD3DCreateInterface) KernelBaseProxy::GetProcAddress_()(
            moduleAmdxc64, "AmdExtD3DCreateInterface");

        if (o_AmdExtD3DCreateInterface != nullptr)
        {
            LOG_DEBUG("Hooking AmdExtD3DCreateInterface");
            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());
            DetourAttach(&(PVOID&) o_AmdExtD3DCreateInterface, hkAmdExtD3DCreateInterface);
            DetourTransactionCommit();
        }
    }
    else
    {
        LOG_INFO("Failed to load amdxc64.dll");
    }
}

HRESULT STDMETHODCALLTYPE Amdxc64Hooks::hkAmdExtD3DCreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject)
{
    const bool runFsr4Upgrade = IdentifyGpu::getPrimaryGpu().fsr4Capable;

    // Proton bleeding edge ships amdxc64 that is missing some required functions
    if (runFsr4Upgrade && riid == __uuidof(IAmdExtD3DFactory) && State::Instance().isRunningOnLinux)
    {
        // Required for the custom AmdExtFfxApi, lack of it triggers visual glitches
        if (amdExtD3DFactory == nullptr)
            amdExtD3DFactory = new AmdExtD3DFactory();

        *ppvObject = amdExtD3DFactory;

        LOG_INFO("IAmdExtD3DFactory queried, returning custom AmdExtD3DFactory");

        if (o_AmdExtD3DCreateInterface != nullptr && o_amdExtD3DFactory == nullptr)
            o_AmdExtD3DCreateInterface(pOuter, riid, (void**) &o_amdExtD3DFactory);

        return S_OK;
    }

    else if (runFsr4Upgrade && riid == __uuidof(IAmdExtFfxApi))
    {
        if (amdExtFfxApi == nullptr)
            amdExtFfxApi = new AmdExtFfxApi();

        // Return custom one
        *ppvObject = amdExtFfxApi;

        LOG_INFO("IAmdExtFfxApi queried, returning custom AmdExtFfxApi");

        return S_OK;
    }

#ifdef LOW_LATENCY_INPUTS
    else if (riid == IID_IAmdExtAntiLagApi)
    {
        if (amdExtAntiLagApi == nullptr)
            amdExtAntiLagApi = new AmdExtAntiLagApi();

        // Return custom one
        *ppvObject = amdExtAntiLagApi;

        LOG_INFO("IAmdExtAntiLagApi queried, returning custom AmdExtAntiLagApi");

        return S_OK;

        // if (!disableAl2Kill)
        //{
        //     LOG_INFO("Killing native AL2");
        //     return E_NOINTERFACE;
        // }
    }
#endif

    else if (o_AmdExtD3DCreateInterface != nullptr)
        return o_AmdExtD3DCreateInterface(pOuter, riid, ppvObject);

    return E_NOINTERFACE;
}
