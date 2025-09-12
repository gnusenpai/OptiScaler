#pragma once

#include <pch.h>

#include <Util.h>
#include <State.h>
#include <Config.h>
#include <DllNames.h>

#include <proxies/Ntdll_Proxy.h>
#include <proxies/NVNGX_Proxy.h>
#include <proxies/XeSS_Proxy.h>
#include <proxies/FfxApi_Proxy.h>
#include <proxies/Dxgi_Proxy.h>
#include <proxies/D3D12_Proxy.h>

#include <inputs/FSR2_Dx12.h>
#include <inputs/FSR3_Dx12.h>
#include <inputs/FfxApiExe_Dx12.h>

#include <spoofing/Dxgi_Spoofing.h>
#include <spoofing/Vulkan_Spoofing.h>

#include <hooks/HooksDx.h>
#include <hooks/HooksVk.h>
#include <hooks/Gdi32_Hooks.h>
#include <hooks/Streamline_Hooks.h>

#include <cwctype>

#pragma intrinsic(_ReturnAddress)

class NtdllHooks
{
  private:
    inline static NtdllProxy::PFN_NtLoadDll o_NtLoadDll = nullptr;
    inline static NtdllProxy::PFN_LdrLoadDll o_LdrLoadDll = nullptr;
    inline static NtdllProxy::PFN_LdrUnloadDll o_LdrUnloadDll = nullptr;

    inline static bool _overlayMethodsCalled = false;

