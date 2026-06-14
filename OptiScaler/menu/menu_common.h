#pragma once

#include "SysUtils.h"
#include <Util.h>
#include <Config.h>
#include <resource.h>
#include <Logger.h>

#include <imgui/imgui.h>
#include <imgui/imgui_impl_win32.h>
#include <imgui/imgui_impl_uwp.h>

#include <detours/detours.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

class ScopedIndent
{
  public:
    explicit ScopedIndent(float indent = 16.0f) : m_indent(indent) { ImGui::Indent(m_indent); }

    ~ScopedIndent() { ImGui::Unindent(m_indent); }

  private:
    float m_indent;
};

class ScopedCollapsingHeader
{
  public:
    explicit ScopedCollapsingHeader(const char* label, ImGuiTreeNodeFlags flags = 0)
    {
        ImGui::PushID(label);

        ImGui::BeginChild("##CollapsingHeaderChild", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        _headerOpen = ImGui::CollapsingHeader(label, flags);
        _active = true;
    }

    bool IsHeaderOpen() const { return _headerOpen; }

    ~ScopedCollapsingHeader()
    {
        if (_active)
        {
            ImGui::EndChild();
            ImGui::PopID();
        }
    }

  private:
    bool _active = false;
    bool _headerOpen = false;
};

template <typename T> struct MenuOption
{
    T value;
    std::string label;
    std::string tooltip;
    bool disabled = false;

    MenuOption& set_disabled(bool condition, const std::string& reason = "")
    {
        if (condition)
        {
            disabled = true;
            if (!reason.empty())
                tooltip = reason;
        }
        return *this;
    }
};

class MenuCommon
{
  private:
    // internal values
    inline static HWND _handle = nullptr;
    inline static WNDPROC _oWndProc = nullptr;
    inline static bool _isVisible = false;
    inline static bool _isInited = false;
    inline static bool _isUWP = false;

    // mipmap calculations
    inline static bool _showMipmapCalcWindow = false;
    inline static bool _showHudlessWindow = false;
    inline static float _mipBias = 0.0f;
    inline static float _mipBiasCalculated = 0.0f;
    inline static uint32_t _mipmapUpscalerQuality = 0;
    inline static float _mipmapUpscalerRatio = 0;
    inline static uint32_t _displayWidth = 0;
    inline static uint32_t _renderWidth = 0;

    inline static UINT64 _frameCount = 0;

    // reflex
    inline static float _limitFps = std::numeric_limits<float>::infinity();

    // ffx
    inline static int _ffxUpscalerIndex = -1;
    inline static int _ffxFGIndex = -1;

    // output scaling
    inline static float _ssRatio = 0.0f;
    inline static bool _ssEnabled = false;
    inline static Scaler _ssDownsampler = Scaler::FSR1;

    // ui scale
    inline static int _selectedScale = 0;

    // overlay states
    inline static bool _dx11Ready = false;
    inline static bool _dx12Ready = false;
    inline static bool _vulkanReady = false;

    inline static void ShowTooltip(const char* tip);

    inline static void ShowHelpMarker(const char* tip);
    inline static void ShowResetButton(CustomOptional<bool, NoDefault>* initFlag, std::string buttonName);
    inline static void ReInitUpscaler();

    inline static void SeparatorWithHelpMarker(const char* label, const char* tip);

    static Upscaler GetBackendCode(const API api);
    static void GetCurrentBackendInfo(const API api, Upscaler& upscaler, std::string* name);
    static void RenderUpscalerCombo(const API api, Upscaler currentUpscaler, const std::vector<Upscaler>& options);
    static void AddDx11Backends(Upscaler upscaler);
    static void AddDx12Backends(Upscaler upscaler);
    static void AddVulkanBackends(Upscaler upscaler);
    template <HasDefaultValue B> static void AddResourceBarrier(std::string name, CustomOptional<int32_t, B>* value);
    template <HasDefaultValue B> static void AddDLSSRenderPreset(std::string name, CustomOptional<uint32_t, B>* value);
    template <HasDefaultValue B> static void AddDLSSDRenderPreset(std::string name, CustomOptional<uint32_t, B>* value);
    template <typename TStorage, typename T>
    static void PopulateCombo(const std::string& name, TStorage& currentValue,
                              const std::vector<MenuOption<T>>& options);

    static void UpdateManualInput(HWND targetHwnd);

  public:
    static void Dx11Inited() { _dx11Ready = true; }
    static void Dx12Inited() { _dx12Ready = true; }
    static void VulkanInited() { _vulkanReady = true; }
    static bool IsInited() { return _isInited; }
    static bool IsVisible() { return _isVisible; }
    static HWND Handle() { return _handle; }

    static bool RenderMenu();
    static void Init(HWND InHwnd, bool isUWP);
    static void Shutdown();
    static void HideMenu();
    static void Present();
};
