#include <pch.h>
#include "IFeature_Dx11wDx12.h"

#include <Config.h>

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <misc/IdentifyGpu.h>

#define ASSIGN_DESC(dest, src)                                                                                         \
    dest.Width = src.Width;                                                                                            \
    dest.Height = src.Height;                                                                                          \
    dest.Format = src.Format;                                                                                          \
    dest.BindFlags = src.BindFlags;                                                                                    \
    dest.MiscFlags = src.MiscFlags;

void IFeature_Dx11wDx12::ResourceBarrier(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource,
                                         D3D12_RESOURCE_STATES beforeState, D3D12_RESOURCE_STATES afterState)
{
    if (beforeState == afterState)
        return;

    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = resource;
    barrier.Transition.StateBefore = beforeState;
    barrier.Transition.StateAfter = afterState;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
}

bool IFeature_Dx11wDx12::CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource,
                                               bool InCopy, bool InDepth, bool InDontUseNTShared)
{
    ID3D11Texture2D* originalTexture = nullptr;
    D3D11_TEXTURE2D_DESC desc {};

    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));

    if (result != S_OK || originalTexture == nullptr)
        return false;

    originalTexture->GetDesc(&desc);

    // check shared nt handle usage later
    if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) && !(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) &&
        !InDontUseNTShared)
    {
        if (desc.Width != OutResource->Desc.Width || desc.Height != OutResource->Desc.Height ||
            desc.Format != OutResource->Desc.Format || desc.BindFlags != OutResource->Desc.BindFlags ||
            OutResource->SharedTexture == nullptr ||
            !(OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
        {
            if (OutResource->SharedTexture != nullptr)
            {
                OutResource->SharedTexture->Release();

                if (OutResource->Dx12Handle != NULL &&
                    (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
                    CloseHandle(OutResource->Dx12Handle);

                OutResource->Dx11Handle = NULL;
                OutResource->Dx12Handle = NULL;
            }

            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            desc.Usage = D3D11_USAGE_DEFAULT;

            ASSIGN_DESC(OutResource->Desc, desc);

            result = Dx11Device->CreateTexture2D(&desc, nullptr, &OutResource->SharedTexture);

            if (result != S_OK)
            {
                LOG_ERROR("CreateTexture2D error: {0:x}", result);
                return false;
            }

            IDXGIResource1* resource;

            result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                return false;
            }

            // Get shared handle
            DWORD access = DXGI_SHARED_RESOURCE_READ;

            if (!InCopy)
                access |= DXGI_SHARED_RESOURCE_WRITE;

            result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);

            if (result != S_OK)
            {
                LOG_ERROR("GetSharedHandle error: {0:x}", result);
                return false;
            }

            resource->Release();
        }

        if (InCopy && OutResource->SharedTexture != nullptr)
            Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
    }
    else if ((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) == 0 && InDontUseNTShared)
    {
        if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
        {
            if (DT == nullptr || DT.get() == nullptr)
                DT = std::make_unique<DepthTransfer_Dx11>("DT", Dx11Device);

            if (DT->Buffer() == nullptr)
                DT->CreateBufferResource(Dx11Device, InResource);

            if (DT->Dispatch(Dx11Device, Dx11DeviceContext, originalTexture, DT->Buffer()))
            {
                IDXGIResource1* resource = nullptr;
                result = DT->Buffer()->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK || resource == nullptr)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    return false;
                }

                // Get shared handle
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);

                if (result != S_OK)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    resource->Release();
                    return false;
                }

                resource->Release();
            }
        }
        else
        {
            if (desc.Width != OutResource->Desc.Width || desc.Height != OutResource->Desc.Height ||
                desc.Format != OutResource->Desc.Format || desc.BindFlags != OutResource->Desc.BindFlags ||
                OutResource->SharedTexture == nullptr ||
                (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
            {
                if (OutResource->SharedTexture != nullptr)
                {
                    OutResource->SharedTexture->Release();

                    if (OutResource->Dx12Handle != NULL &&
                        (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
                        CloseHandle(OutResource->Dx12Handle);

                    OutResource->Dx11Handle = NULL;
                    OutResource->Dx12Handle = NULL;
                }

                if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS)
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                ASSIGN_DESC(OutResource->Desc, desc);
                desc.Usage = D3D11_USAGE_DEFAULT;

                result = Dx11Device->CreateTexture2D(&desc, nullptr, &OutResource->SharedTexture);

                IDXGIResource1* resource;
                result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    return false;
                }

                // Get shared handle
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);

                if (result != S_OK)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    resource->Release();
                    return false;
                }

                resource->Release();
            }

            if (InCopy && OutResource->SharedTexture != nullptr)
                Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
        }
    }
    else
    {
        if (OutResource->SharedTexture != InResource)
        {
            IDXGIResource1* resource;

            result = originalTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK || resource == nullptr)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                return false;
            }

            // Get shared handle
            if ((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) != 0 &&
                (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0)
            {
                DWORD access = DXGI_SHARED_RESOURCE_READ;

                if (!InCopy)
                    access |= DXGI_SHARED_RESOURCE_WRITE;

                result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);
            }
            else
            {
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);
            }

            if (result != S_OK)
            {
                LOG_ERROR("GetSharedHandle error: {0:x}", result);
                return false;
            }

            resource->Release();

            OutResource->SharedTexture = (ID3D11Texture2D*) InResource;
        }
    }

    originalTexture->Release();
    return true;
}