    static HMODULE LoadLibraryCheckW(std::wstring lcaseLibName, LPCWSTR lpLibFullPath)
    {
        auto lcaseLibNameA = wstring_to_string(lcaseLibName);
        LOG_TRACE("{}", lcaseLibNameA);

        // C:\\Path\\like\\this.dll
        auto normalizedPath = std::filesystem::path(lcaseLibName).lexically_normal().string();

        // If Opti is not loading as nvngx.dll
        if (!State::Instance().isWorkingAsNvngx)
        {
            // exe path
            auto exePath = Util::ExePath().parent_path().wstring();

            for (size_t i = 0; i < exePath.size(); i++)
                exePath[i] = std::tolower(exePath[i]);

            auto pos = lcaseLibName.rfind(exePath);

            if (Config::Instance()->EnableDlssInputs.value_or_default() && CheckDllNameW(&lcaseLibName, &nvngxNamesW) &&
                (!Config::Instance()->HookOriginalNvngxOnly.value_or_default() || pos == std::string::npos))
            {
                LOG_INFO("nvngx call: {0}, returning this dll!", lcaseLibNameA);

                // if (!dontCount)
                // loadCount++;

                return dllModule;
            }
        }

        if (!State::Instance().isWorkingAsNvngx &&
            (!State::Instance().isDxgiMode || !State::Instance().skipDxgiLoadChecks) &&
            CheckDllNameW(&lcaseLibName, &dllNamesW))
        {
            if (!State::Instance().ServeOriginal())
            {
                LOG_INFO("{} call, returning this dll!", lcaseLibNameA);
                return dllModule;
            }
            else
            {
                LOG_INFO("{} call, ServeOriginal active returning original dll!", lcaseLibNameA);
                return originalModule;
            }
        }

        // nvngx_dlss
        if (Config::Instance()->DLSSEnabled.value_or_default() && Config::Instance()->NVNGX_DLSS_Library.has_value() &&
            CheckDllNameW(&lcaseLibName, &nvngxDlssNamesW))
        {
            auto nvngxDlss = LoadNvngxDlss(lcaseLibName);

            if (nvngxDlss != nullptr)
                return nvngxDlss;
            else
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
        }

        // NGX OTA
        // Try to catch something like this:
        // c:\programdata/nvidia/ngx/models//dlss/versions/20316673/files/160_e658700.bin
        if (lcaseLibName.ends_with(L".bin"))
        {
            auto loadedBin = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (loadedBin && normalizedPath.contains("\\versions\\"))
            {
                if (normalizedPath.contains("\\dlss\\"))
                {
                    State::Instance().NGX_OTA_Dlss = wstring_to_string(lpLibFullPath);
                }

                if (normalizedPath.contains("\\dlssd\\"))
                {
                    State::Instance().NGX_OTA_Dlssd = wstring_to_string(lpLibFullPath);
                }
            }
            return loadedBin;
        }

        // NvApi64.dll
        if (CheckDllNameW(&lcaseLibName, &nvapiNamesW))
        {
            if (Config::Instance()->OverrideNvapiDll.value_or_default())
            {
                LOG_INFO("{0} call!", lcaseLibNameA);

                auto nvapi = LoadNvApi();

                // Nvapihooks intentionally won't load nvapi so have to make sure it's loaded
                if (nvapi != nullptr)
                {
                    NvApiHooks::Hook(nvapi);
                    return nvapi;
                }
            }
            else
            {
                auto nvapi = GetModuleHandleW(lcaseLibName.c_str());

                // Try to load nvapi only from system32, like the original call would
                if (nvapi == nullptr)
                {
                    nvapi = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
                }

                if (nvapi != nullptr)
                    NvApiHooks::Hook(nvapi);

                // AMD without nvapi override should fall through
            }
        }

        // sl.interposer.dll
        if (CheckDllNameW(&lcaseLibName, &slInterposerNamesW))
        {
            auto streamlineModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (streamlineModule != nullptr)
            {
                StreamlineHooks::hookInterposer(streamlineModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll: {}", lcaseLibNameA);
            }

            return streamlineModule;
        }

        // sl.dlss.dll
        // Try to catch something like this:
        // C:\ProgramData/NVIDIA/NGX/models/sl_dlss_0/versions/133120/files/190_E658703.dll
        if (CheckDllNameW(&lcaseLibName, &slDlssNamesW) ||
            (normalizedPath.contains("\\versions\\") && normalizedPath.contains("\\sl_dlss_0")))
        {
            auto dlssModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (dlssModule != nullptr)
            {
                StreamlineHooks::hookDlss(dlssModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll as sl.dlss: {}", lcaseLibNameA);
            }

            return dlssModule;
        }

        // sl.dlss_g.dll
        if (CheckDllNameW(&lcaseLibName, &slDlssgNamesW) ||
            (normalizedPath.contains("\\versions\\") && normalizedPath.contains("\\sl_dlss_g_")))
        {
            auto dlssgModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (dlssgModule != nullptr)
            {
                StreamlineHooks::hookDlssg(dlssgModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll as sl.dlss_g: {}", lcaseLibNameA);
            }

            return dlssgModule;
        }

        // sl.reflex.dll
        if (CheckDllNameW(&lcaseLibName, &slReflexNamesW) ||
            (normalizedPath.contains("\\versions\\") && normalizedPath.contains("\\sl_reflex_")))
        {
            auto reflexModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (reflexModule != nullptr)
            {
                StreamlineHooks::hookReflex(reflexModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll as sl.reflex: {}", lcaseLibNameA);
            }

            return reflexModule;
        }

        // sl.pcl.dll
        if (CheckDllNameW(&lcaseLibName, &slPclNamesW) ||
            (normalizedPath.contains("\\versions\\") && normalizedPath.contains("\\sl_pcl_")))
        {
            auto pclModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (pclModule != nullptr)
            {
                StreamlineHooks::hookPcl(pclModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll as sl.pcl: {}", lcaseLibNameA);
            }

            return pclModule;
        }

        // sl.common.dll
        if (CheckDllNameW(&lcaseLibName, &slCommonNamesW) ||
            (normalizedPath.contains("\\versions\\") && normalizedPath.contains("\\sl_common_")))
        {
            auto commonModule = NtdllProxy::LoadLibraryExW_Ldr(lpLibFullPath, NULL, 0);

            if (commonModule != nullptr)
            {
                StreamlineHooks::hookCommon(commonModule);
            }
            else
            {
                LOG_ERROR("Trying to load dll as sl.common: {}", lcaseLibNameA);
            }

            return commonModule;
        }

        if (Config::Instance()->DisableOverlays.value_or_default() && CheckDllNameW(&lcaseLibName, &blockOverlayNamesW))
        {
            LOG_DEBUG("Blocking overlay dll: {}", wstring_to_string(lcaseLibName));
            return (HMODULE) 1337;
        }
        else if (CheckDllNameW(&lcaseLibName, &overlayNamesW))
        {
            LOG_DEBUG("Overlay dll: {}", wstring_to_string(lcaseLibName));

            // If we hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow here
            // Order of CreateSwapChain calls become
            // Game -> Overlay -> Opti
            // and Overlays really does not like Opti's wrapped swapchain
            // If we skip hooking here first Steam hook CreateSwapChainForHwnd & CreateSwapChainForCoreWindow
            // Then hopefully Opti hook and call order become
            // Game -> Opti -> Overlay
            // And Opti menu works with Overlay without issues

            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                if (/*!_overlayMethodsCalled && */ DxgiProxy::Module() != nullptr)
                {
                    LOG_INFO("Calling CreateDxgiFactory methods for overlay!");
                    IDXGIFactory* factory = nullptr;
                    IDXGIFactory1* factory1 = nullptr;
                    IDXGIFactory2* factory2 = nullptr;

                    if (DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory) == S_OK && factory != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory ok");
                        factory->Release();
                    }

                    if (DxgiProxy::CreateDxgiFactory1_()(__uuidof(factory1), &factory1) == S_OK && factory1 != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory1 ok");
                        factory1->Release();
                    }

                    if (DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory2), &factory2) == S_OK &&
                        factory2 != nullptr)
                    {
                        LOG_DEBUG("CreateDxgiFactory2 ok");
                        factory2->Release();
                    }

                    _overlayMethodsCalled = true;
                }

                return module;
            }
        }

        // Hooks
        if (CheckDllNameW(&lcaseLibName, &dx11NamesW) && Config::Instance()->OverlayMenu.value_or_default())
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HooksDx::HookDx11(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &dx12NamesW) && Config::Instance()->OverlayMenu.value_or_default())
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                D3d12Proxy::Init(module);
                HooksDx::HookDx12();
            }

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &vkNamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
            {
                HookForVulkanSpoofing(module);
                HookForVulkanExtensionSpoofing(module);
                HookForVulkanVRAMSpoofing(module);

                HooksVk::HookVk(module);
            }

