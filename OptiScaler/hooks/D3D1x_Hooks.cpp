#include "D3D1x_Hooks.h"

#include <Util.h>
#include <Config.h>

#include <wrapped/wrapped_swapchain.h>

#include <resource_tracking/ResTrack_Dx12.h>

#include <proxies/D3D12_Proxy.h>
#include <proxies/IGDExt_Proxy.h>

#include <detours/detours.h>

#include <d3d11_4.h>
#include <d3d11on12.h>
#include <d3d12.h>
#include <dxgi1_6.h>

#pragma intrinsic(_ReturnAddress)

#pragma region FG definitions

// Swapchain frame counter
static UINT64 _frameCounter = 0;
static double _lastFrameTime = 0.0;
UINT _lastPresentFlags = 0;
bool _skipPresent = false;
bool _skipResize = false;
bool _skipDx11Create = false;

#pragma endregion

// DirectX
typedef void (*PFN_CreateSampler)(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                                  D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

typedef HRESULT (*PFN_CheckFeatureSupport)(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                           UINT FeatureSupportDataSize);

typedef HRESULT (*PFN_CreateCommittedResource)(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                               D3D12_HEAP_FLAGS HeapFlags, D3D12_RESOURCE_DESC* pDesc,
                                               D3D12_RESOURCE_STATES InitialResourceState,
                                               const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                               void** ppvResource);

typedef HRESULT (*PFN_CreatePlacedResource)(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                            D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                            const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid,
                                            void** ppvResource);

typedef D3D12_RESOURCE_ALLOCATION_INFO (*PFN_GetResourceAllocationInfo)(ID3D12Device* device, UINT visibleMask,
                                                                        UINT numResourceDescs,
                                                                        D3D12_RESOURCE_DESC* pResourceDescs);

typedef HRESULT (*PFN_CreateSamplerState)(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                          ID3D11SamplerState** ppSamplerState);

typedef ULONG (*PFN_Release)(IUnknown* This);

static PFN_CreateSampler o_CreateSampler = nullptr;
static PFN_CheckFeatureSupport o_CheckFeatureSupport = nullptr;
static PFN_CreateCommittedResource o_CreateCommittedResource = nullptr;
static PFN_CreatePlacedResource o_CreatePlacedResource = nullptr;
static PFN_GetResourceAllocationInfo o_GetResourceAllocationInfo = nullptr;

static D3d12Proxy::PFN_D3D12CreateDevice o_D3D12CreateDevice = nullptr;
static D3d12Proxy::PFN_D3D12SerializeRootSignature o_D3D12SerializeRootSignature = nullptr;
static D3d12Proxy::PFN_D3D12SerializeVersionedRootSignature o_D3D12SerializeVersionedRootSignature = nullptr;
static PFN_Release o_D3D12DeviceRelease = nullptr;

static PFN_D3D11_CREATE_DEVICE o_D3D11CreateDevice = nullptr;
static PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN o_D3D11CreateDeviceAndSwapChain = nullptr;
static PFN_CreateSamplerState o_CreateSamplerState = nullptr;
static PFN_D3D11ON12_CREATE_DEVICE o_D3D11On12CreateDevice = nullptr;

static ID3D12Device* _intelD3D12Device = nullptr;
static ULONG _intelD3D12DeviceRefTarget = 0;
static bool _d3d12Captured = false;

static void hkCreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor);

static HRESULT hkCheckFeatureSupport(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                     UINT FeatureSupportDataSize);

static ULONG hkD3D12DeviceRelease(IUnknown* device);

static HRESULT hkCreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                         D3D12_HEAP_FLAGS HeapFlags, D3D12_RESOURCE_DESC* pDesc,
                                         D3D12_RESOURCE_STATES InitialResourceState,
                                         const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                         void** ppvResource);

static HRESULT hkCreatePlacedResource(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                      D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                      const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource);

static D3D12_RESOURCE_ALLOCATION_INFO hkGetResourceAllocationInfo(ID3D12Device* device, UINT visibleMask,
                                                                  UINT numResourceDescs,
                                                                  D3D12_RESOURCE_DESC* pResourceDescs);

static HRESULT hkCreateSamplerState(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                    ID3D11SamplerState** ppSamplerState);

static inline D3D11_FILTER UpgradeToAF(D3D11_FILTER f)
{
    if (Config::Instance()->AnisotropySkipPointFilter.value_or_default() &&
        (f == D3D11_FILTER_MIN_MAG_MIP_POINT || f == D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT ||
         f == D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT || f == D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT ||
         f == D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT || f == D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT ||
         f == D3D11_FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT || f == D3D11_FILTER_MAXIMUM_MIN_MAG_LINEAR_MIP_POINT ||
         f == D3D11_FILTER_MIN_POINT_MAG_LINEAR_MIP_POINT ||
         f == D3D11_FILTER_COMPARISON_MIN_POINT_MAG_LINEAR_MIP_POINT ||
         f == D3D11_FILTER_MINIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT ||
         f == D3D11_FILTER_MAXIMUM_MIN_POINT_MAG_LINEAR_MIP_POINT))
    {
        return f;
    }

    if (f >= D3D11_FILTER_COMPARISON_MIN_MAG_MIP_POINT && f <= D3D11_FILTER_COMPARISON_ANISOTROPIC)
    {
        return Config::Instance()->AnisotropyModifyComp.value_or_default() ? D3D11_FILTER_COMPARISON_ANISOTROPIC : f;
    }

    if (f >= D3D11_FILTER_MINIMUM_MIN_MAG_MIP_POINT && f <= D3D11_FILTER_MINIMUM_ANISOTROPIC)
    {
        return Config::Instance()->AnisotropyModifyMinMax.value_or_default() ? D3D11_FILTER_MINIMUM_ANISOTROPIC : f;
    }

    if (f >= D3D11_FILTER_MAXIMUM_MIN_MAG_MIP_POINT && f <= D3D11_FILTER_MAXIMUM_ANISOTROPIC)
    {
        return Config::Instance()->AnisotropyModifyMinMax.value_or_default() ? D3D11_FILTER_MAXIMUM_ANISOTROPIC : f;
    }

    return D3D11_FILTER_ANISOTROPIC;
}