void IFeature_Dx11wDx12::ReleaseSharedResources()
{
    SAFE_RELEASE(dx11Color.SharedTexture);
    SAFE_RELEASE(dx11Mv.SharedTexture);
    SAFE_RELEASE(dx11Out.SharedTexture);
    SAFE_RELEASE(dx11Depth.SharedTexture);
    SAFE_RELEASE(dx11Reactive.SharedTexture);
    SAFE_RELEASE(dx11Exp.SharedTexture);
    SAFE_RELEASE(dx11Color.Dx12Resource);
    SAFE_RELEASE(dx11Mv.Dx12Resource);
    SAFE_RELEASE(dx11Out.Dx12Resource);
    SAFE_RELEASE(dx11Depth.Dx12Resource);
    SAFE_RELEASE(dx11Reactive.Dx12Resource);
    SAFE_RELEASE(dx11Exp.Dx12Resource);

    ReleaseSyncResources();

    for (int i = 0; i < DX11WDX12_NUM_OF_BUFFERS; i++)
    {
        SAFE_RELEASE(Dx12CommandList[i]);
        SAFE_RELEASE(Dx12CommandAllocator[i]);
    }

    SAFE_RELEASE(Dx12CommandQueue);
    SAFE_RELEASE(Dx12Fence);

    SAFE_CLOSE_HANDLE(Dx12FenceEvent);

    // SAFE_RELEASE(Dx12Device);
}

void IFeature_Dx11wDx12::ReleaseSyncResources()
{
    SAFE_RELEASE(dx11FenceTextureCopy);
    SAFE_RELEASE(dx12FenceTextureCopy);

    SAFE_CLOSE_HANDLE(dx11SHForTextureCopy);
}