            return module;
        }

        if (!State::Instance().skipDxgiLoadChecks && CheckDllNameW(&lcaseLibName, &dxgiNamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);

            if (module != nullptr)
            {
                DxgiProxy::Init(module);

                if (Config::Instance()->DxgiSpoofing.value_or_default())
                    HookDxgiForSpoofing();

                if (Config::Instance()->OverlayMenu.value_or_default())
                    HooksDx::HookDxgi();
            }
        }

        if (CheckDllNameW(&lcaseLibName, &fsr2NamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR2Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &fsr2BENamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR2Dx12Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &fsr3NamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR3Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &fsr3BENamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            if (module != nullptr)
                HookFSR3Dx12Inputs(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &xessNamesW))
        {
            auto module = LoadLibxess(lcaseLibName);

            LOG_DEBUG("Libxess: {:X}", (size_t) module);

            if (module != nullptr)
                XeSSProxy::HookXeSS(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &xessDx11NamesW))
        {
            auto module = LoadLibxessDx11(lcaseLibName);

            if (module != nullptr)
                XeSSProxy::HookXeSSDx11(module);
            else
                LOG_ERROR("Trying to load dll: {}", wstring_to_string(lcaseLibName));

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &ffxDx12NamesW))
        {
            auto module = LoadFfxapiDx12(lcaseLibName);

            if (module != nullptr)
                FfxApiProxy::InitFfxDx12(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &ffxDx12UpscalerNamesW))
        {
            auto module = NtdllProxy::LoadLibraryExW_Ldr(lcaseLibName.c_str(), NULL, 0);

            FSR4ModelSelection::Hook(module);

            return module;
        }

        if (CheckDllNameW(&lcaseLibName, &ffxVkNamesW))
        {
            auto module = LoadFfxapiVk(lcaseLibName);

            if (module != nullptr)
                FfxApiProxy::InitFfxVk(module);

            return module;
        }

        return nullptr;
    }

    static HMODULE LoadNvApi()
    {
        HMODULE nvapi = nullptr;

        if (Config::Instance()->NvapiDllPath.has_value())
        {
            nvapi = NtdllProxy::LoadLibraryExW_Ldr(Config::Instance()->NvapiDllPath->c_str(), NULL, 0);

            if (nvapi != nullptr)
            {
                LOG_INFO("nvapi64.dll loaded from {0}", wstring_to_string(Config::Instance()->NvapiDllPath.value()));
                return nvapi;
            }
        }

        if (nvapi == nullptr)
        {
            auto localPath = Util::DllPath().parent_path() / L"nvapi64.dll";
            nvapi = NtdllProxy::LoadLibraryExW_Ldr(localPath.wstring().c_str(), NULL, 0);

            if (nvapi != nullptr)
            {
                LOG_INFO("nvapi64.dll loaded from {0}", wstring_to_string(localPath.wstring()));
                return nvapi;
            }
        }

        if (nvapi == nullptr)
        {
            nvapi = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, 0);

            if (nvapi != nullptr)
            {
                LOG_WARN("nvapi64.dll loaded from system!");
                return nvapi;
            }
        }

        return nullptr;
    }

    static HMODULE LoadNvngxDlss(std::wstring originalPath)
    {
        HMODULE nvngxDlss = nullptr;

        if (Config::Instance()->NVNGX_DLSS_Library.has_value())
        {
            nvngxDlss = NtdllProxy::LoadLibraryExW_Ldr(Config::Instance()->NVNGX_DLSS_Library.value().c_str(), NULL, 0);

            if (nvngxDlss != nullptr)
            {
                LOG_INFO("nvngx_dlss.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->NVNGX_DLSS_Library.value()));
                return nvngxDlss;
            }
            else
            {
                LOG_WARN("nvngx_dlss.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->NVNGX_DLSS_Library.value()));
            }
        }

        if (nvngxDlss == nullptr)
        {
            nvngxDlss = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

            if (nvngxDlss != nullptr)
            {
                LOG_INFO("nvngx_dlss.dll loaded from {0}", wstring_to_string(originalPath));
                return nvngxDlss;
            }
        }

        return nullptr;
    }

    static HMODULE LoadLibxess(std::wstring originalPath)
    {
        if (XeSSProxy::Module() != nullptr)
            return XeSSProxy::Module();

        HMODULE libxess = nullptr;

        if (Config::Instance()->XeSSLibrary.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->XeSSLibrary.value().c_str());

            if (libPath.has_filename())
                libxess = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                libxess = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"libxess.dll").c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(Config::Instance()->XeSSLibrary.value()));
                return libxess;
            }
            else
            {
                LOG_WARN("libxess.dll can't found at {0}", wstring_to_string(Config::Instance()->XeSSLibrary.value()));
            }
        }

        if (libxess == nullptr)
        {
            libxess = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess.dll loaded from {0}", wstring_to_string(originalPath));
                return libxess;
            }
        }

        return nullptr;
    }

    static HMODULE LoadLibxessDx11(std::wstring originalPath)
    {
        if (XeSSProxy::ModuleDx11() != nullptr)
            return XeSSProxy::ModuleDx11();

        HMODULE libxess = nullptr;

        if (Config::Instance()->XeSSDx11Library.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->XeSSDx11Library.value().c_str());

            if (libPath.has_filename())
                libxess = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                libxess = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"libxess_dx11.dll").c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess_dx11.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->XeSSDx11Library.value()));
                return libxess;
            }
            else
            {
                LOG_WARN("libxess_dx11.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->XeSSDx11Library.value()));
            }
        }

        if (libxess == nullptr)
        {
            libxess = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

            if (libxess != nullptr)
            {
                LOG_INFO("libxess_dx11.dll loaded from {0}", wstring_to_string(originalPath));
                return libxess;
            }
        }

        return nullptr;
    }

    static HMODULE LoadFfxapiDx12(std::wstring originalPath)
    {
        if (FfxApiProxy::Dx12Module() != nullptr)
            return FfxApiProxy::Dx12Module();

        HMODULE ffxDx12 = nullptr;

        if (Config::Instance()->FfxDx12Path.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxDx12Path.value().c_str());

            if (libPath.has_filename())
                ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_dx12.dll").c_str(), NULL, 0);

            if (ffxDx12 != nullptr)
            {
                LOG_INFO("amd_fidelityfx_dx12.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
                return ffxDx12;
            }
            else
            {
                LOG_WARN("amd_fidelityfx_dx12.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->FfxDx12Path.value()));
            }
        }

        if (ffxDx12 == nullptr)
        {
            ffxDx12 = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

            if (ffxDx12 != nullptr)
            {
                LOG_INFO("amd_fidelityfx_dx12.dll loaded from {0}", wstring_to_string(originalPath));
                return ffxDx12;
            }
        }

        return nullptr;
    }

    static HMODULE LoadFfxapiVk(std::wstring originalPath)
    {
        if (FfxApiProxy::VkModule() != nullptr)
            return FfxApiProxy::VkModule();

        HMODULE ffxVk = nullptr;

        if (Config::Instance()->FfxVkPath.has_value())
        {
            std::filesystem::path libPath(Config::Instance()->FfxVkPath.value().c_str());

            if (libPath.has_filename())
                ffxVk = NtdllProxy::LoadLibraryExW_Ldr(libPath.c_str(), NULL, 0);
            else
                ffxVk = NtdllProxy::LoadLibraryExW_Ldr((libPath / L"amd_fidelityfx_vk.dll").c_str(), NULL, 0);

            if (ffxVk != nullptr)
            {
                LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}",
                         wstring_to_string(Config::Instance()->FfxVkPath.value()));
                return ffxVk;
            }
            else
            {
                LOG_WARN("amd_fidelityfx_vk.dll can't found at {0}",
                         wstring_to_string(Config::Instance()->FfxVkPath.value()));
            }
        }

        if (ffxVk == nullptr)
        {
            ffxVk = NtdllProxy::LoadLibraryExW_Ldr(originalPath.c_str(), NULL, 0);

            if (ffxVk != nullptr)
            {
                LOG_INFO("amd_fidelityfx_vk.dll loaded from {0}", wstring_to_string(originalPath));
                return ffxVk;
            }
        }

        return nullptr;
    }

    static inline bool EndsWithInsensitive(std::wstring_view text, std::wstring_view suffix)
    {
        if (suffix.size() > text.size())
            return false;
        if (suffix.empty())
            return true;

        const wchar_t* tail = text.data() + (text.size() - suffix.size());
        const int res =
            CompareStringOrdinal(tail, static_cast<int>(suffix.size()), suffix.data(), static_cast<int>(suffix.size()),
                                 TRUE); // case-insensitive
        return res == CSTR_EQUAL;
    }

    static inline bool EndsWithInsensitive(const UNICODE_STRING& text, std::wstring_view suffix)
    {
        return EndsWithInsensitive(
            std::wstring_view { text.Buffer, static_cast<size_t>(text.Length) / sizeof(wchar_t) }, suffix);
    }

    static inline bool IsApiSetName(const std::wstring_view& n)
    {
        return n.rfind(L"api-ms-win-", 0) == 0 || n.rfind(L"ext-ms-", 0) == 0 || n.rfind(L"api-ms-onecore-", 0) == 0;
    }

    static NTSTATUS NTAPI hkLdrLoadDll(PWSTR PathToFile, PULONG Flags, PUNICODE_STRING ModuleFileName,
                                       PHANDLE ModuleHandle)
    {
        if (ModuleHandle == nullptr)
            return STATUS_INVALID_PARAMETER;

        if (ModuleFileName == nullptr || ModuleFileName->Length == 0 || State::Instance().isShuttingDown)
            return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);

        std::wstring_view name(ModuleFileName->Buffer, ModuleFileName->Length / sizeof(wchar_t));

        if (IsApiSetName(name))
            return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);

        if (State::SkipDllChecks())
        {
            const std::wstring skip = string_to_wstring(State::SkipDllName());

            if (skip.empty() || EndsWithInsensitive(name, std::wstring_view(skip)) ||
                EndsWithInsensitive(name, std::wstring(skip + L".dll")))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(name.data()));
                return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
            }
        }

