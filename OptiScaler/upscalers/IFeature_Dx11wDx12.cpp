#include <pch.h>
#include "IFeature_Dx11wDx12.h"

#include <Config.h>

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>
#include <misc/IdentifyGpu.h>

#include <with_dx12/with_dx12.h>

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

bool IFeature_Dx11wDx12::CreateD3D12Objects()
{
    HRESULT result;

    for (size_t i = 0; i < 2; i++)
    {
        if (Dx12CommandAllocator[i] == nullptr)
        {
            result =
                _dx11on12Device->CreateCommandAllocator(Dx12CommandListType, IID_PPV_ARGS(&Dx12CommandAllocator[i]));

            if (result != S_OK)
            {
                LOG_ERROR("CreateCommandAllocator error: {:X}", result);
                return false;
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
                return false;
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
            return false;
        }

        Dx12FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        if (Dx12FenceEvent == nullptr)
        {
            LOG_ERROR("CreateEvent error!");
            return false;
        }
    }

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

    SAFE_RELEASE(Dx12CommandList[0]);
    SAFE_RELEASE(Dx12CommandList[1]);
    SAFE_RELEASE(Dx12CommandAllocator[0]);
    SAFE_RELEASE(Dx12CommandAllocator[1]);
    SAFE_RELEASE(Dx12Fence);

    if (Dx12FenceEvent)
    {
        CloseHandle(Dx12FenceEvent);
        Dx12FenceEvent = nullptr;
    }

    // SAFE_RELEASE(Dx12Device);
}

void IFeature_Dx11wDx12::ReleaseSyncResources()
{
    SAFE_RELEASE(dx11FenceTextureCopy);
    SAFE_RELEASE(dx12FenceTextureCopy);

    if (dx11SHForTextureCopy != NULL)
    {
        CloseHandle(dx11SHForTextureCopy);
        dx11SHForTextureCopy = NULL;
    }
}

bool GetD3D11ResourceFromParameter(std::string name, const NVSDK_NGX_Parameter* InParameters, const char* paramName,
                                   ID3D11Resource** outResource, Dx11WithDx12::D3D11_TEXTURE2D_RESOURCE_C* shared,
                                   bool copy, bool depth, bool useNtShared)
{

    if (InParameters->Get(paramName, outResource) != NVSDK_NGX_Result_Success)
        if (InParameters->Get(paramName, outResource) != NVSDK_NGX_Result_Success)
            return false;

    if (*outResource == nullptr)
    {
        LOG_ERROR("{} not exist!!", name);
        return false;
    }

    LOG_TRACE("{} exist..", name);

    if (!Dx11WithDx12::CopyTextureFrom11To12(*outResource, shared, copy, depth, useNtShared))
        return false;

    return true;
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

    if (!GetD3D11ResourceFromParameter("Color", InParameters, NVSDK_NGX_Parameter_Color, &paramColor, &dx11Color, true,
                                       false, dontUseNTS))
    {
        return false;
    }

    if (!GetD3D11ResourceFromParameter("MotionVectors", InParameters, NVSDK_NGX_Parameter_MotionVectors, &paramMv,
                                       &dx11Mv, true, false, dontUseNTS))
    {
        return false;
    }

    if (!GetD3D11ResourceFromParameter("Output", InParameters, NVSDK_NGX_Parameter_Output, &paramOutput[frame],
                                       &dx11Out, false, false, dontUseNTS))
    {
        return false;
    }

    if (!GetD3D11ResourceFromParameter("Depth", InParameters, NVSDK_NGX_Parameter_Depth, &paramDepth, &dx11Depth, true,
                                       true, dontUseNTS))
    {
        return false;
    }

    if (AutoExposure())
    {
        LOG_DEBUG("AutoExposure enabled!");
    }
    else
    {
        if (!GetD3D11ResourceFromParameter("Exposure", InParameters, NVSDK_NGX_Parameter_ExposureTexture,
                                           &paramExposure, &dx11Exp, true, false, dontUseNTS))
        {
            LOG_WARN("AutoExposure disabled but ExposureTexture is not exist, it may cause problems!!");
            State::Instance().autoExposure = true;
            State::Instance().changeBackend[Handle()->Id] = true;
            return true;
        }
    }

    if (Config::Instance()->DisableReactiveMask.value_or(false))
    {
        LOG_DEBUG("ReactiveMask disabled!");
    }
    else
    {
        if (!GetD3D11ResourceFromParameter("BiasMask", InParameters,
                                           NVSDK_NGX_Parameter_DLSS_Input_Bias_Current_Color_Mask, &paramReactiveMask,
                                           &dx11Reactive, true, false, dontUseNTS))
        {
            if (Config::Instance()->Dx11Upscaler.value_or_default() == Upscaler::XeSS ||
                Config::Instance()->Dx11Upscaler.value_or_default() == Upscaler::XeSS_on12)
            {
                LOG_WARN("Bias mask not exist and it's enabled in config, it may cause problems!!");
                Config::Instance()->DisableReactiveMask.set_volatile_value(true);
                State::Instance().changeBackend[Handle()->Id] = true;
                return true;
            }
        }
    }

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

    if (paramColor != nullptr && !Dx11WithDx12::OpenHandle("Color", _dx11on12Device, paramColor, &dx11Color))
        return false;

    if (paramMv != nullptr && !Dx11WithDx12::OpenHandle("MV", _dx11on12Device, paramMv, &dx11Mv))
        return false;

    if (paramOutput[frame] != nullptr &&
        !Dx11WithDx12::OpenHandle("Output", _dx11on12Device, paramOutput[frame], &dx11Out))
        return false;

    if (paramDepth != nullptr && !Dx11WithDx12::OpenHandle("Depth", _dx11on12Device, paramDepth, &dx11Depth))
        return false;

    if (AutoExposure())
        LOG_DEBUG("AutoExposure enabled!");
    else if (paramExposure != nullptr &&
             !Dx11WithDx12::OpenHandle("Exposure", _dx11on12Device, paramExposure, &dx11Exp))
        return false;

    if (!Config::Instance()->DisableReactiveMask.value_or(false) && paramReactiveMask != nullptr &&
        !Dx11WithDx12::OpenHandle("ReactiveMask", _dx11on12Device, paramReactiveMask, &dx11Reactive))
    {
        return false;
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

    SetInitParameters(InParameters);

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

    ID3D11ShaderResourceView* restoreSRVs[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = {};
    ID3D11SamplerState* restoreSamplerStates[D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT] = {};
    ID3D11Buffer* restoreCBVs[D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT] = {};
    ID3D11UnorderedAccessView* restoreUAVs[D3D11_1_UAV_SLOT_COUNT] = {};
    ID3D11RenderTargetView* restoreRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    ID3D11DepthStencilView* restoreDSV = nullptr;

    // backup compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        restoreSRVs[i] = nullptr;
        InDeviceContext->CSGetShaderResources(i, 1, &restoreSRVs[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        restoreSamplerStates[i] = nullptr;
        InDeviceContext->CSGetSamplers(i, 1, &restoreSamplerStates[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        restoreCBVs[i] = nullptr;
        InDeviceContext->CSGetConstantBuffers(i, 1, &restoreCBVs[i]);
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        restoreUAVs[i] = nullptr;
        InDeviceContext->CSGetUnorderedAccessViews(i, 1, &restoreUAVs[i]);
    }

    DeviceContext->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restoreRTVs, &restoreDSV);

    // Unbind RenderTargets
    ID3D11RenderTargetView* nullRTVs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] = {};
    DeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, nullRTVs, nullptr);

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

    // restore compute shader resources
    for (UINT i = 0; i < D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT; i++)
    {
        if (restoreSRVs[i] != nullptr)
            InDeviceContext->CSSetShaderResources(i, 1, &restoreSRVs[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_SAMPLER_SLOT_COUNT; i++)
    {
        if (restoreSamplerStates[i] != nullptr)
            InDeviceContext->CSSetSamplers(i, 1, &restoreSamplerStates[i]);
    }

    for (UINT i = 0; i < D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT; i++)
    {
        if (restoreCBVs[i] != nullptr)
            InDeviceContext->CSSetConstantBuffers(i, 1, &restoreCBVs[i]);
    }

    for (UINT i = 0; i < D3D11_1_UAV_SLOT_COUNT; i++)
    {
        if (restoreUAVs[i] != nullptr)
            InDeviceContext->CSSetUnorderedAccessViews(i, 1, &restoreUAVs[i], 0);
    }

    DeviceContext->OMSetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT, restoreRTVs, restoreDSV);

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

    _dx11on12Device = WithDx12::GetD3D12Device(D3D_FEATURE_LEVEL_11_0);
    if (_dx11on12Device == nullptr)
    {
        LOG_ERROR("Cannot get D3D12 device from WithDx12!");
        return false;
    }

    Dx12CommandQueue = WithDx12::GetD3D12CommandQueue();
    if (Dx12CommandQueue == nullptr)
    {
        LOG_ERROR("Cannot get D3D12 command queue from WithDx12!");
        return false;
    }

    Dx12CommandListType = WithDx12::GetD3D12CommandListType();

    if (!CreateD3D12Objects())
    {
        LOG_ERROR("Failed to create D3D12 objects!");
        return false;
    }

    Dx11WithDx12::Init(Dx11Device, Dx11DeviceContext, _dx11on12Device, Dx12CommandQueue);

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
}
