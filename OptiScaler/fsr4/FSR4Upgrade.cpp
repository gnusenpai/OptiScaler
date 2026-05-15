#include "pch.h"
#include "FSR4Upgrade.h"

#include <proxies/FfxApi_Proxy.h>

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

HRESULT STDMETHODCALLTYPE AmdExtFfxApi::UpdateFfxApiProvider(void* pData, uint32_t dataSizeInBytes)
{
    auto returnAddress = _ReturnAddress();
    auto callerModule = Util::GetCallerModule(returnAddress);

    if (callerModule == exeModule)
    {
        LOG_WARN("Called from game module, returning E_NOINTERFACE");
        return E_NOINTERFACE;
    }

    auto effectType = FfxApiProxy::GetType(reinterpret_cast<ExternalProviderData*>(pData)->descType);

    auto effect = magic_enum::enum_name(effectType);
    if (effectType >= FFXStructType::Unknown)
        effect = "???";

    if (o_UpdateFfxApiProvider == nullptr)
    {
        FSR4Upgrade::moduleAmdxcffx64 = nullptr;
        HMODULE memModule = nullptr;
        auto optiPath = Config::Instance()->MainDllPath.value();
        Util::LoadProxyLibrary(L"amdxcffx64.dll", L"", optiPath, &memModule, &FSR4Upgrade::moduleAmdxcffx64);

        if (FSR4Upgrade::moduleAmdxcffx64 == nullptr && memModule != nullptr)
            FSR4Upgrade::moduleAmdxcffx64 = memModule;

        if (FSR4Upgrade::moduleAmdxcffx64 == nullptr)
        {
            auto storePath = Util::GetDriverStore();

            for (size_t i = 0; i < storePath.size(); i++)
            {
                if (FSR4Upgrade::moduleAmdxcffx64 == nullptr)
                {
                    auto dllPath = storePath[i] / L"amdxcffx64.dll";
                    LOG_DEBUG("Trying to load: {}", wstring_to_string(dllPath.c_str()));
                    FSR4Upgrade::moduleAmdxcffx64 = NtdllProxy::LoadLibraryExW_Ldr(dllPath.c_str(), NULL, 0);

                    if (FSR4Upgrade::moduleAmdxcffx64 != nullptr)
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

        if (FSR4Upgrade::moduleAmdxcffx64)
        {
            FSR4ModelSelection::Hook(FSR4Upgrade::moduleAmdxcffx64, FSR4Source::DriverDll);
        }
        else
        {
            LOG_WARN("Failed to load amdxcffx64.dll");
            return E_NOINTERFACE;
        }

        o_UpdateFfxApiProvider = (PFN_UpdateFfxApiProvider) KernelBaseProxy::GetProcAddress_()(
            FSR4Upgrade::moduleAmdxcffx64, "UpdateFfxApiProvider");
        o_UpdateFfxApiProviderEx = (PFN_UpdateFfxApiProviderEx) KernelBaseProxy::GetProcAddress_()(
            FSR4Upgrade::moduleAmdxcffx64, "UpdateFfxApiProviderEx");

        if (o_UpdateFfxApiProvider == nullptr)
        {
            LOG_ERROR("Failed to get UpdateFfxApiProvider");
            return E_NOINTERFACE;
        }
    }

    // Result 0x80004002 (E_NOINTERFACE) basically means that amdxcffx64 doesn't have a provider for that effect
    if ((effectType == FFXStructType::FG || effectType == FFXStructType::Upscaling ||
         effectType == FFXStructType::SwapchainDX12) &&
        o_UpdateFfxApiProviderEx != nullptr)
    {
        auto owner = State::GetOwner();
        State::DisableChecks(owner);

        magicData data = { { 0, 1, 1, 0 }, nullptr };
        auto result = o_UpdateFfxApiProviderEx(pData, dataSizeInBytes, &data);

        auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
        spdlog::log(level, "UpdateFfxApiProviderEx for: {}, result: 0x{:X}", effect, (UINT) result);

        State::EnableChecks(owner);
        return result;
    }

    else if (o_UpdateFfxApiProvider != nullptr)
    {
        auto owner = State::GetOwner();
        State::DisableChecks(owner);

        auto result = o_UpdateFfxApiProvider(pData, dataSizeInBytes);

        auto level = SUCCEEDED(result) ? spdlog::level::info : spdlog::level::err;
        spdlog::log(level, "UpdateFfxApiProvider for: {}, result: 0x{:X}", effect, (UINT) result);

        State::EnableChecks(owner);
        return result;
    }

    return E_NOINTERFACE;
}

HRESULT STDMETHODCALLTYPE AmdExtD3DShaderIntrinsics::GetInfo(void* ShaderIntrinsicsInfo)
{
    LOG_FUNC();
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AmdExtD3DShaderIntrinsics::CheckSupport(AmdExtD3DShaderIntrinsicsSupport intrinsic)
{
    LOG_TRACE(": {}", magic_enum::enum_name(intrinsic));
    return S_OK;
}
HRESULT STDMETHODCALLTYPE AmdExtD3DShaderIntrinsics::Enable()
{
    LOG_FUNC();
    return S_OK;
}

HRESULT STDMETHODCALLTYPE AmdExtD3DDevice8::GetWaveMatrixProperties(uint64_t* count,
                                                                    AmdExtWaveMatrixProperties* waveMatrixProperties)
{
    LOG_TRACE(": {}", *count);

    waveMatrixProperties->mSize = 16;
    waveMatrixProperties->nSize = 16;
    waveMatrixProperties->kSize = 16;

    waveMatrixProperties->aType = fp8;
    waveMatrixProperties->bType = fp8;

    waveMatrixProperties->cType = float32;
    waveMatrixProperties->resultType = float32;

    waveMatrixProperties->saturatingAccumulation = false;

    return S_OK;
}