static inline D3D12_FILTER UpgradeToAF(D3D12_FILTER f)
{
    // Skip point filter
    const auto minF = D3D12_DECODE_MIN_FILTER(f);
    const auto magF = D3D12_DECODE_MAG_FILTER(f);
    const auto mipF = D3D12_DECODE_MIP_FILTER(f);
    if (Config::Instance()->AnisotropySkipPointFilter.value_or_default() &&
        ((mipF == D3D12_FILTER_TYPE_POINT) || (minF == D3D12_FILTER_TYPE_POINT && magF == D3D12_FILTER_TYPE_POINT)))
    {
        return f;
    }

    const auto reduction = D3D12_DECODE_FILTER_REDUCTION(f);

    if (reduction == D3D12_FILTER_REDUCTION_TYPE_COMPARISON)
    {
        if (Config::Instance()->AnisotropyModifyComp.value_or_default())
            return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_COMPARISON);

        return f;
    }

    if (reduction == D3D12_FILTER_REDUCTION_TYPE_MINIMUM)
    {
        if (Config::Instance()->AnisotropyModifyMinMax.value_or_default())
            return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_MINIMUM);

        return f;
    }

    if (reduction == D3D12_FILTER_REDUCTION_TYPE_MAXIMUM)
    {
        if (Config::Instance()->AnisotropyModifyMinMax.value_or_default())
            return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_MAXIMUM);

        return f;
    }

    return D3D12_ENCODE_ANISOTROPIC_FILTER(D3D12_FILTER_REDUCTION_TYPE_STANDARD);
}

#pragma region DirectX hooks

static void HookToDevice(ID3D12Device* InDevice)
{
    if (o_CreateSampler != nullptr || InDevice == nullptr)
        return;

    LOG_DEBUG("Dx12");

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) InDevice;

    ID3D12Device* realDevice = nullptr;
    if (Util::CheckForRealObject(__FUNCTION__, InDevice, (IUnknown**) &realDevice))
        pVTable = *(PVOID**) realDevice;

    // hudless
    o_D3D12DeviceRelease = (PFN_Release) pVTable[2];
    o_CreateSampler = (PFN_CreateSampler) pVTable[22];
    o_CheckFeatureSupport = (PFN_CheckFeatureSupport) pVTable[13];
    o_GetResourceAllocationInfo = (PFN_GetResourceAllocationInfo) pVTable[25];
    o_CreateCommittedResource = (PFN_CreateCommittedResource) pVTable[27];
    o_CreatePlacedResource = (PFN_CreatePlacedResource) pVTable[29];

    // Apply the detour
    if (o_CreateSampler != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_CreateSampler != nullptr)
            DetourAttach(&(PVOID&) o_CreateSampler, hkCreateSampler);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            if (o_CheckFeatureSupport != nullptr)
                DetourAttach(&(PVOID&) o_CheckFeatureSupport, hkCheckFeatureSupport);

            if (o_CreateCommittedResource != nullptr)
                DetourAttach(&(PVOID&) o_CreateCommittedResource, hkCreateCommittedResource);

            if (o_CreatePlacedResource != nullptr)
                DetourAttach(&(PVOID&) o_CreatePlacedResource, hkCreatePlacedResource);

            if (o_D3D12DeviceRelease != nullptr)
                DetourAttach(&(PVOID&) o_D3D12DeviceRelease, hkD3D12DeviceRelease);

            // This does not work but luckily
            // UE works without Intel Extension for it
            // if (o_GetResourceAllocationInfo != nullptr)
            //    DetourAttach(&(PVOID&) o_GetResourceAllocationInfo, hkGetResourceAllocationInfo);
        }

        DetourTransactionCommit();
    }

    if (State::Instance().activeFgInput != FGInput::Nukems && Config::Instance()->OverlayMenu.value_or_default())
        ResTrack_Dx12::HookDevice(InDevice);
}

static void HookToDevice(ID3D11Device* InDevice)
{
    if (o_CreateSamplerState != nullptr || InDevice == nullptr)
        return;

    LOG_DEBUG("Dx11");

    // Get the vtable pointer
    PVOID* pVTable = *(PVOID**) InDevice;

    o_CreateSamplerState = (PFN_CreateSamplerState) pVTable[23];

    // Apply the detour
    if (o_CreateSamplerState != nullptr)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourAttach(&(PVOID&) o_CreateSamplerState, hkCreateSamplerState);

        DetourTransactionCommit();
    }
}

