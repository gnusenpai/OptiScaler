#include "FSR3_Dx12_FG.h"

#include "Config.h"
#include "Util.h"

#include "resource.h"
#include "NVNGX_Parameter.h"

#include <proxies/KernelBase_Proxy.h>

#include <scanner/scanner.h>

#include "detours/detours.h"

#include "fsr3/ffx_fsr3.h"
#include "fsr3/dx12/ffx_dx12.h"
#include "fsr3/ffx_frameinterpolation.h"

// Swapchain create
typedef Fsr3::FfxErrorCode (*PFN_ffxReplaceSwapchainForFrameinterpolationDX12)(Fsr3::FfxCommandQueue gameQueue,
                                                                               Fsr3::FfxSwapchain& gameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxCreateFrameinterpolationSwapchainDX12)(DXGI_SWAP_CHAIN_DESC* desc,
                                                                           ID3D12CommandQueue* queue,
                                                                           IDXGIFactory* dxgiFactory,
                                                                           Fsr3::FfxSwapchain& outGameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12)(
    HWND hWnd, DXGI_SWAP_CHAIN_DESC1* desc1, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc, ID3D12CommandQueue* queue,
    IDXGIFactory* dxgiFactory, Fsr3::FfxSwapchain& outGameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxWaitForPresents)(Fsr3::FfxSwapchain gameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxRegisterFrameinterpolationUiResourceDX12)(Fsr3::FfxSwapchain gameSwapChain,
                                                                              Fsr3::FfxResource uiResource);

typedef Fsr3::FfxErrorCode (*PFN_ffxGetFrameinterpolationCommandlistDX12)(Fsr3::FfxSwapchain gameSwapChain,
                                                                          Fsr3::FfxCommandList& gameCommandlist);

typedef Fsr3::FfxResource (*PFN_ffxGetFrameinterpolationTextureDX12)(Fsr3::FfxSwapchain gameSwapChain);

// Context
typedef Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationContextCreate)(
    FfxFrameInterpolationContext* context, FfxFrameInterpolationContextDescription* contextDescription);

typedef Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationDispatch)(FfxFrameInterpolationContext* context,
                                                                FfxFrameInterpolationDispatchDescription* params);

typedef Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationContextDestroy)(FfxFrameInterpolationContext* context);

typedef Fsr3::FfxErrorCode (*PFN_ffxFsr3ConfigureFrameGeneration)(void* context,
                                                                  Fsr3::FfxFrameGenerationConfig* config);

// Swapchain
static PFN_ffxReplaceSwapchainForFrameinterpolationDX12 o_ffxReplaceSwapchainForFrameinterpolationDX12 = nullptr;
static PFN_ffxCreateFrameinterpolationSwapchainDX12 o_ffxCreateFrameinterpolationSwapchainDX12 = nullptr;
static PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12 o_ffxCreateFrameinterpolationSwapchainForHwndDX12 = nullptr;
static PFN_ffxWaitForPresents o_ffxWaitForPresents = nullptr;
static PFN_ffxRegisterFrameinterpolationUiResourceDX12 o_ffxRegisterFrameinterpolationUiResourceDX12 = nullptr;
static PFN_ffxGetFrameinterpolationCommandlistDX12 o_ffxGetFrameinterpolationCommandlistDX12 = nullptr;
static PFN_ffxGetFrameinterpolationTextureDX12 o_ffxGetFrameinterpolationTextureDX12 = nullptr;

// Context
static PFN_ffxFrameInterpolationContextCreate o_ffxFrameInterpolationContextCreate = nullptr;
static PFN_ffxFrameInterpolationDispatch o_ffxFrameInterpolationDispatch = nullptr;
static PFN_ffxFrameInterpolationContextDestroy o_ffxFrameInterpolationContextDestroy = nullptr;
static PFN_ffxFsr3ConfigureFrameGeneration o_ffxFsr3ConfigureFrameGeneration = nullptr;

static ID3D12Device* _device = nullptr;
static FG_Constants _fgConst {};
static UINT64 _currentFrameId = 0;

static Fsr3::FfxPresentCallbackFunc _presentCallback = nullptr;
static void* _presentCallbackUserContext = nullptr;
static UINT64 _presentCallbackFrameId = 0;

