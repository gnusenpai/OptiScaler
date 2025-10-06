#include "FSR4Upgrade.h"

#include <pch.h>
#include <proxies/Dxgi_Proxy.h>
#include <detours/detours.h>
#include <scanner/scanner.h>
#include "FSR4ModelSelection.h"

static HMODULE moduleAmdxc64 = nullptr;
static HMODULE fsr4Module = nullptr;

static AmdExtFfxCapability* amdExtFfxThird = nullptr;
static AmdExtFfxCapability2* amdExtFfxSecond = nullptr;
static AmdExtFfxQuery* amdExtFfxQuery = nullptr;
static AmdExtFfxQuery* o_amdExtFfxQuery = nullptr;
static AmdExtFfxApi* amdExtFfxApi = nullptr;

static PFN_AmdExtD3DCreateInterface o_AmdExtD3DCreateInterface = nullptr;

typedef decltype(&D3DKMTQueryAdapterInfo) PFN_D3DKMTQueryAdapterInfo;
typedef decltype(&D3DKMTEnumAdapters) PFN_D3DKMTEnumAdapters;
typedef decltype(&D3DKMTCloseAdapter) PFN_D3DKMTCloseAdapter;

std::vector<std::filesystem::path> GetDriverStore()
{
    std::vector<std::filesystem::path> result;

    // Load D3DKMT functions dynamically
    bool libraryLoaded = false;
    HMODULE hGdi32 = KernelBaseProxy::GetModuleHandleW_()(L"Gdi32.dll");

    if (hGdi32 == nullptr)
    {
        hGdi32 = NtdllProxy::LoadLibraryExW_Ldr(L"Gdi32.dll", NULL, 0);
        libraryLoaded = hGdi32 != nullptr;
    }

    if (hGdi32 == nullptr)
    {
        LOG_ERROR("Failed to load Gdi32.dll");
        return result;
    }

    do
    {
        auto o_D3DKMTEnumAdapters =
            (PFN_D3DKMTEnumAdapters) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTEnumAdapters");
        auto o_D3DKMTQueryAdapterInfo =
            (PFN_D3DKMTQueryAdapterInfo) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTQueryAdapterInfo");
        auto o_D3DKMTCloseAdapter =
            (PFN_D3DKMTCloseAdapter) KernelBaseProxy::GetProcAddress_()(hGdi32, "D3DKMTCloseAdapter");

        if (o_D3DKMTEnumAdapters == nullptr || o_D3DKMTQueryAdapterInfo == nullptr || o_D3DKMTCloseAdapter == nullptr)
        {
            LOG_ERROR("Failed to resolve D3DKMT functions");
            break;
        }

        D3DKMT_UMDFILENAMEINFO umdFileInfo = {};
        D3DKMT_QUERYADAPTERINFO queryAdapterInfo = {};

        queryAdapterInfo.Type = KMTQAITYPE_UMDRIVERNAME;
        queryAdapterInfo.pPrivateDriverData = &umdFileInfo;
        queryAdapterInfo.PrivateDriverDataSize = sizeof(umdFileInfo);

        D3DKMT_ENUMADAPTERS enumAdapters = {};

        // Query the number of adapters first
        if (o_D3DKMTEnumAdapters(&enumAdapters) != 0)
        {
            LOG_ERROR("Failed to enumerate adapters.");
            break;
        }

        // If there are any adapters, the first one should be in the list
        if (enumAdapters.NumAdapters > 0)
        {
            for (size_t i = 0; i < enumAdapters.NumAdapters; i++)
            {
                D3DKMT_ADAPTERINFO adapter = enumAdapters.Adapters[i];
                queryAdapterInfo.hAdapter = adapter.hAdapter;

                auto hr = o_D3DKMTQueryAdapterInfo(&queryAdapterInfo);

                if (hr != 0)
                    LOG_ERROR("Failed to query adapter info {:X}", hr);
                else
                    result.push_back(std::filesystem::path(umdFileInfo.UmdFileName).parent_path());

                D3DKMT_CLOSEADAPTER closeAdapter = {};
                closeAdapter.hAdapter = adapter.hAdapter;
                auto closeResult = o_D3DKMTCloseAdapter(&closeAdapter);
                if (closeResult != 0)
                    LOG_ERROR("D3DKMTCloseAdapter error: {:X}", closeResult);
            }
        }
        else
        {
            LOG_ERROR("No adapters found.");
            break;
        }

    } while (false);

    if (libraryLoaded)
        NtdllProxy::FreeLibrary_Ldr(hGdi32);

    return result;
}