static HRESULT hkD3D11On12CreateDevice(IUnknown* pDevice, UINT Flags, D3D_FEATURE_LEVEL* pFeatureLevels,
                                       UINT FeatureLevels, IUnknown** ppCommandQueues, UINT NumQueues, UINT NodeMask,
                                       ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext,
                                       D3D_FEATURE_LEVEL* pChosenFeatureLevel)
{
    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    bool rtss = false;

    // Assuming RTSS is creating a D3D11on12 device, not sure why but sometimes RTSS tries to create
    // it's D3D11on12 device with old CommandQueue which results crash
    // I am changing it's CommandQueue with current swapchain's command queue
    if (State::Instance().currentCommandQueue != nullptr && *ppCommandQueues != State::Instance().currentCommandQueue &&
        GetModuleHandle(L"RTSSHooks64.dll") != nullptr && pDevice == State::Instance().currentD3D12Device)
    {
        LOG_INFO("Replaced RTSS CommandQueue with correct one {0:X} -> {1:X}", (UINT64) *ppCommandQueues,
                 (UINT64) State::Instance().currentCommandQueue);
        *ppCommandQueues = State::Instance().currentCommandQueue;
        rtss = true;
    }

    auto result = o_D3D11On12CreateDevice(pDevice, Flags, pFeatureLevels, FeatureLevels, ppCommandQueues, NumQueues,
                                          NodeMask, ppDevice, ppImmediateContext, pChosenFeatureLevel);

    if (result == S_OK && *ppDevice != nullptr && !rtss && !_d3d12Captured)
    {
        LOG_INFO("Device captured, D3D11Device: {0:X}", (UINT64) *ppDevice);
        HookToDevice(*ppDevice);
    }

    if (result == S_OK && *ppDevice != nullptr)
        State::Instance().d3d11Devices.push_back(*ppDevice);

    LOG_FUNC_RESULT(result);

    return result;
}

static HRESULT hkD3D11CreateDevice(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
                                   CONST D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
                                   ID3D11Device** ppDevice, D3D_FEATURE_LEVEL* pFeatureLevel,
                                   ID3D11DeviceContext** ppImmediateContext)
{
    if (_skipDx11Create)
    {
        LOG_DEBUG("Skip");
        return o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
                                   ppDevice, pFeatureLevel, ppImmediateContext);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr)
    {
        State::Instance().skipSpoofing = true;
        if (pAdapter->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));

            if (desc.VendorId == VendorId::Microsoft)
            {
                State::Instance().skipSpoofing = false;

                _skipDx11Create = true;
                State::Instance().skipParentWrapping = true;

                auto result = o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                                  SDKVersion, ppDevice, pFeatureLevel, ppImmediateContext);

                State::Instance().skipParentWrapping = false;
                _skipDx11Create = false;

                return result;
            }
        }

        State::Instance().skipSpoofing = false;
    }

    if (!(State::Instance().gameQuirks & GameQuirk::SkipD3D11FeatureLevelElevation))
    {
        static const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
        };

        D3D_FEATURE_LEVEL maxLevel = D3D_FEATURE_LEVEL_1_0_CORE;

        for (UINT i = 0; i < FeatureLevels; ++i)
        {
            maxLevel = std::max(maxLevel, pFeatureLevels[i]);
        }

        if (maxLevel == D3D_FEATURE_LEVEL_11_0)
        {
            LOG_INFO("Overriding D3D_FEATURE_LEVEL, "
                     "Game requested D3D_FEATURE_LEVEL_11_0, "
                     "we need D3D_FEATURE_LEVEL_11_1!");

            pFeatureLevels = levels;
            FeatureLevels = ARRAYSIZE(levels);
        }
    }

    _skipDx11Create = true;

    State::Instance().skipParentWrapping = true;

    auto result = o_D3D11CreateDevice(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion,
                                      ppDevice, pFeatureLevel, ppImmediateContext);

    State::Instance().skipParentWrapping = false;

    _skipDx11Create = false;

    if (result == S_OK && *ppDevice != nullptr && !_d3d12Captured)
    {
        LOG_INFO("Device captured");

        if (szName.size() > 0)
            State::Instance().DeviceAdapterNames[*ppDevice] = wstring_to_string(szName);

        HookToDevice(*ppDevice);
    }

    LOG_FUNC_RESULT(result);

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr)
        State::Instance().d3d11Devices.push_back(*ppDevice);

    return result;
}