#ifdef _DEBUG
        LOG_TRACE("{}", wstring_to_string(name.data()));
#endif

        auto moduleHandle = LoadLibraryCheckW(name.data(), name.data());

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1337)
        {
            return STATUS_DLL_NOT_FOUND;
        }

        if (moduleHandle != nullptr)
        {

            LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
            *ModuleHandle = (HANDLE) moduleHandle;
            return (NTSTATUS) 0x00000000L;
        }

        return o_LdrLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
    }

    static NTSTATUS NTAPI hkNtLoadDll(PUNICODE_STRING PathToFile, PULONG Flags, PUNICODE_STRING ModuleFileName,
                                      PHANDLE ModuleHandle)
    {
        if (ModuleHandle == nullptr)
            return STATUS_INVALID_PARAMETER;

        if (ModuleFileName == nullptr || ModuleFileName->Length == 0 || State::Instance().isShuttingDown)
            return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);

        std::wstring_view name(ModuleFileName->Buffer, ModuleFileName->Length / sizeof(wchar_t));

        if (IsApiSetName(name))
            return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);

        std::wstring_view path;
        if (PathToFile == nullptr || PathToFile->Length == 0)
            path = name;
        else
            path = std::wstring_view(PathToFile->Buffer, PathToFile->Length / sizeof(wchar_t));

        if (State::SkipDllChecks())
        {
            const std::wstring skip = string_to_wstring(State::SkipDllName());

            if (skip.empty() || EndsWithInsensitive(name, std::wstring_view(skip)) ||
                EndsWithInsensitive(name, std::wstring(skip + L".dll")))
            {
                LOG_TRACE("Skip checks for: {}", wstring_to_string(name.data()));
                return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
            }
        }