HRESULT IFeature_Dx11wDx12::CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel)
{
    LOG_FUNC();

    ScopedSkipSpoofing skipSpoofing {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    HRESULT result;

    if (State::Instance().currentD3D12Device == nullptr || (_localDx11on12Device == nullptr))
    {
        IDXGIFactory2* factory = nullptr;

        if (DxgiProxy::Module() == nullptr)
            result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        else
            result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

        if (result != S_OK)
        {
            LOG_ERROR("Can't create factory: {0:x}", result);
            return result;
        }

        IDXGIAdapter* hwAdapter = nullptr;
        IdentifyGpu::getHardwareAdapter(factory, &hwAdapter, InFeatureLevel);

        if (hwAdapter == nullptr)
            LOG_WARN("Can't get hwAdapter, will try nullptr!");

        if (D3d12Proxy::Module() == nullptr)
            result = D3D12CreateDevice(hwAdapter, InFeatureLevel, IID_PPV_ARGS(&_localDx11on12Device));
        else
            result = D3d12Proxy::D3D12CreateDevice_()(hwAdapter, InFeatureLevel, IID_PPV_ARGS(&_localDx11on12Device));

        if (result != S_OK)
        {
            LOG_ERROR("Can't create device: {:X}", (UINT) result);
            return result;
        }

        _dx11on12Device = _localDx11on12Device;

        if (hwAdapter != nullptr)
        {
            DXGI_ADAPTER_DESC desc {};
            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            if (hwAdapter->GetDesc(&desc) == S_OK && !IsEqualLUID(desc.AdapterLuid, primaryGpu.luid))
            {
                LOG_WARN("D3D12Device created with non-primary GPU");
            }
        }
    }
    else
    {
        // If there is local d3d12 device always use it
        if (_localDx11on12Device != nullptr)
        {
            LOG_DEBUG("Using _localDx11on12Device");
            _dx11on12Device = _localDx11on12Device;
        }
        else
        {
            LOG_DEBUG("Using currentD3D12Device");
            _dx11on12Device = State::Instance().currentD3D12Device;
        }
    }

    if (Dx12CommandQueue == nullptr)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = Dx12CommandListType;

        // CreateCommandQueue
        result = _dx11on12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&Dx12CommandQueue));

        if (result != S_OK || Dx12CommandQueue == nullptr)
        {
            LOG_DEBUG("CreateCommandQueue result: {0:x}", result);
            return E_NOINTERFACE;
        }
    }

    for (size_t i = 0; i < 2; i++)
    {
        if (Dx12CommandAllocator[i] == nullptr)
        {
            result =
                _dx11on12Device->CreateCommandAllocator(Dx12CommandListType, IID_PPV_ARGS(&Dx12CommandAllocator[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocator error: {:X}", result);
                return E_NOINTERFACE;
            }
        }

        if (Dx12CommandList[i] == nullptr && Dx12CommandAllocator[i] != nullptr)
        {
            // CreateCommandList
            result = _dx11on12Device->CreateCommandList(0, Dx12CommandListType, Dx12CommandAllocator[i], nullptr,
                                                        IID_PPV_ARGS(&Dx12CommandList[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandList error: {:X}", result);
                return E_NOINTERFACE;
            }

            Dx12CommandList[i]->Close();
        }
    }

    if (Dx12Fence == nullptr)
    {
        result = _dx11on12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&Dx12Fence));

        if (result != S_OK)
        {
            LOG_ERROR("CreateFence error: {0:X}", result);
            return E_NOINTERFACE;
        }

        Dx12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (Dx12FenceEvent == nullptr)
        {
            LOG_ERROR("CreateEvent error!");
            return E_NOINTERFACE;
        }
    }

    return S_OK;
}

bool IFeature_Dx11wDx12::ProcessDx11Textures(const NVSDK_NGX_Parameter* InParameters)
{
    HRESULT result;

    // Wait for last frame
    if (Dx12Fence->GetCompletedValue() < _frameCount)
    {
        result = Dx12Fence->SetEventOnCompletion(_frameCount, Dx12FenceEvent);
        if (result != S_OK)
        {
            LOG_ERROR("SetEventOnCompletion error: {:X}", (UINT) result);
            return false;
        }

        WaitForSingleObject(Dx12FenceEvent, INFINITE);
    }

    auto frame = _frameCount % DX11WDX12_NUM_OF_BUFFERS;

    result = Dx12CommandAllocator[frame]->Reset();
    if (result != S_OK)
    {
        LOG_ERROR("CommandAllocator Reset error: {:X}", (UINT) result);
        return false;
    }

    result = Dx12CommandList[frame]->Reset(Dx12CommandAllocator[frame], nullptr);
    if (result != S_OK)
    {
        LOG_ERROR("CommandList Reset error: {:X}", (UINT) result);
        return false;
    }

    auto dontUseNTS = Config::Instance()->DontUseNTShared.value_or_default();

#pragma region Texture copies

    ID3D11Resource* paramColor = nullptr;
    ID3D11Resource* paramMv = nullptr;
    ID3D11Resource* paramDepth = nullptr;
    ID3D11Resource* paramExposure = nullptr;
    ID3D11Resource* paramReactiveMask = nullptr;

    if (InParameters->Get(NVSDK_NGX_Parameter_Color, &paramColor) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Color, (void**) &paramColor);

    if (paramColor)
    {
        LOG_DEBUG("Color exist..");
        if (CopyTextureFrom11To12(paramColor, &dx11Color, true, false, dontUseNTS) == false)
            return false;
    }
    else
    {
        LOG_ERROR("Color not exist!!");
        return false;
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, &paramMv) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_MotionVectors, (void**) &paramMv);

    if (paramMv)
    {
        LOG_DEBUG("MotionVectors exist..");
        if (CopyTextureFrom11To12(paramMv, &dx11Mv, true, false, dontUseNTS) == false)
            return false;
    }
    else
    {
        LOG_ERROR("MotionVectors not exist!!");
        return false;
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_Output, &paramOutput[frame]) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Output, (void**) &paramOutput[frame]);

    if (paramOutput[frame])
    {
        LOG_DEBUG("Output exist..");
        if (CopyTextureFrom11To12(paramOutput[frame], &dx11Out, false, false, dontUseNTS) == false)
            return false;
    }
    else
    {
        LOG_ERROR("Output not exist!!");
        return false;
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_Depth, &paramDepth) != NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_Depth, (void**) &paramDepth);

    if (paramDepth)
    {
        LOG_DEBUG("Depth exist..");

        if (CopyTextureFrom11To12(paramDepth, &dx11Depth, true, true, true) == false)
            return false;
    }
    else
        LOG_ERROR("IFeature_Dx11wDx12::Evaluate Depth not exist!!");

    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else
    {
        if (InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, &paramExposure) != NVSDK_NGX_Result_Success)
            InParameters->Get(NVSDK_NGX_Parameter_ExposureTexture, (void**) &paramExposure);

        if (paramExposure)
        {
            LOG_DEBUG("ExposureTexture exist..");

            if (CopyTextureFrom11To12(paramExposure, &dx11Exp, true, false, dontUseNTS) == false)
                return false;
        }
        else
        {
            LOG_WARN("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().autoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
        }
    }

    if (InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask) !=
        NVSDK_NGX_Result_Success)
        InParameters->Get(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void**) &paramReactiveMask);

    if (!Config::Instance()->DisableReactiveMask.value_or(paramReactiveMask == nullptr))
    {
        if (paramReactiveMask)
        {
            Config::Instance()->DisableReactiveMask.set_volatile_value(false);
            LOG_DEBUG("Input Bias mask exist..");

            if (CopyTextureFrom11To12(paramReactiveMask, &dx11Reactive, true, false, dontUseNTS) == false)
                return false;
        }
        // This is only needed for XeSS
        else if (Config::Instance()->Dx11Upscaler.value_or_default() == Upscaler::XeSS)
        {
            LOG_WARN("bias mask not exist and it's enabled in config, it may cause problems!!");
            Config::Instance()->DisableReactiveMask.set_volatile_value(true);
            State::Instance().changeBackend[Handle()->Id] = true;
        }
    }
    else
        LOG_DEBUG("DisableReactiveMask enabled!");