static HRESULT hkD3D11CreateDeviceAndSwapChain(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
                                               UINT Flags, CONST D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
                                               UINT SDKVersion, DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
                                               IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
                                               D3D_FEATURE_LEVEL* pFeatureLevel,
                                               ID3D11DeviceContext** ppImmediateContext)
{
    if (_skipDx11Create)
    {

        LOG_DEBUG("Skip");
        return o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                               SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel,
                                               ppImmediateContext);
    }

    LOG_DEBUG("Caller: {}", Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX11
    Flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr)
    {
        State::Instance().skipSpoofing = true;
        if (pAdapter->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));

            if (desc.VendorId == VendorId::Microsoft)
            {
                State::Instance().skipSpoofing = false;

                _skipDx11Create = true;
                State::Instance().skipParentWrapping = true;

                auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels,
                                                              FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
                                                              ppDevice, pFeatureLevel, ppImmediateContext);

                State::Instance().skipParentWrapping = false;
                _skipDx11Create = true;

                return result;
            }
        }

        State::Instance().skipSpoofing = false;
    }

    if (!(State::Instance().gameQuirks & GameQuirk::SkipD3D11FeatureLevelElevation))
    {
        static const D3D_FEATURE_LEVEL levels[] = {
            D3D_FEATURE_LEVEL_11_1,
        };

        D3D_FEATURE_LEVEL maxLevel = D3D_FEATURE_LEVEL_1_0_CORE;

        for (UINT i = 0; i < FeatureLevels; ++i)
        {
            maxLevel = std::max(maxLevel, pFeatureLevels[i]);
        }

        if (maxLevel == D3D_FEATURE_LEVEL_11_0)
        {
            LOG_INFO("Overriding D3D_FEATURE_LEVEL, "
                     "Game requested D3D_FEATURE_LEVEL_11_0, "
                     "we need D3D_FEATURE_LEVEL_11_1!");

            pFeatureLevels = levels;
            FeatureLevels = ARRAYSIZE(levels);
        }
    }

    if (pSwapChainDesc != nullptr && pSwapChainDesc->BufferDesc.Height == 2 && pSwapChainDesc->BufferDesc.Width == 2)
    {
        LOG_WARN("Overlay call!");

        _skipDx11Create = true;
        State::Instance().skipParentWrapping = true;

        auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels,
                                                      FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice,
                                                      pFeatureLevel, ppImmediateContext);

        State::Instance().skipParentWrapping = false;
        _skipDx11Create = false;

        return result;
    }

    // For vsync override
    if (pSwapChainDesc != nullptr)
    {
        if (!pSwapChainDesc->Windowed)
        {
            LOG_INFO("Game is creating fullscreen swapchain, disabled V-Sync overrides");
            Config::Instance()->OverrideVsync.set_volatile_value(false);
        }

        if (Config::Instance()->OverrideVsync.value_or_default())
        {
            pSwapChainDesc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            pSwapChainDesc->Flags |= DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

            if (pSwapChainDesc->BufferCount < 2)
                pSwapChainDesc->BufferCount = 2;
        }
    }

    _skipDx11Create = true;

    auto result = o_D3D11CreateDeviceAndSwapChain(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels,
                                                  SDKVersion, pSwapChainDesc, ppSwapChain, ppDevice, pFeatureLevel,
                                                  ppImmediateContext);
    _skipDx11Create = false;

    if (result == S_OK && *ppDevice != nullptr && !_d3d12Captured)
    {
        LOG_INFO("Device captured");
        HookToDevice(*ppDevice);
    }

    if (result == S_OK && pSwapChainDesc != nullptr && ppSwapChain != nullptr && *ppSwapChain != nullptr &&
        ppDevice != nullptr && *ppDevice != nullptr)
    {
        // check for SL proxy
        IDXGISwapChain* realSC = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppSwapChain, (IUnknown**) &realSC))
            realSC = *ppSwapChain;

        State::Instance().currentRealSwapchain = realSC;

        IUnknown* readDevice = nullptr;
        if (!Util::CheckForRealObject(__FUNCTION__, *ppDevice, (IUnknown**) &readDevice))
            readDevice = *ppDevice;

        if (Util::GetProcessWindow() == pSwapChainDesc->OutputWindow)
        {
            State::Instance().screenWidth = pSwapChainDesc->BufferDesc.Width;
            State::Instance().screenHeight = pSwapChainDesc->BufferDesc.Height;
        }

        LOG_DEBUG("Created new swapchain: {0:X}, hWnd: {1:X}", (UINT64) *ppSwapChain,
                  (UINT64) pSwapChainDesc->OutputWindow);

        *ppSwapChain =
            new WrappedIDXGISwapChain4(realSC, readDevice, pSwapChainDesc->OutputWindow, pSwapChainDesc->Flags, false);

        State::Instance().currentSwapchain = *ppSwapChain;
        State::Instance().currentWrappedSwapchain = *ppSwapChain;

        LOG_DEBUG("Created new WrappedIDXGISwapChain4: {0:X}, pDevice: {1:X}", (UINT64) *ppSwapChain,
                  (UINT64) *ppDevice);
    }

    if (result == S_OK && ppDevice != nullptr && *ppDevice != nullptr)
        State::Instance().d3d11Devices.push_back(*ppDevice);

    LOG_FUNC_RESULT(result);
    return result;
}

#ifdef ENABLE_DEBUG_LAYER_DX12
static ID3D12Debug3* debugController = nullptr;
static ID3D12InfoQueue* infoQueue = nullptr;
static ID3D12InfoQueue1* infoQueue1 = nullptr;

static void CALLBACK D3D12DebugCallback(D3D12_MESSAGE_CATEGORY Category, D3D12_MESSAGE_SEVERITY Severity,
                                        D3D12_MESSAGE_ID ID, LPCSTR pDescription, void* pContext)
{
    LOG_DEBUG("Category: {}, Severity: {}, ID: {}, Message: {}", (UINT) Category, (UINT) Severity, (UINT) ID,
              pDescription);
}
#endif

static HRESULT hkD3D12CreateDevice(IDXGIAdapter* pAdapter, D3D_FEATURE_LEVEL MinimumFeatureLevel, REFIID riid,
                                   void** ppDevice)
{
    LOG_DEBUG("Adapter: {:X}, Level: {:X}, Caller: {}", (size_t) pAdapter, (UINT) MinimumFeatureLevel,
              Util::WhoIsTheCaller(_ReturnAddress()));

#ifdef ENABLE_DEBUG_LAYER_DX12
    LOG_WARN("Debug layers active!");
    if (debugController == nullptr && D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)) == S_OK)
    {
        debugController->EnableDebugLayer();

#ifdef ENABLE_GPU_VALIDATION
        LOG_WARN("GPU Based Validation active!");
        debugController->SetEnableGPUBasedValidation(TRUE);
#endif
    }
