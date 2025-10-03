#pragma once

#include <pch.h>

#include <Util.h>
#include <State.h>
#include <Config.h>

#include "Gdi32_Hooks.h"
#include "Streamline_Hooks.h"
#include "LibraryLoad_Hooks.h"

#include <fsr4/FSR4ModelSelection.h>

#include <cwctype>

#pragma intrinsic(_ReturnAddress)

// Enables hooking of GetModuleHandle
// which might create issues, not tested very well
// #define HOOK_GET_MODULE

#ifdef HOOK_GET_MODULE
// Handle nvngx.dll calls on GetModule handle
// #define GET_MODULE_NVNGX

// Handle Opti dll calls on GetModule handle
#define GET_MODULE_DLL
#endif

class KernelHooks
{
  private:
    inline static Kernel32Proxy::PFN_FreeLibrary o_K32_FreeLibrary = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryA o_K32_LoadLibraryA = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryW o_K32_LoadLibraryW = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryExA o_K32_LoadLibraryExA = nullptr;
    inline static Kernel32Proxy::PFN_LoadLibraryExW o_K32_LoadLibraryExW = nullptr;
    inline static Kernel32Proxy::PFN_GetProcAddress o_K32_GetProcAddress = nullptr;
    inline static Kernel32Proxy::PFN_GetModuleHandleA o_K32_GetModuleHandleA = nullptr;
    inline static Kernel32Proxy::PFN_GetModuleHandleExW o_K32_GetModuleHandleExW = nullptr;
    inline static Kernel32Proxy::PFN_GetFileAttributesW o_K32_GetFileAttributesW = nullptr;
    inline static Kernel32Proxy::PFN_CreateFileW o_K32_CreateFileW = nullptr;

    inline static KernelBaseProxy::PFN_FreeLibrary o_KB_FreeLibrary = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryA o_KB_LoadLibraryA = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryW o_KB_LoadLibraryW = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryExA o_KB_LoadLibraryExA = nullptr;
    inline static KernelBaseProxy::PFN_LoadLibraryExW o_KB_LoadLibraryExW = nullptr;
    inline static KernelBaseProxy::PFN_GetProcAddress o_KB_GetProcAddress = nullptr;

    static constexpr HMODULE amdxc64Mark = HMODULE(0xFFFFFFFF13372137);

    static FARPROC WINAPI hk_K32_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
    {

        if ((size_t) lpProcName < 0x000000000000F000)
        {
            if (hModule == dllModule)
                LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

            return o_K32_GetProcAddress(hModule, lpProcName);
        }

        if (hModule == dllModule && lpProcName != nullptr)
        {
            LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
                      Util::WhoIsTheCaller(_ReturnAddress()));
        }

        std::string pName(lpProcName);

        if (pName == "xessDestroyContext")
        {
            LOG_DEBUG("XeSSProxy: {:X}", (size_t) XeSSProxy::Module());
            LOG_DEBUG("Address: {:X}", (size_t) o_K32_GetModuleHandleA("libxess.dll"));
            LOG_DEBUG("HERE!");
        }

        // FSR 4 Init in case of missing amdxc64.dll
        // 2nd check is amdxcffx64.dll trying to queue amdxc64 but amdxc64 not being loaded.
        // Also skip the internal call of amdxc64
        if (lpProcName != nullptr && (hModule == amdxc64Mark || hModule == nullptr) &&
            lstrcmpA(lpProcName, "AmdExtD3DCreateInterface") == 0 &&
            Config::Instance()->Fsr4Update.value_or_default() &&
            Util::GetCallerModule(_ReturnAddress()) != KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll"))
        {
            return (FARPROC) &hkAmdExtD3DCreateInterface;
        }

        if (State::Instance().isRunningOnLinux && lpProcName != nullptr &&
            hModule == KernelBaseProxy::GetModuleHandleW_()(L"gdi32.dll") &&
            lstrcmpA(lpProcName, "D3DKMTEnumAdapters2") == 0)
        {
            return (FARPROC) &customD3DKMTEnumAdapters2;
        }

        return o_K32_GetProcAddress(hModule, lpProcName);
    }

