#pragma once
#include "SysUtils.h"

#include <filesystem>

#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d12.h>

namespace Util
{
typedef struct _version_t
{
    uint16_t major;
    uint16_t minor;
    uint16_t patch;
    uint16_t reserved;

    _version_t() : major(0), minor(0), patch(0), reserved(0) {}

    constexpr _version_t(uint16_t maj, uint16_t min, uint16_t pat, uint16_t res)
        : major(maj), minor(min), patch(pat), reserved(res)
    {
    }

    bool operator==(const _version_t& other) const
    {
        return major == other.major && minor == other.minor && patch == other.patch && reserved == other.reserved;
    }

    bool operator!=(const _version_t& other) const { return !(*this == other); }

    bool operator<(const _version_t& other) const
    {
        if (major != other.major)
            return major < other.major;
        if (minor != other.minor)
            return minor < other.minor;
        if (patch != other.patch)
            return patch < other.patch;
        return reserved < other.reserved;
    }

    bool operator>(const _version_t& other) const { return other < *this; }

    bool operator<=(const _version_t& other) const { return !(other < *this); }

    bool operator>=(const _version_t& other) const { return !(*this < other); }
} version_t;

struct MonitorInfo
{
    HMONITOR handle;
    int x;
    int y;
    int width;
    int height;
    RECT monitorRect;  // full monitor bounds
    RECT workRect;     // work area (taskbar excluded)
    std::wstring name; // e.g., \\.\DISPLAY1
};

std::filesystem::path ExePath();
std::filesystem::path DllPath();
std::optional<std::filesystem::path> NvngxPath();
double MillisecondsNow();

HWND GetProcessWindow();
bool GetFileVersion(std::wstring dllPath, version_t* fileVersionOut, version_t* productVersionOut = nullptr);
bool GetRealWindowsVersion(OSVERSIONINFOW& osInfo);
std::string GetWindowsName(const OSVERSIONINFOW& os);
std::wstring GetExeProductName();
std::wstring GetWindowTitle(HWND hwnd);
std::optional<std::filesystem::path> FindFilePath(const std::filesystem::path& startDir,
                                                  const std::filesystem::path fileName);
std::optional<std::filesystem::path> WhoIsTheCallerPath(void* returnAddress);
std::string WhoIsTheCaller(void* returnAddress);
HMODULE GetCallerModule(void* returnAddress);
MonitorInfo GetMonitorInfoForWindow(HWND hwnd);
MonitorInfo GetMonitorInfoForOutput(IDXGIOutput* pOutput);
int GetActiveRefreshRate(HWND hwnd);
bool CheckForRealObject(std::string functionName, IUnknown* pObject, IUnknown** ppRealObject);
void GetDeviceRemovedReason(ID3D11Device* pDevice);
void GetDeviceRemovedReason(ID3D12Device* pDevice);
void LoadProxyLibrary(const std::wstring& name, const std::wstring& optiPath, const std::wstring& overridePath,
                      HMODULE* memoryModule, HMODULE* loadedModule);

}; // namespace Util

inline void ThrowIfFailed(HRESULT hr)
{
    if (FAILED(hr))
    {
        // Set a breakpoint on this line to catch DirectX API errors
        throw std::exception();
    }
}