#endif

    DXGI_ADAPTER_DESC desc {};
    std::wstring szName;
    if (pAdapter != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        State::Instance().skipSpoofing = true;
        if (pAdapter->GetDesc(&desc) == S_OK)
        {
            szName = desc.Description;
            LOG_INFO("Adapter Desc: {}", wstring_to_string(szName));
        }
        State::Instance().skipSpoofing = false;
    }

    auto minLevel = MinimumFeatureLevel;
    if (Config::Instance()->SpoofFeatureLevel.value_or_default() && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_INFO("Forcing feature level 0xb000 for new device");
        minLevel = D3D_FEATURE_LEVEL_11_0;
    }

    if (desc.VendorId == VendorId::Intel)
        State::Instance().skipSpoofing = true;

    auto result = o_D3D12CreateDevice(pAdapter, minLevel, riid, ppDevice);

    if (desc.VendorId == VendorId::Intel)
        State::Instance().skipSpoofing = false;

    LOG_DEBUG("o_D3D12CreateDevice result: {:X}", (UINT) result);

    if (result == S_OK && ppDevice != nullptr && MinimumFeatureLevel != D3D_FEATURE_LEVEL_1_0_CORE)
    {
        LOG_DEBUG("Device captured: {0:X}", (size_t) *ppDevice);
        State::Instance().currentD3D12Device = (ID3D12Device*) *ppDevice;

        if (szName.size() > 0)
            State::Instance().DeviceAdapterNames[*ppDevice] = wstring_to_string(szName);

        if (desc.VendorId == VendorId::Intel && Config::Instance()->UESpoofIntelAtomics64.value_or_default())
        {
            IGDExtProxy::EnableAtomicSupport(State::Instance().currentD3D12Device);
            _intelD3D12Device = State::Instance().currentD3D12Device;
            _intelD3D12DeviceRefTarget = _intelD3D12Device->AddRef();

            if (o_D3D12DeviceRelease == nullptr)
                _intelD3D12Device->Release();
            else
                o_D3D12DeviceRelease(_intelD3D12Device);
        }

        HookToDevice(State::Instance().currentD3D12Device);
        _d3d12Captured = true;

        State::Instance().d3d12Devices.push_back((ID3D12Device*) *ppDevice);

#ifdef ENABLE_DEBUG_LAYER_DX12
        if (infoQueue != nullptr)
            infoQueue->Release();

        if (infoQueue1 != nullptr)
            infoQueue1->Release();

        if (State::Instance().currentD3D12Device->QueryInterface(IID_PPV_ARGS(&infoQueue)) == S_OK)
        {
            LOG_DEBUG("infoQueue accuired");

            infoQueue->ClearRetrievalFilter();
            infoQueue->SetMuteDebugOutput(false);

            HRESULT res;
            res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
            // res = infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);

            if (infoQueue->QueryInterface(IID_PPV_ARGS(&infoQueue1)) == S_OK && infoQueue1 != nullptr)
            {
                LOG_DEBUG("infoQueue1 accuired, registering MessageCallback");
                res = infoQueue1->RegisterMessageCallback(D3D12DebugCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,
                                                          NULL, NULL);
            }
        }
#endif
    }

    LOG_DEBUG("final result: {:X}", (UINT) result);
    return result;
}

static HRESULT hkD3D12SerializeRootSignature(D3d12Proxy::D3D12_ROOT_SIGNATURE_DESC_L* pRootSignature,
                                             D3D_ROOT_SIGNATURE_VERSION Version, ID3DBlob** ppBlob,
                                             ID3DBlob** ppErrorBlob)
{
    if (pRootSignature != nullptr)
    {
        for (size_t i = 0; i < pRootSignature->NumStaticSamplers; i++)
        {
            auto samplerDesc = &pRootSignature->pStaticSamplers[i];

            if (Config::Instance()->MipmapBiasOverride.has_value())
            {
                if ((samplerDesc->MipLODBias < 0.0f && samplerDesc->MinLOD != samplerDesc->MaxLOD) ||
                    Config::Instance()->MipmapBiasOverrideAll.value_or_default())
                {
                    if (Config::Instance()->MipmapBiasOverride.has_value())
                    {
                        LOG_DEBUG("Overriding mipmap bias {0} -> {1}", samplerDesc->MipLODBias,
                                  Config::Instance()->MipmapBiasOverride.value());

                        if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                            samplerDesc->MipLODBias = Config::Instance()->MipmapBiasOverride.value();
                        else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias * Config::Instance()->MipmapBiasOverride.value();
                        else
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias + Config::Instance()->MipmapBiasOverride.value();
                    }

                    if (State::Instance().lastMipBiasMax < samplerDesc->MipLODBias)
                        State::Instance().lastMipBiasMax = samplerDesc->MipLODBias;

                    if (State::Instance().lastMipBias > samplerDesc->MipLODBias)
                        State::Instance().lastMipBias = samplerDesc->MipLODBias;
                }
            }

            if (Config::Instance()->AnisotropyOverride.has_value())
            {
                LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", samplerDesc->MaxAnisotropy,
                          Config::Instance()->AnisotropyOverride.value(), (UINT) samplerDesc->Filter);

                samplerDesc->Filter = UpgradeToAF(samplerDesc->Filter);
                samplerDesc->MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
            }
        }
    }

    return o_D3D12SerializeRootSignature(pRootSignature, Version, ppBlob, ppErrorBlob);
}