    static HMODULE WINAPI hk_K32_GetModuleHandleA(LPCSTR lpModuleName)
    {
        if (lpModuleName != NULL)
        {
            if (strcmp(lpModuleName, "nvngx_dlssg.dll") == 0)
            {
                LOG_TRACE("Trying to get module handle of {}, caller: {}", lpModuleName,
                          Util::WhoIsTheCaller(_ReturnAddress()));
                return dllModule;
            }
            else if (strcmp(lpModuleName, "amdxc64.dll") == 0)
            {
                // Libraries like FFX SDK or AntiLag 2 SDK do not load amdxc64 themselves
                // so most likely amdxc64 is getting loaded by the driver itself.
                // Therefore it should be safe for us to return a custom implementation when it's not loaded
                // This can get removed if Proton starts to ship amdxc64

                CheckForGPU();

                auto original = o_K32_GetModuleHandleA(lpModuleName);

                if (original == nullptr && Config::Instance()->Fsr4Update.value_or_default())
                {
                    LOG_INFO("amdxc64.dll is not loaded, giving a fake HMODULE");
                    return amdxc64Mark;
                }

                return original;
            }
        }

        return o_K32_GetModuleHandleA(lpModuleName);
    }

    static BOOL WINAPI hk_K32_GetModuleHandleExW(DWORD dwFlags, LPCWSTR lpModuleName, HMODULE* phModule)
    {
        if (lpModuleName && dwFlags == GET_MODULE_HANDLE_EX_FLAG_PIN && lstrcmpW(L"nvapi64.dll", lpModuleName) == 0 &&
            phModule)
        {
            LOG_TRACE("Suspected SpecialK call for nvapi64");
            *phModule = LibraryLoadHooks::LoadNvApi();
            return true;
        }

        return o_K32_GetModuleHandleExW(dwFlags, lpModuleName, phModule);
    }

    static FARPROC WINAPI hk_KB_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
    {
        if ((size_t) lpProcName < 0x000000000000F000)
        {
            if (hModule == dllModule)
                LOG_TRACE("Ordinal call: {:X}", (size_t) lpProcName);

            return o_KB_GetProcAddress(hModule, lpProcName);
        }

        if (hModule == dllModule && lpProcName != nullptr)
        {
            LOG_TRACE("Trying to get process address of {}, caller: {}", lpProcName,
                      Util::WhoIsTheCaller(_ReturnAddress()));
        }

        if (State::Instance().isRunningOnLinux && lpProcName != nullptr &&
            hModule == KernelBaseProxy::GetModuleHandleW_()(L"gdi32.dll") &&
            lstrcmpA(lpProcName, "D3DKMTEnumAdapters2") == 0)
            return (FARPROC) &customD3DKMTEnumAdapters2;

        return o_KB_GetProcAddress(hModule, lpProcName);
    }

    static inline void NormalizePath(std::string& path)
    {
        while (!path.empty() && (path.back() == '\\' || path.back() == '/'))
            path.pop_back();
    }

    static inline bool IsInsideWindowsDirectory(const std::string& path)
    {
        char windowsDir[MAX_PATH];
        UINT len = GetWindowsDirectoryA(windowsDir, MAX_PATH);

        if (len == 0 || len >= MAX_PATH)
            return false;

        std::string pathToCheck(path);
        std::string windowsPath(windowsDir);

        NormalizePath(pathToCheck);
        NormalizePath(windowsPath);

        to_lower_in_place(pathToCheck);
        to_lower_in_place(windowsPath);

        // Check if pathToCheck starts with windowsPath, while having a slash after that
        if (pathToCheck.compare(0, windowsPath.size(), windowsPath) == 0 &&
            (pathToCheck.size() == windowsPath.size() || pathToCheck[windowsPath.size()] == '\\' ||
             pathToCheck[windowsPath.size()] == '/'))
            return true;

        return false;
    }