#pragma endregion

    {
        if (dx11FenceTextureCopy == nullptr)
        {
            result = Dx11Device->CreateFence(0, D3D11_FENCE_FLAG_SHARED, IID_PPV_ARGS(&dx11FenceTextureCopy));

            if (result != S_OK)
            {
                LOG_ERROR("Can't create dx11FenceTextureCopy {0:x}", result);
                return false;
            }

            LOG_INFO("dx11FenceTextureCopy created successfully!");
        }

        if (dx11SHForTextureCopy == nullptr)
        {
            result = dx11FenceTextureCopy->CreateSharedHandle(nullptr, GENERIC_ALL, nullptr, &dx11SHForTextureCopy);

            if (result != S_OK)
            {
                LOG_ERROR("Can't create sharedhandle for dx11FenceTextureCopy {:X}", (UINT) result);
                return false;
            }

            result = _dx11on12Device->OpenSharedHandle(dx11SHForTextureCopy, IID_PPV_ARGS(&dx12FenceTextureCopy));

            if (result != S_OK)
            {
                LOG_ERROR("Can't create open sharedhandle for dx12FenceTextureCopy {:X}", (UINT) result);
                return false;
            }

            LOG_INFO("dx12FenceTextureCopy created successfully from shared handle!");
        }

        // Fence
        LOG_DEBUG("Dx11 Signal & Dx12 Wait!");

        result = Dx11DeviceContext->Signal(dx11FenceTextureCopy, _fenceValue);

        if (result != S_OK)
        {
            LOG_ERROR("Dx11DeviceContext->Signal(dx11FenceTextureCopy, {}) : {:X}!", _fenceValue, (UINT) result);
            return false;
        }

        Dx11DeviceContext->Flush();

        // Gpu Sync
        result = Dx12CommandQueue->Wait(dx12FenceTextureCopy, _fenceValue);
        _fenceValue++;

        if (result != S_OK)
        {
            LOG_ERROR("Dx12CommandQueue->Wait(dx12fence_1, {}) : {:X}!", _fenceValue, result);
            return false;
        }
    }