static std::mutex _newFrameMutex;

static ID3D12Resource* _hudless[BUFFER_COUNT] = {};
static Dx12Resource _uiRes[BUFFER_COUNT] = {};

static Fsr3::FfxResourceStates GetFfxApiState(D3D12_RESOURCE_STATES state)
{
    switch (state)
    {
    case D3D12_RESOURCE_STATE_COMMON:
        return Fsr3::FFX_RESOURCE_STATE_COMMON;
    case D3D12_RESOURCE_STATE_UNORDERED_ACCESS:
        return Fsr3::FFX_RESOURCE_STATE_UNORDERED_ACCESS;
    case D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:
        return Fsr3::FFX_RESOURCE_STATE_COMPUTE_READ;
    case D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE:
        return Fsr3::FFX_RESOURCE_STATE_PIXEL_READ;
    case (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE):
        return Fsr3::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ;
    case D3D12_RESOURCE_STATE_COPY_SOURCE:
        return Fsr3::FFX_RESOURCE_STATE_COPY_SRC;
    case D3D12_RESOURCE_STATE_COPY_DEST:
        return Fsr3::FFX_RESOURCE_STATE_COPY_DEST;
    case D3D12_RESOURCE_STATE_GENERIC_READ:
        return Fsr3::FFX_RESOURCE_STATE_GENERIC_READ;
    case D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return Fsr3::FFX_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case D3D12_RESOURCE_STATE_RENDER_TARGET:
        return Fsr3::FFX_RESOURCE_STATE_RENDER_TARGET;
    default:
        return Fsr3::FFX_RESOURCE_STATE_COMMON;
    }
}