    static DWORD WINAPI hk_K32_GetFileAttributesW(LPCWSTR lpFileName)
    {
        if (!State::Instance().nvngxExists && State::Instance().nvngxReplacement.has_value() &&
            (Config::Instance()->DxgiSpoofing.value_or_default() ||
             Config::Instance()->StreamlineSpoofing.value_or_default()))
        {
            auto path = wstring_to_string(std::wstring(lpFileName));
            to_lower_in_place(path);

            if (path.contains("nvngx.dll") && !path.contains("_nvngx.dll") &&
                !IsInsideWindowsDirectory(path)) // apply the override to just one path
            {
                LOG_DEBUG("Overriding GetFileAttributesW for nvngx");
                return FILE_ATTRIBUTE_ARCHIVE;
            }
        }

        return o_K32_GetFileAttributesW(lpFileName);
    }

    static HANDLE WINAPI hk_K32_CreateFileW(LPCWSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode,
                                            LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition,
                                            DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
    {
        if (!State::Instance().nvngxExists && State::Instance().nvngxReplacement.has_value() &&
            (Config::Instance()->DxgiSpoofing.value_or_default() ||
             Config::Instance()->StreamlineSpoofing.value_or_default()))
        {
            auto path = wstring_to_string(std::wstring(lpFileName));
            to_lower_in_place(path);

            static auto signedDll = Util::FindFilePath(Util::DllPath().remove_filename(), "nvngx_dlss.dll");

            if (path.contains("nvngx.dll") && !path.contains("_nvngx.dll") && // apply the override to just one path
                !IsInsideWindowsDirectory(path) && signedDll.has_value())
            {
                LOG_DEBUG("Overriding CreateFileW for nvngx with a signed dll, original path: {}", path);
                return o_K32_CreateFileW(signedDll.value().c_str(), dwDesiredAccess, dwShareMode, lpSecurityAttributes,
                                         dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile);
            }
        }

        return o_K32_CreateFileW(lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition,
                                 dwFlagsAndAttributes, hTemplateFile);
    }

    // Load Library checks
    static HMODULE CheckLoad(const std::wstring& name)
    {
        do
        {
            if (State::Instance().isShuttingDown || LibraryLoadHooks::IsApiSetName(name))
                break;

            if (State::SkipDllChecks())
            {
                const std::wstring skip = string_to_wstring(State::SkipDllName());

                if (skip.empty() || LibraryLoadHooks::EndsWithInsensitive(name, std::wstring_view(skip)) ||
                    LibraryLoadHooks::EndsWithInsensitive(name, std::wstring(skip + L".dll")))
                {
                    LOG_TRACE("Skip checks for: {}", wstring_to_string(name.data()));
                    break;
                }
            }

            auto moduleHandle = LibraryLoadHooks::LoadLibraryCheckW(name.data(), name.data());

            // skip loading of dll
            if (moduleHandle == (HMODULE) 1337)
                break;

            if (moduleHandle != nullptr)
            {
                LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
                return moduleHandle;
            }
        } while (false);

        return nullptr;
    }

    static HMODULE hk_K32_LoadLibraryW(LPCWSTR lpLibFileName)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring name(lpLibFileName);

#ifdef _DEBUG
        // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto result = CheckLoad(name);

        if (result != nullptr)
            return result;

        return o_K32_LoadLibraryW(lpLibFileName);
    }

    static HMODULE hk_K32_LoadLibraryA(LPCSTR lpLibFileName)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::string nameA(lpLibFileName);
        std::wstring name = string_to_wstring(nameA);

#ifdef _DEBUG
        // LOG_TRACE("{}, caller: {}", nameA.data(), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto result = CheckLoad(name);