#ifdef _DEBUG
        LOG_TRACE("{}", wstring_to_string(name.data()));
#endif

        auto moduleHandle = LoadLibraryCheckW(name.data(), path.data());

        // skip loading of dll
        if (moduleHandle == (HMODULE) 1337)
        {
            return STATUS_DLL_NOT_FOUND;
        }

        if (moduleHandle != nullptr)
        {
            LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
            *ModuleHandle = (HANDLE) moduleHandle;
            return (NTSTATUS) 0x00000000L;
        }

        return o_NtLoadDll(PathToFile, Flags, ModuleFileName, ModuleHandle);
    }

    static NTSTATUS NTAPI hkLdrUnloadDll(PVOID lpLibrary)
    {
        if (lpLibrary == nullptr)
            return STATUS_INVALID_PARAMETER;

#ifdef _DEBUG
        LOG_TRACE("{:X}", (size_t) lpLibrary);
#endif

        if (!State::Instance().isShuttingDown)
        {
            if (lpLibrary == dllModule)
            {
                LOG_WARN("Call for OptiScaler, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == FfxApiProxy::Dx12Module())
            {
                LOG_WARN("Call for FFX Dx12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == FfxApiProxy::VkModule())
            {
                LOG_WARN("Call for FFX Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == XeSSProxy::Module())
            {
                LOG_WARN("Call for XeSS, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == DxgiProxy::Module())
            {
                LOG_WARN("Call for DXGI, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == D3d12Proxy::Module())
            {
                LOG_WARN("Call for D3D12, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == Kernel32Proxy::Module())
            {
                LOG_WARN("Call for Kernel32, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == KernelBaseProxy::Module())
            {
                LOG_WARN("Call for KernelBase, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == NtdllProxy::Module())
            {
                LOG_WARN("Call for ntdll, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == vulkanModule)
            {
                LOG_WARN("Call for Vulkan, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
            else if (lpLibrary == d3d11Module)
            {
                LOG_WARN("Call for D3D11, caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));
                State::Instance().modulesToFree.insert(lpLibrary);
                return TRUE;
            }
        }

        return o_LdrUnloadDll(lpLibrary);
    }

  public:
    static void Hook()
    {
        if (o_LdrLoadDll != nullptr)
            return;

        if (NtdllProxy::Module() == nullptr)
            return;

        o_NtLoadDll = NtdllProxy::Hook_NtLoadDll(hkNtLoadDll);
        o_LdrLoadDll = NtdllProxy::Hook_LdrLoadDll(hkLdrLoadDll);
        o_LdrUnloadDll = NtdllProxy::Hook_LdrUnloadDll(hkLdrUnloadDll);
    }
};