#pragma endregion

void CheckForGPU()
{
    if (Config::Instance()->Fsr4Update.has_value())
        return;

    // Call init for any case
    DxgiProxy::Init();

    IDXGIFactory* factory = nullptr;
    HRESULT result = DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory);

    if (result != S_OK || factory == nullptr)
        return;

    UINT adapterIndex = 0;
    DXGI_ADAPTER_DESC adapterDesc {};
    IDXGIAdapter* adapter;

    while (factory->EnumAdapters(adapterIndex, &adapter) == S_OK)
    {
        if (adapter == nullptr)
        {
            adapterIndex++;
            continue;
        }

        State::Instance().skipSpoofing = true;
        result = adapter->GetDesc(&adapterDesc);
        State::Instance().skipSpoofing = false;

        if (result == S_OK && adapterDesc.VendorId != VendorId::Microsoft)
        {
            std::wstring szName(adapterDesc.Description);
            std::string descStr = std::format("Adapter: {}, VRAM: {} MB", wstring_to_string(szName),
                                              adapterDesc.DedicatedVideoMemory / (1024 * 1024));
            LOG_INFO("{}", descStr);

            // If GPU is AMD
            if (adapterDesc.VendorId == VendorId::AMD)
            {
                // If GPU Name contains 90XX or GFX12 (Linux) always set it to true
                if (szName.find(L" 90") != std::wstring::npos || szName.find(L" GFX12") != std::wstring::npos)
                    Config::Instance()->Fsr4Update.set_volatile_value(true);
            }
        }
        else
        {
            LOG_DEBUG("Can't get description of adapter: {}", adapterIndex);
        }

        adapter->Release();
        adapter = nullptr;
        adapterIndex++;
    }

    factory->Release();
    factory = nullptr;

    LOG_INFO("Fsr4Update: {}", Config::Instance()->Fsr4Update.value_or_default());
}

struct ffxProviderInterface
{
    uint64_t versionId;
    const char* versionName;
    PVOID canProvide;
    PVOID createContext;
    PVOID destroyContext;
    PVOID configure;
    PVOID query;
    PVOID dispatch;
};

struct ExternalProviderData
{
    uint32_t structVersion = 2;
    uint64_t descType;
    ffxProviderInterface provider;
};

struct AmdExtFfxApi : public IAmdExtFfxApi
{
    PFN_UpdateFfxApiProvider o_UpdateFfxApiProvider = nullptr;