static D3D12_RESOURCE_STATES GetD3D12State(Fsr3::FfxResourceStates state)
{
    switch (state)
    {
    case Fsr3::FFX_RESOURCE_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case Fsr3::FFX_RESOURCE_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case Fsr3::FFX_RESOURCE_STATE_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case Fsr3::FFX_RESOURCE_STATE_PIXEL_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case Fsr3::FFX_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    case Fsr3::FFX_RESOURCE_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case Fsr3::FFX_RESOURCE_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case Fsr3::FFX_RESOURCE_STATE_GENERIC_READ:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case Fsr3::FFX_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case Fsr3::FFX_RESOURCE_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

// Hook Methods
static Fsr3::FfxErrorCode hkffxReplaceSwapchainForFrameinterpolationDX12(Fsr3::FfxCommandQueue gameQueue,
                                                                         Fsr3::FfxSwapchain& gameSwapChain)
{
    if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
    {
        gameSwapChain = State::Instance().currentFGSwapchain;
        return Fsr3::FFX_OK;
    }
    else
    {
        LOG_ERROR("currentFG: {:X}, currentFGSwapchain: {:X}", (size_t) State::Instance().currentFG,
                  (size_t) State::Instance().currentFGSwapchain);

        return Fsr3::FFX_ERROR_INVALID_ALIGNMENT;
    }
}

static Fsr3::FfxErrorCode hkffxCreateFrameinterpolationSwapchainDX12(DXGI_SWAP_CHAIN_DESC* desc,
                                                                     ID3D12CommandQueue* queue,
                                                                     IDXGIFactory* dxgiFactory,
                                                                     Fsr3::FfxSwapchain& outGameSwapChain)
{
    if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
    {
        auto result =
            State::Instance().currentFG->CreateSwapchain(dxgiFactory, queue, desc, (IDXGISwapChain**) outGameSwapChain);

        return result ? Fsr3::FFX_OK : Fsr3::FFX_ERROR_BACKEND_API_ERROR;
    }
    else
    {
        LOG_ERROR("currentFG: {:X}, currentFGSwapchain: {:X}", (size_t) State::Instance().currentFG,
                  (size_t) State::Instance().currentFGSwapchain);

        return Fsr3::FFX_ERROR_INVALID_ALIGNMENT;
    }
}

static Fsr3::FfxErrorCode hkffxCreateFrameinterpolationSwapchainForHwndDX12(
    HWND hWnd, DXGI_SWAP_CHAIN_DESC1* desc1, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc, ID3D12CommandQueue* queue,
    IDXGIFactory* dxgiFactory, Fsr3::FfxSwapchain& outGameSwapChain)
{
    if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
    {
        auto result = State::Instance().currentFG->CreateSwapchain1(dxgiFactory, queue, hWnd, desc1, fullscreenDesc,
                                                                    (IDXGISwapChain1**) outGameSwapChain);

        return result ? Fsr3::FFX_OK : Fsr3::FFX_ERROR_BACKEND_API_ERROR;
    }
    else
    {
        LOG_ERROR("currentFG: {:X}, currentFGSwapchain: {:X}", (size_t) State::Instance().currentFG,
                  (size_t) State::Instance().currentFGSwapchain);

        return Fsr3::FFX_ERROR_INVALID_ALIGNMENT;
    }
}

static Fsr3::FfxErrorCode hkffxWaitForPresents(Fsr3::FfxSwapchain gameSwapChain) { return Fsr3::FFX_OK; }

static Fsr3::FfxErrorCode hkffxRegisterFrameinterpolationUiResourceDX12(Fsr3::FfxSwapchain gameSwapChain,
                                                                        Fsr3::FfxResource uiResource)
{
    LOG_DEBUG("UiResource found 1: {:X}", (size_t) uiResource.resource);

    auto fg = State::Instance().currentFG;

    if (fg->FrameGenerationContext() != nullptr && uiResource.resource != nullptr)
    {
        UINT width = 0;
        UINT height = 0;

        DXGI_SWAP_CHAIN_DESC scDesc {};
        State::Instance().currentFGSwapchain->GetDesc(&scDesc);

        width = scDesc.BufferDesc.Width;
        height = scDesc.BufferDesc.Height;

        Dx12Resource ui {};
        ui.cmdList = nullptr; // Not sure about this
        ui.height = height;
        ui.resource = (ID3D12Resource*) uiResource.resource;
        ui.state = GetD3D12State((Fsr3::FfxResourceStates) uiResource.state);
        ui.type = FG_ResourceType::UIColor;
        ui.validity = FG_ResourceValidity::UntilPresent;
        ui.width = width;
        ui.left = 0;
        ui.top = 0;

        _uiRes[fg->GetIndex()] = ui;

        fg->SetResource(&ui);
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxGetFrameinterpolationCommandlistDX12(Fsr3::FfxSwapchain gameSwapChain,
                                                                    Fsr3::FfxCommandList& gameCommandlist)
{
    auto fg = State::Instance().currentFG;

    if (fg != nullptr)
    {
        gameCommandlist = fg->GetUICommandList();
        LOG_DEBUG("Returning cmdList: {:X}", (size_t) gameCommandlist);
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxResource hkffxGetFrameinterpolationTextureDX12(Fsr3::FfxSwapchain gameSwapChain)
{
    LOG_WARN("");
    return (Fsr3::FfxResource) nullptr;
}

static Fsr3::FfxErrorCode
hkffxFrameInterpolationContextCreate(FfxFrameInterpolationContext* context,
                                     FfxFrameInterpolationContextDescription* contextDescription)
{
    if (context == nullptr || contextDescription == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    if (State::Instance().currentFG->FrameGenerationContext() != nullptr)
    {
        LOG_INFO("There is already an active FG context: {:X}, destroying it.",
                 (size_t) State::Instance().currentFG->FrameGenerationContext());

        State::Instance().currentFG->DestroyFGContext();
    }

    _fgConst = {};

    _fgConst.displayHeight = contextDescription->displaySize.height;
    _fgConst.displayWidth = contextDescription->displaySize.width;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED) > 0)
        _fgConst.flags |= FG_Flags::DisplayResolutionMVs;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INVERTED) > 0)
        _fgConst.flags |= FG_Flags::InvertedDepth;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_DEPTH_INFINITE) > 0)
        _fgConst.flags |= FG_Flags::InfiniteDepth;

    if ((contextDescription->flags & FFX_FRAMEINTERPOLATION_ENABLE_HDR_COLOR_INPUT) > 0)
        _fgConst.flags |= FG_Flags::Hdr;

    Config::Instance()->FGXeFGDepthInverted = _fgConst.flags[FG_Flags::InvertedDepth];
    Config::Instance()->FGXeFGJitteredMV = _fgConst.flags[FG_Flags::JitteredMVs];
    Config::Instance()->FGXeFGHighResMV = _fgConst.flags[FG_Flags::DisplayResolutionMVs];
    LOG_DEBUG("XeFG DepthInverted: {}", Config::Instance()->FGXeFGDepthInverted.value_or_default());
    LOG_DEBUG("XeFG JitteredMV: {}", Config::Instance()->FGXeFGJitteredMV.value_or_default());
    LOG_DEBUG("XeFG HighResMV: {}", Config::Instance()->FGXeFGHighResMV.value_or_default());
    Config::Instance()->SaveXeFG();

    State::Instance().currentFG->CreateContext(_device, _fgConst);

    *context = {};
    context->data[0] = fgContext;

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxFrameInterpolationDispatch(FfxFrameInterpolationContext* context,
                                                          FfxFrameInterpolationDispatchDescription* params)
{
    if (context == nullptr || params == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    float ratio = (float) params->interpolationRect.width / (float) params->interpolationRect.height;
    fg->SetCameraValues(params->cameraNear, params->cameraFar, params->cameraFovAngleVertical, ratio,
                        params->viewSpaceToMetersFactor);
    fg->SetInterpolationPos(params->interpolationRect.left, params->interpolationRect.top);
    fg->SetInterpolationRect(params->interpolationRect.width, params->interpolationRect.height);
    fg->SetFrameTimeDelta(params->frameTimeDelta);
    fg->SetReset(params->reset ? 1 : 0);

    if (params->currentBackBuffer_HUDLess.resource != nullptr &&
        fg->GetResource(FG_ResourceType::HudlessColor) == nullptr)
    {
        UINT width = params->interpolationRect.width;
        UINT height = params->interpolationRect.height;
        UINT left = params->interpolationRect.left;
        UINT top = params->interpolationRect.top;

        if (width == 0)
        {
            DXGI_SWAP_CHAIN_DESC scDesc {};
            State::Instance().currentFGSwapchain->GetDesc(&scDesc);

            width = scDesc.BufferDesc.Width;
            height = scDesc.BufferDesc.Height;
            top = 0;
            left = 0;
        }

        Dx12Resource hudless {};
        hudless.cmdList = (ID3D12GraphicsCommandList*) params->commandList;
        hudless.height = height;
        hudless.resource = (ID3D12Resource*) params->currentBackBuffer_HUDLess.resource;
        hudless.state = GetD3D12State((Fsr3::FfxResourceStates) params->currentBackBuffer_HUDLess.state);
        hudless.type = FG_ResourceType::HudlessColor;
        hudless.validity = FG_ResourceValidity::UntilPresent;
        hudless.width = width;
        hudless.top = top;
        hudless.left = left;
        fg->SetResource(&hudless);
    }
}

static Fsr3::FfxErrorCode hkffxFrameInterpolationContextDestroy(FfxFrameInterpolationContext* context)
{
    if (context == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    LOG_DEBUG("");

    if (State::Instance().currentFG != nullptr && fgContext == context->data[0])
    {
        LOG_INFO("Destroying FG Context: {:X}", (size_t) State::Instance().currentFG);
        State::Instance().currentFG->DestroyFGContext();
    }

    return Fsr3::FFX_OK;
}

static Fsr3::FfxErrorCode hkffxFsr3ConfigureFrameGeneration(void* context, Fsr3::FfxFrameGenerationConfig* config)
{
    if (context == nullptr || config == nullptr)
        return Fsr3::FFX_ERROR_INVALID_ARGUMENT;

    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return Fsr3::FFX_ERROR_NULL_DEVICE;
    }

    if (fg->FrameGenerationContext() != nullptr && config->HUDLessColor.resource != nullptr)
    {
        {
            std::lock_guard<std::mutex> lock(_newFrameMutex);
            fg->StartNewFrame();
            _uiRes[fg->GetIndex()] = {};
        }

        LOG_DEBUG("hkffxFsr3ConfigureFrameGeneration enabled: {} ", config->frameGenerationEnabled);

        State::Instance().FSRFGInputActive = config->frameGenerationEnabled;

        if (config->frameGenerationEnabled && !fg->IsActive() && Config::Instance()->FGEnabled.value_or_default())
        {
            fg->Activate();
            fg->ResetCounters();
        }
        else if (!config->frameGenerationEnabled && fg->IsActive())
        {
            fg->Deactivate();
            fg->ResetCounters();
        }

        UINT width = 0;
        UINT height = 0;
        UINT left = 0;
        UINT top = 0;

        fg->GetInterpolationPos(left, top);
        fg->GetInterpolationRect(width, height);

        if (width == 0)
        {
            DXGI_SWAP_CHAIN_DESC scDesc {};
            State::Instance().currentFGSwapchain->GetDesc(&scDesc);

            width = scDesc.BufferDesc.Width;
            height = scDesc.BufferDesc.Height;
            top = 0;
            left = 0;
        }

        Dx12Resource ui {};
        ui.cmdList = nullptr; // Not sure about this
        ui.height = height;
        ui.resource = (ID3D12Resource*) config->HUDLessColor.resource;
        ui.state = GetD3D12State((Fsr3::FfxResourceStates) config->HUDLessColor.state);
        ui.type = FG_ResourceType::HudlessColor;
        ui.validity = FG_ResourceValidity::UntilPresent;
        ui.width = width;
        ui.left = left;
        ui.top = top;

        _uiRes[fg->GetIndex()] = ui;

        fg->SetResource(&ui);

        if (config->frameGenerationCallback != nullptr)
        {
            LOG_DEBUG("frameGenerationCallback exist");
        }

        if (config->presentCallback != nullptr)
        {
            LOG_DEBUG("presentCallback exist");
            _presentCallback = config->presentCallback;
        }
    }

    return Fsr3::FFX_OK;
}

void HookFSR3FGExeInputs()
{
    if (o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr)
        return;

    LOG_INFO("Trying to hook FSR3 methods");

    // Swapchain
    o_ffxReplaceSwapchainForFrameinterpolationDX12 =
        (PFN_ffxReplaceSwapchainForFrameinterpolationDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxReplaceSwapchainForFrameinterpolationDX12");
    o_ffxCreateFrameinterpolationSwapchainDX12 =
        (PFN_ffxCreateFrameinterpolationSwapchainDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxCreateFrameinterpolationSwapchainDX12");
    o_ffxCreateFrameinterpolationSwapchainForHwndDX12 =
        (PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxCreateFrameinterpolationSwapchainForHwndDX12");
    o_ffxWaitForPresents = (PFN_ffxWaitForPresents) KernelBaseProxy::GetProcAddress_()(exeModule, "ffxWaitForPresents");
    o_ffxRegisterFrameinterpolationUiResourceDX12 =
        (PFN_ffxRegisterFrameinterpolationUiResourceDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxRegisterFrameinterpolationUiResourceDX12");
    o_ffxGetFrameinterpolationCommandlistDX12 =
        (PFN_ffxGetFrameinterpolationCommandlistDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxGetFrameinterpolationCommandlistDX12");
    o_ffxGetFrameinterpolationTextureDX12 =
        (PFN_ffxGetFrameinterpolationTextureDX12) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxGetFrameinterpolationTextureDX12");

    // Context
    o_ffxFrameInterpolationContextCreate = (PFN_ffxFrameInterpolationContextCreate) KernelBaseProxy::GetProcAddress_()(
        exeModule, "ffxFrameInterpolationContextCreate");
    o_ffxFrameInterpolationDispatch = (PFN_ffxFrameInterpolationDispatch) KernelBaseProxy::GetProcAddress_()(
        exeModule, "ffxFrameInterpolationDispatch");
    o_ffxFrameInterpolationContextDestroy =
        (PFN_ffxFrameInterpolationContextDestroy) KernelBaseProxy::GetProcAddress_()(
            exeModule, "ffxFrameInterpolationContextDestroy");
    o_ffxFsr3ConfigureFrameGeneration = (PFN_ffxFsr3ConfigureFrameGeneration) KernelBaseProxy::GetProcAddress_()(
        exeModule, "ffxFsr3ConfigureFrameGeneration");

    LOG_DEBUG("ffxReplaceSwapchainForFrameinterpolationDX12: {:X}",
              (size_t) o_ffxReplaceSwapchainForFrameinterpolationDX12);
    LOG_DEBUG("ffxCreateFrameinterpolationSwapchainDX12: {:X}", (size_t) o_ffxCreateFrameinterpolationSwapchainDX12);
    LOG_DEBUG("ffxCreateFrameinterpolationSwapchainForHwndDX12: {:X}",
              (size_t) o_ffxCreateFrameinterpolationSwapchainForHwndDX12);
    LOG_DEBUG("ffxWaitForPresents: {:X}", (size_t) o_ffxWaitForPresents);
    LOG_DEBUG("ffxRegisterFrameinterpolationUiResourceDX12: {:X}",
              (size_t) o_ffxRegisterFrameinterpolationUiResourceDX12);
    LOG_DEBUG("ffxGetFrameinterpolationCommandlistDX12: {:X}", (size_t) o_ffxGetFrameinterpolationCommandlistDX12);
    LOG_DEBUG("ffxGetFrameinterpolationTextureDX12: {:X}", (size_t) o_ffxGetFrameinterpolationTextureDX12);
    LOG_DEBUG("ffxFrameInterpolationContextCreate: {:X}", (size_t) o_ffxFrameInterpolationContextCreate);
    LOG_DEBUG("ffxFrameInterpolationDispatch: {:X}", (size_t) o_ffxFrameInterpolationDispatch);
    LOG_DEBUG("ffxFrameInterpolationContextDestroy: {:X}", (size_t) o_ffxFrameInterpolationContextDestroy);
    LOG_DEBUG("ffxFsr3ConfigureFrameGeneration: {:X}", (size_t) o_ffxFsr3ConfigureFrameGeneration);

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_ffxReplaceSwapchainForFrameinterpolationDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxReplaceSwapchainForFrameinterpolationDX12,
                     hkffxReplaceSwapchainForFrameinterpolationDX12);
    if (o_ffxCreateFrameinterpolationSwapchainDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxCreateFrameinterpolationSwapchainDX12, hkffxCreateFrameinterpolationSwapchainDX12);
    if (o_ffxCreateFrameinterpolationSwapchainForHwndDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxCreateFrameinterpolationSwapchainForHwndDX12,
                     hkffxCreateFrameinterpolationSwapchainForHwndDX12);
    if (o_ffxWaitForPresents != nullptr)
        DetourAttach(&(PVOID&) o_ffxWaitForPresents, hkffxWaitForPresents);
    if (o_ffxRegisterFrameinterpolationUiResourceDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxRegisterFrameinterpolationUiResourceDX12,
                     hkffxRegisterFrameinterpolationUiResourceDX12);
    if (o_ffxGetFrameinterpolationCommandlistDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxGetFrameinterpolationCommandlistDX12, hkffxGetFrameinterpolationCommandlistDX12);
    if (o_ffxGetFrameinterpolationTextureDX12 != nullptr)
        DetourAttach(&(PVOID&) o_ffxGetFrameinterpolationTextureDX12, hkffxGetFrameinterpolationTextureDX12);
    if (o_ffxFrameInterpolationContextCreate != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationContextCreate, hkffxFrameInterpolationContextCreate);
    if (o_ffxFrameInterpolationDispatch != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationDispatch, hkffxFrameInterpolationDispatch);
    if (o_ffxFrameInterpolationContextDestroy != nullptr)
        DetourAttach(&(PVOID&) o_ffxFrameInterpolationContextDestroy, hkffxFrameInterpolationContextDestroy);
    if (o_ffxFsr3ConfigureFrameGeneration != nullptr)
        DetourAttach(&(PVOID&) o_ffxFsr3ConfigureFrameGeneration, hkffxFsr3ConfigureFrameGeneration);

    DetourTransactionCommit();
}

void HookFSR3FGInputs(HMODULE module) {}