        if (result != nullptr)
            return result;

        return o_K32_LoadLibraryA(lpLibFileName);
    }

    static HMODULE hk_K32_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring name(lpLibFileName);

#ifdef _DEBUG
        // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto result = CheckLoad(name);

        if (result != nullptr)
            return result;

        return o_K32_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    }

    static HMODULE hk_K32_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::string nameA(lpLibFileName);
        std::wstring name = string_to_wstring(nameA);

#ifdef _DEBUG
        // LOG_TRACE("{}, caller: {}", nameA.data(), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto result = CheckLoad(name);

        if (result != nullptr)
            return result;

        return o_K32_LoadLibraryExA(lpLibFileName, hFile, dwFlags);
    }

    static HMODULE hk_KB_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
    {
        if (lpLibFileName == nullptr)
            return NULL;

        std::wstring name(lpLibFileName);

#ifdef _DEBUG
        // LOG_TRACE("{}, caller: {}", wstring_to_string(name.data()), Util::WhoIsTheCaller(_ReturnAddress()));
#endif

        auto result = CheckLoad(name);

        if (result != nullptr)
            return result;

        return o_KB_LoadLibraryExW(lpLibFileName, hFile, dwFlags);
    }

    static BOOL hk_K32_FreeLibrary(HMODULE lpLibrary)
    {
        if (lpLibrary == nullptr)
            return STATUS_INVALID_PARAMETER;

#ifdef _DEBUG
        // LOG_TRACE("{:X}", (size_t) lpLibrary);
#endif

        if (!State::Instance().isShuttingDown)
        {
            auto result = LibraryLoadHooks::FreeLibrary(lpLibrary);

            if (result.has_value())
                return result.value() == TRUE;
        }

        return o_K32_FreeLibrary(lpLibrary);
    }

  public:
    static void Hook()
    {
        if (o_K32_FreeLibrary != nullptr)
            return;

        LOG_DEBUG("");

        o_K32_GetProcAddress = Kernel32Proxy::Hook_GetProcAddress(hk_K32_GetProcAddress);
        o_K32_GetModuleHandleA = Kernel32Proxy::Hook_GetModuleHandleA(hk_K32_GetModuleHandleA);
        o_K32_GetModuleHandleExW = Kernel32Proxy::Hook_GetModuleHandleExW(hk_K32_GetModuleHandleExW);
        o_K32_GetFileAttributesW = Kernel32Proxy::Hook_GetFileAttributesW(hk_K32_GetFileAttributesW);
        o_K32_CreateFileW = Kernel32Proxy::Hook_CreateFileW(hk_K32_CreateFileW);

        if (!Config::Instance()->UseNtdllHooks.value_or_default())
        {
            o_K32_FreeLibrary = Kernel32Proxy::Hook_FreeLibrary(hk_K32_FreeLibrary);
            o_K32_LoadLibraryA = Kernel32Proxy::Hook_LoadLibraryA(hk_K32_LoadLibraryA);
            o_K32_LoadLibraryW = Kernel32Proxy::Hook_LoadLibraryW(hk_K32_LoadLibraryW);
            o_K32_LoadLibraryExA = Kernel32Proxy::Hook_LoadLibraryExA(hk_K32_LoadLibraryExA);
            o_K32_LoadLibraryExW = Kernel32Proxy::Hook_LoadLibraryExW(hk_K32_LoadLibraryExW);
        }
    }

    static void HookBase()
    {
        if (o_KB_GetProcAddress != nullptr)
            return;

        LOG_DEBUG("");

        o_KB_GetProcAddress = KernelBaseProxy::Hook_GetProcAddress(hk_KB_GetProcAddress);

        if (!Config::Instance()->UseNtdllHooks.value_or_default())
        {
            o_KB_LoadLibraryExW = KernelBaseProxy::Hook_LoadLibraryExW(hk_KB_LoadLibraryExW);
        }
    }
};