#pragma region shared handles

    LOG_DEBUG("SharedHandles start!");

    if (paramColor && dx11Color.Dx12Handle != dx11Color.Dx11Handle)
    {
        SAFE_CLOSE_HANDLE(dx11Color.Dx12Handle);

        result = _dx11on12Device->OpenSharedHandle(dx11Color.Dx11Handle, IID_PPV_ARGS(&dx11Color.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("Color OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Color.Dx12Handle = dx11Color.Dx11Handle;
    }

    if (paramMv && dx11Mv.Dx12Handle != dx11Mv.Dx11Handle)
    {
        SAFE_CLOSE_HANDLE(dx11Mv.Dx12Handle);

        result = _dx11on12Device->OpenSharedHandle(dx11Mv.Dx11Handle, IID_PPV_ARGS(&dx11Mv.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("MotionVectors OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Mv.Dx11Handle = dx11Mv.Dx12Handle;
    }

    if (paramOutput[frame] && dx11Out.Dx12Handle != dx11Out.Dx11Handle)
    {
        SAFE_CLOSE_HANDLE(dx11Out.Dx12Handle);

        result = _dx11on12Device->OpenSharedHandle(dx11Out.Dx11Handle, IID_PPV_ARGS(&dx11Out.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("Output OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Out.Dx12Handle = dx11Out.Dx11Handle;
    }

    if (paramDepth && dx11Depth.Dx12Handle != dx11Depth.Dx11Handle)
    {
        SAFE_CLOSE_HANDLE(dx11Depth.Dx12Handle);

        result = _dx11on12Device->OpenSharedHandle(dx11Depth.Dx11Handle, IID_PPV_ARGS(&dx11Depth.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("Depth OpenSharedHandle error: {0:x}", result);
            return false;
        }

        auto desc = dx11Depth.Dx12Resource->GetDesc();

        dx11Depth.Dx12Handle = dx11Depth.Dx11Handle;
    }

    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else if (paramExposure && dx11Exp.Dx12Handle != dx11Exp.Dx11Handle)
    {
        SAFE_CLOSE_HANDLE(dx11Exp.Dx12Handle);

        result = _dx11on12Device->OpenSharedHandle(dx11Exp.Dx11Handle, IID_PPV_ARGS(&dx11Exp.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("ExposureTexture OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Exp.Dx12Handle = dx11Exp.Dx11Handle;
    }

    if (!Config::Instance()->DisableReactiveMask.value_or(false) && paramReactiveMask &&
        dx11Reactive.Dx12Handle != dx11Reactive.Dx11Handle)
    {
        SAFE_CLOSE_HANDLE(dx11Reactive.Dx12Handle);

        result = _dx11on12Device->OpenSharedHandle(dx11Reactive.Dx11Handle, IID_PPV_ARGS(&dx11Reactive.Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("TransparencyMask OpenSharedHandle error: {0:x}", result);
            return false;
        }

        dx11Reactive.Dx12Handle = dx11Reactive.Dx11Handle;
    }

#pragma endregion

    return true;
}

bool IFeature_Dx11wDx12::CopyBackOutput()
{
    // Fence ones
    {
        // wait for fsr on dx12
        Dx11DeviceContext->Wait(dx11FenceTextureCopy, _fenceValue);
        _fenceValue++;

        auto frame = _frameCount % DX11WDX12_NUM_OF_BUFFERS;

        // Copy Back
        Dx11DeviceContext->CopyResource(paramOutput[frame], dx11Out.SharedTexture);
    }

    return true;
}

bool IFeature_Dx11wDx12::Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;
    DeviceContext = InContext;

    if (!BaseInit(Device, InContext, InParameters))
    {
        LOG_DEBUG("BaseInit failed!");
        return false;
    }

    // Non-DLSS upscalers don't use the cmdList during Init
    // We have more than one cmdList so unsure how that would even work
    SetInit(dx12Feature->Init(_dx11on12Device, Dx12CommandList[0], InParameters));

    return IsInited();
}

bool IFeature_Dx11wDx12::Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    auto& cfg = *Config::Instance();
    const auto& ngxParams = *InParameters;

    if (!IsInited())
        return false;

    ID3D11DeviceContext4* dc;
    auto result = InDeviceContext->QueryInterface(IID_PPV_ARGS(&dc));

    if (result != S_OK)
    {
        LOG_ERROR("QueryInterface error: {0:x}", result);
        return false;
    }

    if (dc != Dx11DeviceContext)
    {
        LOG_WARN("Dx11DeviceContext changed!");
        ReleaseSharedResources();
        Dx11DeviceContext = dc;
    }

    if (dc != nullptr)
        dc->Release();

    auto frame = _frameCount % DX11WDX12_NUM_OF_BUFFERS;
    auto cmdList = Dx12CommandList[frame];

    bool dx12EvalResult = false;
    do
    {
        if (!ProcessDx11Textures(InParameters))
        {
            LOG_ERROR("Can't process Dx11 textures!");
            break;
        }

        if (State::Instance().changeBackend[Handle()->Id])
        {
            break;
        }

        InParameters->Set(NVSDK_NGX_Parameter_Color, (void*) dx11Color.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_MotionVectors, (void*) dx11Mv.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_Output, (void*) dx11Out.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_Depth, (void*) dx11Depth.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_ExposureTexture, (void*) dx11Exp.Dx12Resource);
        InParameters->Set(NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, (void*) dx11Reactive.Dx12Resource);

        LOG_DEBUG("Dispatch!!");
        dx12EvalResult = dx12Feature->Evaluate(cmdList, InParameters);

        // Should we restore the resources in the params to DX11 ???

    } while (false);

    if (dx12EvalResult)
    {
        cmdList->Close();
        ID3D12CommandList* ppCommandLists[] = { cmdList };
        Dx12CommandQueue->ExecuteCommandLists(1, ppCommandLists);
        Dx12CommandQueue->Signal(dx12FenceTextureCopy, _fenceValue);
    }

    auto evalResult = false;

    do
    {
        if (!dx12EvalResult)
            break;

        if (!CopyBackOutput())
        {
            LOG_ERROR("Can't copy output texture back!");
            break;
        }

        evalResult = true;

    } while (false);

    _frameCount++;
    Dx12CommandQueue->Signal(Dx12Fence, _frameCount);

    return evalResult;
}

bool IFeature_Dx11wDx12::BaseInit(ID3D11Device* InDevice, ID3D11DeviceContext* InContext,
                                  NVSDK_NGX_Parameter* InParameters)
{
    LOG_FUNC();

    if (IsInited())
        return true;

    Device = InDevice;
    DeviceContext = InContext;

    if (!InContext)
    {
        LOG_ERROR("context is null!");
        return false;
    }

    auto contextResult = InContext->QueryInterface(IID_PPV_ARGS(&Dx11DeviceContext));
    if (contextResult != S_OK)
    {
        LOG_ERROR("QueryInterface ID3D11DeviceContext4 result: {0:x}", contextResult);
        return false;
    }
    else
    {
        Dx11DeviceContext->Release();
    }

    if (!InDevice)
        Dx11DeviceContext->GetDevice(&InDevice);

    auto dx11DeviceResult = InDevice->QueryInterface(IID_PPV_ARGS(&Dx11Device));

    if (dx11DeviceResult != S_OK)
    {
        LOG_ERROR("QueryInterface ID3D11Device5 result: {0:x}", dx11DeviceResult);
        return false;
    }
    else
    {
        Dx11Device->Release();
    }

    auto fl = Dx11Device->GetFeatureLevel();
    auto result = CreateDx12Device(fl);

    if (result != S_OK || _dx11on12Device == nullptr)
    {
        LOG_ERROR("QueryInterface Dx12Device result: {0:x}", result);
        return false;
    }

    return true;
}

IFeature_Dx11wDx12::IFeature_Dx11wDx12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters)
    : IFeature(InHandleId, InParameters), IFeature_Dx11(InHandleId, InParameters)
{
}

IFeature_Dx11wDx12::~IFeature_Dx11wDx12()
{
    if (State::Instance().isShuttingDown)
        return;

    ReleaseSharedResources();

    DT.reset();
}