    HRESULT STDMETHODCALLTYPE UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes) override
    {
        LOG_INFO("UpdateFfxApiProvider called");

        // To prevent crashes with amd_fidelityfx_dx12.dll & amd_fidelityfx_framegeneration_dx12.dll combo added this
        // check after ML FG update this should be disabled!
        auto providerData = reinterpret_cast<ExternalProviderData*>(pData);
        if (providerData->descType >= 0x00020001u && providerData->descType <= 0x00030009u)
        {
            LOG_ERROR("Skip update for FG");
            return E_INVALIDARG;
        }

        if (o_UpdateFfxApiProvider == nullptr)
        {
            fsr4Module = NtdllProxy::LoadLibraryExW_Ldr(L"amdxcffx64.dll", NULL, 0);

            if (fsr4Module == nullptr)
            {
                auto storePath = GetDriverStore();

                for (size_t i = 0; i < storePath.size(); i++)
                {
                    if (fsr4Module == nullptr)
                    {
                        auto dllPath = storePath[i] / L"amdxcffx64.dll";
                        LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
                        fsr4Module = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

                        if (fsr4Module != nullptr)
                        {
                            LOG_INFO(L"amdxcffx64 loaded from {}", dllPath.wstring());
                            break;
                        }
                    }
                }
            }
            else
            {
                LOG_INFO("amdxcffx64 loaded from game folder");
            }

            if (fsr4Module == nullptr)
            {
                LOG_ERROR("Failed to load amdxcffx64.dll");
                return E_NOINTERFACE;
            }

            auto sdk2upscalingModule = KernelBaseProxy::GetModuleHandleA_()("amd_fidelityfx_upscaler_dx12.dll");
            constexpr bool unhookOld = false;

            if (sdk2upscalingModule != nullptr)
                FSR4ModelSelection::Hook(sdk2upscalingModule, unhookOld);
            else
                FSR4ModelSelection::Hook(fsr4Module, unhookOld);

            o_UpdateFfxApiProvider =
                (PFN_UpdateFfxApiProvider) KernelBaseProxy::GetProcAddress_()(fsr4Module, "UpdateFfxApiProvider");

            if (o_UpdateFfxApiProvider == nullptr)
            {
                LOG_ERROR("Failed to get UpdateFfxApiProvider");
                return E_NOINTERFACE;
            }
        }

        if (o_UpdateFfxApiProvider != nullptr)
        {
            State::DisableChecks(1);

            auto result = o_UpdateFfxApiProvider(pData, dataSizeInBytes);

            LOG_INFO("UpdateFfxApiProvider called, result: {} ({:X})", result == S_OK ? "Ok" : "Error", (UINT) result);
            State::EnableChecks(1);
            return result;
        }

        return E_NOINTERFACE;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }

    ULONG __stdcall AddRef(void) override { return 0; }

    ULONG __stdcall Release(void) override { return 0; }
};

#define STUB(number)                                                                                                   \
    HRESULT STDMETHODCALLTYPE unknown##number()                                                                        \
    {                                                                                                                  \
        LOG_FUNC();                                                                                                    \
        return S_OK;                                                                                                   \
    }

struct AmdExtFfxCapability2 : public IAmdExtFfxCapability2
{
    STUB(1)
    HRESULT STDMETHODCALLTYPE IsSupported(uint64_t a)
    {
        LOG_TRACE(": {}", a);
        return S_OK;
    }
    STUB(3)

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG __stdcall AddRef(void) override { return 0; }
    ULONG __stdcall Release(void) override { return 0; }
};

struct AmdExtFfxCapability : public IAmdExtFfxCapability
{
    STUB(1)
    STUB(2)
    STUB(3)
    STUB(4)
    STUB(5)
    STUB(6)
    STUB(7)
    STUB(8)
    STUB(9)
    STUB(10)
    STUB(11)
    STUB(12)
    STUB(13)
    HRESULT STDMETHODCALLTYPE CheckWMMASupport(uint64_t* a, uint8_t* data)
    {
        LOG_TRACE(": {}", *a);

        *reinterpret_cast<uint64_t*>(&data[0x00]) = 16;
        *reinterpret_cast<uint64_t*>(&data[0x08]) = 16;
        *reinterpret_cast<uint64_t*>(&data[0x10]) = 16;

        *reinterpret_cast<uint32_t*>(&data[0x18]) = 11;
        *reinterpret_cast<uint32_t*>(&data[0x1C]) = 11;

        *reinterpret_cast<uint32_t*>(&data[0x20]) = 1;
        *reinterpret_cast<uint32_t*>(&data[0x24]) = 1;

        data[0x28] = 0;

        return S_OK;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override { return E_NOTIMPL; }
    ULONG __stdcall AddRef(void) override { return 0; }
    ULONG __stdcall Release(void) override { return 0; }
};

struct AmdExtFfxQuery : public IAmdExtFfxQuery
{
    HRESULT STDMETHODCALLTYPE queryInternal(IUnknown* pOuter, REFIID riid, void** ppvObject) override
    {
        if (riid == __uuidof(IAmdExtFfxCapability2))
        {
            if (amdExtFfxSecond == nullptr)
                amdExtFfxSecond = new AmdExtFfxCapability2();

            *ppvObject = amdExtFfxSecond;

            LOG_INFO("Custom IAmdExtFfxCapability2 queried, returning custom AmdExtFfxCapability2");

            return S_OK;
        }
        else if (riid == __uuidof(IAmdExtFfxCapability))
        {
            if (amdExtFfxThird == nullptr)
                amdExtFfxThird = new AmdExtFfxCapability();

            *ppvObject = amdExtFfxThird;

            LOG_INFO("Custom IAmdExtFfxCapability queried, returning custom AmdExtFfxCapability");

            return S_OK;
        }
        else if (o_amdExtFfxQuery)
        {
            return o_amdExtFfxQuery->queryInternal(pOuter, riid, ppvObject);
        }

        return E_NOINTERFACE;
    }

    HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override
    {
        if (o_amdExtFfxQuery)
            return o_amdExtFfxQuery->QueryInterface(riid, ppvObject);

        return E_NOTIMPL;
    }
    ULONG __stdcall AddRef(void) override
    {
        if (o_amdExtFfxQuery)
            return o_amdExtFfxQuery->AddRef();

        return 0;
    }
    ULONG __stdcall Release(void) override
    {
        if (o_amdExtFfxQuery)
        {
            auto result = o_amdExtFfxQuery->Release();

            if (result == 0)
                o_amdExtFfxQuery = nullptr;

            return result;
        }

        return 0;
    }
};

void InitFSR4Update()
{
    if (Config::Instance()->Fsr4Update.has_value() && !Config::Instance()->Fsr4Update.value())
        return;

    if (o_AmdExtD3DCreateInterface != nullptr)
        return;

    LOG_DEBUG("");

    // For FSR4 Upgrade
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

HMODULE GetFSR4Module() { return fsr4Module; }

HRESULT STDMETHODCALLTYPE hkAmdExtD3DCreateInterface(IUnknown* pOuter, REFIID riid, void** ppvObject)
{
    CheckForGPU();

    if (!Config::Instance()->Fsr4Update.value_or_default() && o_AmdExtD3DCreateInterface != nullptr)
        return o_AmdExtD3DCreateInterface(pOuter, riid, ppvObject);

    // Proton bleeding edge ships amdxc64 that is missing some required functions
    else if (riid == __uuidof(IAmdExtFfxQuery) && State::Instance().isRunningOnLinux)
    {
        // Required for the custom AmdExtFfxApi, lack of it triggers visual glitches
        if (amdExtFfxQuery == nullptr)
            amdExtFfxQuery = new AmdExtFfxQuery();

        *ppvObject = amdExtFfxQuery;

        LOG_INFO("IAmdExtFfxQuery queried, returning custom AmdExtFfxQuery");

        if (o_AmdExtD3DCreateInterface != nullptr && o_amdExtFfxQuery == nullptr)
            o_AmdExtD3DCreateInterface(pOuter, riid, (void**) &o_amdExtFfxQuery);

        return S_OK;
    }

    else if (riid == __uuidof(IAmdExtFfxApi))
    {
        if (amdExtFfxApi == nullptr)
            amdExtFfxApi = new AmdExtFfxApi();

        // Return custom one
        *ppvObject = amdExtFfxApi;

        LOG_INFO("IAmdExtFfxApi queried, returning custom AmdExtFfxApi");

        return S_OK;
    }

    else if (o_AmdExtD3DCreateInterface != nullptr)
        return o_AmdExtD3DCreateInterface(pOuter, riid, ppvObject);

    return E_NOINTERFACE;
}