static HRESULT hkD3D12SerializeVersionedRootSignature(D3d12Proxy::D3D12_VERSIONED_ROOT_SIGNATURE_DESC_L* pRootSignature,
                                                      ID3DBlob** ppBlob, ID3DBlob** ppErrorBlob)
{
    if (pRootSignature != nullptr)
    {
        for (size_t i = 0; i < pRootSignature->Desc_1_0.NumStaticSamplers; i++)
        {
            auto samplerDesc = &pRootSignature->Desc_1_0.pStaticSamplers[i];

            if (Config::Instance()->MipmapBiasOverride.has_value())
            {
                if ((samplerDesc->MipLODBias < 0.0f && samplerDesc->MinLOD != samplerDesc->MaxLOD) ||
                    Config::Instance()->MipmapBiasOverrideAll.value_or_default())
                {
                    if (Config::Instance()->MipmapBiasOverride.has_value())
                    {
                        LOG_DEBUG("Overriding mipmap bias {0} -> {1}", samplerDesc->MipLODBias,
                                  Config::Instance()->MipmapBiasOverride.value());

                        if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                            samplerDesc->MipLODBias = Config::Instance()->MipmapBiasOverride.value();
                        else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias * Config::Instance()->MipmapBiasOverride.value();
                        else
                            samplerDesc->MipLODBias =
                                samplerDesc->MipLODBias + Config::Instance()->MipmapBiasOverride.value();
                    }

                    if (State::Instance().lastMipBiasMax < samplerDesc->MipLODBias)
                        State::Instance().lastMipBiasMax = samplerDesc->MipLODBias;

                    if (State::Instance().lastMipBias > samplerDesc->MipLODBias)
                        State::Instance().lastMipBias = samplerDesc->MipLODBias;
                }
            }

            if (Config::Instance()->AnisotropyOverride.has_value())
            {
                LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", samplerDesc->MaxAnisotropy,
                          Config::Instance()->AnisotropyOverride.value(), (UINT) samplerDesc->Filter);

                samplerDesc->Filter = UpgradeToAF(samplerDesc->Filter);
                samplerDesc->MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
            }
        }
    }

    return o_D3D12SerializeVersionedRootSignature(pRootSignature, ppBlob, ppErrorBlob);
}

static ULONG hkD3D12DeviceRelease(IUnknown* device)
{
    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && device == _intelD3D12Device)
    {
        auto refCount = device->AddRef();

        if (refCount == _intelD3D12DeviceRefTarget)
        {
            LOG_INFO("Destroying IGDExt context!");
            _intelD3D12Device = nullptr;
            IGDExtProxy::DestroyContext();
        }

        o_D3D12DeviceRelease(device);
    }

    auto result = o_D3D12DeviceRelease(device);

    return result;
}

static HRESULT hkCheckFeatureSupport(ID3D12Device* device, D3D12_FEATURE Feature, void* pFeatureSupportData,
                                     UINT FeatureSupportDataSize)
{
    auto result = o_CheckFeatureSupport(device, Feature, pFeatureSupportData, FeatureSupportDataSize);

    if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && Feature == D3D12_FEATURE_D3D12_OPTIONS9)
    {
        auto featureSupport = (D3D12_FEATURE_DATA_D3D12_OPTIONS9*) pFeatureSupportData;
        LOG_INFO("Spoofing AtomicInt64OnTypedResourceSupported");
        featureSupport->AtomicInt64OnTypedResourceSupported = true;
    }

    return result;
}

struct UE_D3D12_RESOURCE_DESC
{
    D3D12_RESOURCE_DIMENSION Dimension;
    UINT64 Alignment;
    UINT64 Width;
    UINT Height;
    UINT16 DepthOrArraySize;
    UINT16 MipLevels;
    DXGI_FORMAT Format;
    DXGI_SAMPLE_DESC SampleDesc;
    D3D12_TEXTURE_LAYOUT Layout;
    D3D12_RESOURCE_FLAGS Flags;

    // UE Part
    uint8_t PixelFormat { 0 };
    uint8_t UAVPixelFormat { 0 };
    bool bRequires64BitAtomicSupport : 1 = false;
    bool bReservedResource : 1 = false;
    bool bBackBuffer : 1 = false;
    bool bExternal : 1 = false;
};

static bool skipCommitedResource = false;

static HRESULT hkCreateCommittedResource(ID3D12Device* device, const D3D12_HEAP_PROPERTIES* pHeapProperties,
                                         D3D12_HEAP_FLAGS HeapFlags, D3D12_RESOURCE_DESC* pDesc,
                                         D3D12_RESOURCE_STATES InitialResourceState,
                                         const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riidResource,
                                         void** ppvResource)
{
    if (!skipCommitedResource)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipCommitedResource = true;
            auto result = IGDExtProxy::CreateCommitedResource(pHeapProperties, HeapFlags, pDesc, InitialResourceState,
                                                              pOptimizedClearValue, riidResource, ppvResource);
            skipCommitedResource = false;

            return result;
        }
    }

    return o_CreateCommittedResource(device, pHeapProperties, HeapFlags, pDesc, InitialResourceState,
                                     pOptimizedClearValue, riidResource, ppvResource);
}

static bool skipPlacedResource = false;

static HRESULT hkCreatePlacedResource(ID3D12Device* device, ID3D12Heap* pHeap, UINT64 HeapOffset,
                                      D3D12_RESOURCE_DESC* pDesc, D3D12_RESOURCE_STATES InitialState,
                                      const D3D12_CLEAR_VALUE* pOptimizedClearValue, REFIID riid, void** ppvResource)
{
    if (!skipPlacedResource)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pDesc);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipPlacedResource = true;
            auto result = IGDExtProxy::CreatePlacedResource(pHeap, HeapOffset, pDesc, InitialState,
                                                            pOptimizedClearValue, riid, ppvResource);
            skipPlacedResource = false;

            return result;
        }
    }

    return o_CreatePlacedResource(device, pHeap, HeapOffset, pDesc, InitialState, pOptimizedClearValue, riid,
                                  ppvResource);
}

static bool skipGetResourceAllocationInfo = false;

static D3D12_RESOURCE_ALLOCATION_INFO hkGetResourceAllocationInfo(ID3D12Device* device, UINT visibleMask,
                                                                  UINT numResourceDescs,
                                                                  D3D12_RESOURCE_DESC* pResourceDescs)
{
    if (State::Instance().currentD3D12Device != device)
        device = State::Instance().currentD3D12Device;

    if (!skipGetResourceAllocationInfo)
    {
        auto ueDesc = reinterpret_cast<UE_D3D12_RESOURCE_DESC*>(pResourceDescs);

        if (Config::Instance()->UESpoofIntelAtomics64.value_or_default() && ueDesc != nullptr &&
            ueDesc->bRequires64BitAtomicSupport)
        {
            skipGetResourceAllocationInfo = true;
            auto result = IGDExtProxy::GetResourceAllocationInfo(visibleMask, numResourceDescs, pResourceDescs);
            skipGetResourceAllocationInfo = false;

            return result;
        }
    }

    return o_GetResourceAllocationInfo(device, visibleMask, numResourceDescs, pResourceDescs);
}

static void hkCreateSampler(ID3D12Device* device, const D3D12_SAMPLER_DESC* pDesc,
                            D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor)
{
    if (pDesc == nullptr || device == nullptr)
        return;

    D3D12_SAMPLER_DESC newDesc {};

    newDesc.AddressU = pDesc->AddressU;
    newDesc.AddressV = pDesc->AddressV;
    newDesc.AddressW = pDesc->AddressW;
    newDesc.BorderColor[0] = pDesc->BorderColor[0];
    newDesc.BorderColor[1] = pDesc->BorderColor[1];
    newDesc.BorderColor[2] = pDesc->BorderColor[2];
    newDesc.BorderColor[3] = pDesc->BorderColor[3];
    newDesc.ComparisonFunc = pDesc->ComparisonFunc;

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", pDesc->MaxAnisotropy,
                  Config::Instance()->AnisotropyOverride.value(), (UINT) newDesc.Filter);

        newDesc.Filter = UpgradeToAF(pDesc->Filter);
        newDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
    }
    else
    {
        newDesc.Filter = pDesc->Filter;
        newDesc.MaxAnisotropy = pDesc->MaxAnisotropy;
    }

    newDesc.MaxLOD = pDesc->MaxLOD;
    newDesc.MinLOD = pDesc->MinLOD;
    newDesc.MipLODBias = pDesc->MipLODBias;

    if ((newDesc.MipLODBias < 0.0f && newDesc.MinLOD != newDesc.MaxLOD) ||
        Config::Instance()->MipmapBiasOverrideAll.value_or_default())
    {
        if (Config::Instance()->MipmapBiasOverride.has_value())
        {
            LOG_DEBUG("Overriding mipmap bias {0} -> {1}", pDesc->MipLODBias,
                      Config::Instance()->MipmapBiasOverride.value());

            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                newDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                newDesc.MipLODBias = newDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
            else
                newDesc.MipLODBias = newDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();
        }

        if (State::Instance().lastMipBiasMax < newDesc.MipLODBias)
            State::Instance().lastMipBiasMax = newDesc.MipLODBias;

        if (State::Instance().lastMipBias > newDesc.MipLODBias)
            State::Instance().lastMipBias = newDesc.MipLODBias;
    }

    return o_CreateSampler(device, &newDesc, DestDescriptor);
}

static HRESULT hkCreateSamplerState(ID3D11Device* This, const D3D11_SAMPLER_DESC* pSamplerDesc,
                                    ID3D11SamplerState** ppSamplerState)
{
    if (pSamplerDesc == nullptr || This == nullptr)
        return E_INVALIDARG;

    if (_d3d12Captured)
        return o_CreateSamplerState(This, pSamplerDesc, ppSamplerState);

    LOG_FUNC();

    D3D11_SAMPLER_DESC newDesc {};

    newDesc.AddressU = pSamplerDesc->AddressU;
    newDesc.AddressV = pSamplerDesc->AddressV;
    newDesc.AddressW = pSamplerDesc->AddressW;
    newDesc.ComparisonFunc = pSamplerDesc->ComparisonFunc;
    newDesc.BorderColor[0] = pSamplerDesc->BorderColor[0];
    newDesc.BorderColor[1] = pSamplerDesc->BorderColor[1];
    newDesc.BorderColor[2] = pSamplerDesc->BorderColor[2];
    newDesc.BorderColor[3] = pSamplerDesc->BorderColor[3];
    newDesc.MinLOD = pSamplerDesc->MinLOD;
    newDesc.MaxLOD = pSamplerDesc->MaxLOD;

    if (Config::Instance()->AnisotropyOverride.has_value())
    {
        LOG_DEBUG("Overriding {2:X} to anisotropic filtering {0} -> {1}", pSamplerDesc->MaxAnisotropy,
                  Config::Instance()->AnisotropyOverride.value(), (UINT) newDesc.Filter);

        newDesc.Filter = UpgradeToAF(pSamplerDesc->Filter);
        newDesc.MaxAnisotropy = Config::Instance()->AnisotropyOverride.value();
    }
    else
    {
        newDesc.Filter = pSamplerDesc->Filter;
        newDesc.MaxAnisotropy = pSamplerDesc->MaxAnisotropy;
    }

    newDesc.MipLODBias = pSamplerDesc->MipLODBias;

    if ((newDesc.MipLODBias < 0.0f && newDesc.MinLOD != newDesc.MaxLOD) ||
        Config::Instance()->MipmapBiasOverrideAll.value_or_default())
    {
        if (Config::Instance()->MipmapBiasOverride.has_value())
        {
            LOG_DEBUG("Overriding mipmap bias {0} -> {1}", pSamplerDesc->MipLODBias,
                      Config::Instance()->MipmapBiasOverride.value());

            if (Config::Instance()->MipmapBiasFixedOverride.value_or_default())
                newDesc.MipLODBias = Config::Instance()->MipmapBiasOverride.value();
            else if (Config::Instance()->MipmapBiasScaleOverride.value_or_default())
                newDesc.MipLODBias = newDesc.MipLODBias * Config::Instance()->MipmapBiasOverride.value();
            else
                newDesc.MipLODBias = newDesc.MipLODBias + Config::Instance()->MipmapBiasOverride.value();
        }

        if (State::Instance().lastMipBiasMax < newDesc.MipLODBias)
            State::Instance().lastMipBiasMax = newDesc.MipLODBias;

        if (State::Instance().lastMipBias > newDesc.MipLODBias)
            State::Instance().lastMipBias = newDesc.MipLODBias;
    }

    return o_CreateSamplerState(This, &newDesc, ppSamplerState);
}

#pragma endregion

#pragma region Public hook methods

void D3D1XHooks::HookDx12()
{
    if (o_D3D12CreateDevice != nullptr)
        return;

    LOG_DEBUG("");

    o_D3D12CreateDevice = D3d12Proxy::Hook_D3D12CreateDevice(hkD3D12CreateDevice);
    o_D3D12SerializeRootSignature = D3d12Proxy::Hook_D3D12SerializeRootSignature(hkD3D12SerializeRootSignature);
    o_D3D12SerializeVersionedRootSignature =
        D3d12Proxy::Hook_D3D12SerializeVersionedRootSignature(hkD3D12SerializeVersionedRootSignature);
}

void D3D1XHooks::HookDx11(HMODULE dx11Module)
{
    if (o_D3D11CreateDevice != nullptr)
        return;

    LOG_DEBUG("");

    o_D3D11CreateDevice = (PFN_D3D11_CREATE_DEVICE) KernelBaseProxy::GetProcAddress_()(dx11Module, "D3D11CreateDevice");
    o_D3D11CreateDeviceAndSwapChain = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN) KernelBaseProxy::GetProcAddress_()(
        dx11Module, "D3D11CreateDeviceAndSwapChain");
    o_D3D11On12CreateDevice =
        (PFN_D3D11ON12_CREATE_DEVICE) KernelBaseProxy::GetProcAddress_()(dx11Module, "D3D11On12CreateDevice");

    if (o_D3D11CreateDevice != nullptr || o_D3D11On12CreateDevice != nullptr ||
        o_D3D11CreateDeviceAndSwapChain != nullptr)
    {
        LOG_DEBUG("Hooking D3D11CreateDevice methods");

        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        if (o_D3D11CreateDevice != nullptr)
            DetourAttach(&(PVOID&) o_D3D11CreateDevice, hkD3D11CreateDevice);

        if (o_D3D11On12CreateDevice != nullptr)
            DetourAttach(&(PVOID&) o_D3D11On12CreateDevice, hkD3D11On12CreateDevice);

        if (o_D3D11CreateDeviceAndSwapChain != nullptr)
            DetourAttach(&(PVOID&) o_D3D11CreateDeviceAndSwapChain, hkD3D11CreateDeviceAndSwapChain);

        DetourTransactionCommit();
    }
}

void D3D1XHooks::ReleaseDx12SwapChain(HWND hwnd)
{
    State::Instance().currentSwapchain = nullptr;

    auto fg = State::Instance().currentFG;
    if (fg == nullptr)
        return;

    // Skip if it's already being released
    if (fg->Mutex.getOwner() == 1)
        return;

    if (fg->SwapchainContext() != nullptr)
        fg->ReleaseSwapchain(hwnd);
}

void D3D1XHooks::UnHookDx()
{
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    if (o_D3D11CreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_D3D11CreateDevice, hkD3D11CreateDevice);
        o_D3D11CreateDevice = nullptr;
    }

    if (o_D3D11On12CreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_D3D11On12CreateDevice, hkD3D11On12CreateDevice);
        o_D3D11On12CreateDevice = nullptr;
    }

    if (o_D3D12CreateDevice != nullptr)
    {
        DetourDetach(&(PVOID&) o_D3D12CreateDevice, hkD3D12CreateDevice);
        o_D3D12CreateDevice = nullptr;
    }

    if (o_CreateSampler != nullptr)
    {
        DetourDetach(&(PVOID&) o_CreateSampler, hkCreateSampler);
        o_CreateSampler = nullptr;
    }

    DetourTransactionCommit();
}

#pragma endregion
