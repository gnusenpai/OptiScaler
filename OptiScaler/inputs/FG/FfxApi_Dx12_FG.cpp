#include "FfxApi_Dx12_FG.h"

#include "pch.h"

#include <Config.h>

#include <magic_enum.hpp>

#include "ffx_framegeneration.h"
#include "dx12/ffx_api_dx12.h"

ID3D12Device* _device = nullptr;
FG_Constants _fgConst {};
UINT64 _currentFrameId = 0;
std::mutex _newFrameMutex;

static D3D12_RESOURCE_STATES GetD3D12State(FfxApiResourceState state)
{
    switch (state)
    {
    case FFX_API_RESOURCE_STATE_COMMON:
        return D3D12_RESOURCE_STATE_COMMON;
    case FFX_API_RESOURCE_STATE_UNORDERED_ACCESS:
        return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    case FFX_API_RESOURCE_STATE_COMPUTE_READ:
        return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_READ:
        return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    case FFX_API_RESOURCE_STATE_PIXEL_COMPUTE_READ:
        return (D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    case FFX_API_RESOURCE_STATE_COPY_SRC:
        return D3D12_RESOURCE_STATE_COPY_SOURCE;
    case FFX_API_RESOURCE_STATE_COPY_DEST:
        return D3D12_RESOURCE_STATE_COPY_DEST;
    case FFX_API_RESOURCE_STATE_GENERIC_READ:
        return D3D12_RESOURCE_STATE_GENERIC_READ;
    case FFX_API_RESOURCE_STATE_INDIRECT_ARGUMENT:
        return D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    case FFX_API_RESOURCE_STATE_RENDER_TARGET:
        return D3D12_RESOURCE_STATE_RENDER_TARGET;
    default:
        return D3D12_RESOURCE_STATE_COMMON;
    }
}

ffxReturnCode_t ffxCreateContext_Dx12FG(ffxContext* context, ffxCreateContextDescHeader* desc,
                                        const ffxAllocationCallbacks* memCb)
{
    LOG_DEBUG("");

    if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATION)
    {
        ffxCreateContextDescHeader* next = nullptr;
        next = desc;
        while (next->pNext != nullptr)
        {
            next = next->pNext;

            if (next->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_BACKEND_DX12)
            {
                auto cbDesc = (ffxCreateBackendDX12Desc*) next;
                _device = cbDesc->device;
                LOG_DEBUG("Device found: {:X}", (size_t) _device);
                break;
            }
        }

        if (_device != nullptr && State::Instance().currentFG != nullptr)
        {
            if (State::Instance().currentFG->FrameGenerationContext() != nullptr)
            {
                LOG_INFO("There is already an active FG context: {:X}, destroying it.",
                         (size_t) State::Instance().currentFG->FrameGenerationContext());

                State::Instance().currentFG->DestroyFGContext();
            }

            auto ccDesc = (ffxCreateContextDescFrameGeneration*) desc;

            _fgConst = {};

            _fgConst.displayHeight = ccDesc->displaySize.height;
            _fgConst.displayWidth = ccDesc->displaySize.width;

            if ((ccDesc->flags & FFX_FRAMEGENERATION_ENABLE_ASYNC_WORKLOAD_SUPPORT) > 0)
                _fgConst.flags |= FG_Flags::Async;

            if ((ccDesc->flags & FFX_FRAMEGENERATION_ENABLE_DISPLAY_RESOLUTION_MOTION_VECTORS) > 0)
                _fgConst.flags |= FG_Flags::DisplayResolutionMVs;

            if ((ccDesc->flags & FFX_FRAMEGENERATION_ENABLE_MOTION_VECTORS_JITTER_CANCELLATION) > 0)
                _fgConst.flags |= FG_Flags::JitteredMVs;

            if ((ccDesc->flags & FFX_FRAMEGENERATION_ENABLE_DEPTH_INVERTED) > 0)
                _fgConst.flags |= FG_Flags::InvertedDepth;

            if ((ccDesc->flags & FFX_FRAMEGENERATION_ENABLE_DEPTH_INFINITE) > 0)
                _fgConst.flags |= FG_Flags::InfiniteDepth;

            if ((ccDesc->flags & FFX_FRAMEGENERATION_ENABLE_HIGH_DYNAMIC_RANGE) > 0)
                _fgConst.flags |= FG_Flags::Hdr;

            // State::Instance().currentFG->CreateObjects(_device);
            // State::Instance().currentFG->CreateContext(_device, _fgConst);
            // State::Instance().currentFG->ResetCounters();

            // if (State::Instance().currentFG->FrameGenerationContext() == nullptr)
            //     return FFX_API_RETURN_ERROR_RUNTIME_ERROR;

            *context = (ffxContext) fcContext;
            return FFX_API_RETURN_OK;

            // State::Instance().currentFG->FrameGenerationContext();
        }
    }
    else if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WRAP_DX12)
    {
        auto cDesc = (ffxCreateContextDescFrameGenerationSwapChainWrapDX12*) desc;

        if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
        {
            *context = (ffxContext) scContext; // State::Instance().currentFG->SwapchainContext();
            *cDesc->swapchain = (IDXGISwapChain4*) State::Instance().currentFGSwapchain;
            return FFX_API_RETURN_OK;
        }
        else
        {
            LOG_ERROR("currentFG: {:X}, currentFGSwapchain: {:X}", (size_t) State::Instance().currentFG,
                      (size_t) State::Instance().currentFGSwapchain);
            return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
        }
    }
    else if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_NEW_DX12)
    {
        auto cDesc = (ffxCreateContextDescFrameGenerationSwapChainNewDX12*) desc;
        auto result =
            cDesc->dxgiFactory->CreateSwapChain(cDesc->gameQueue, cDesc->desc, (IDXGISwapChain**) cDesc->swapchain);

        if (result == S_OK)
        {
            LOG_INFO("Swapchain created");

            if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
            {
                *context = (ffxContext) scContext;
                // *context = State::Instance().currentFG->SwapchainContext();
                return FFX_API_RETURN_OK;
            }
            else
            {
                LOG_ERROR("FG Swapchain creation error, currentFG: {:X}, currentFGSwapchain: {:X}",
                          (size_t) State::Instance().currentFG, (size_t) State::Instance().currentFGSwapchain);
            }
        }
        else
        {
            LOG_ERROR("Swapchain creation error: {:X}", (UINT) result);
        }

        return FFX_API_RETURN_ERROR_PARAMETER;
    }
    else if (desc->type == FFX_API_CREATE_CONTEXT_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_FOR_HWND_DX12)
    {
        auto cDesc = (ffxCreateContextDescFrameGenerationSwapChainForHwndDX12*) desc;

        IDXGIFactory2* factory = nullptr;
        auto scResult = cDesc->dxgiFactory->QueryInterface(IID_PPV_ARGS(&factory));

        if (factory == nullptr)
            return FFX_API_RETURN_ERROR_PARAMETER;

        factory->Release();

        auto result = factory->CreateSwapChainForHwnd(cDesc->gameQueue, cDesc->hwnd, cDesc->desc, cDesc->fullscreenDesc,
                                                      nullptr, (IDXGISwapChain1**) cDesc->swapchain);

        if (result == S_OK)
        {
            LOG_INFO("Swapchain created");

            if (State::Instance().currentFG != nullptr && State::Instance().currentFGSwapchain != nullptr)
            {
                *context = (ffxContext) scContext;
                //*context = State::Instance().currentFG->SwapchainContext();
                return FFX_API_RETURN_OK;
            }
            else
            {
                LOG_ERROR("FG Swapchain creation error, currentFG: {:X}, currentFGSwapchain: {:X}",
                          (size_t) State::Instance().currentFG, (size_t) State::Instance().currentFGSwapchain);
            }
        }
        else
        {
            LOG_ERROR("Swapchain creation error: {:X}", (UINT) result);
        }

        return FFX_API_RETURN_ERROR_PARAMETER;
    }

    return rcContinue;
}

ffxReturnCode_t ffxDestroyContext_Dx12FG(ffxContext* context, const ffxAllocationCallbacks* memCb)
{
    LOG_DEBUG("");

    if (State::Instance().currentFG != nullptr && (void*) scContext == *context)
    {
        LOG_INFO("Destroying Swapchain Context: {:X}", (size_t) State::Instance().currentFG);
        State::Instance().currentFG->Shutdown();
        return FFX_API_RETURN_OK;
    }
    else if (State::Instance().currentFG != nullptr && (void*) fcContext == *context)
    {
        LOG_INFO("Destroying FG Context: {:X}", (size_t) State::Instance().currentFG);
        State::Instance().currentFG->DestroyFGContext();
        return FFX_API_RETURN_OK;
    }

    return rcContinue;
}

ffxReturnCode_t ffxConfigure_Dx12FG(ffxContext* context, ffxConfigureDescHeader* desc)
{
    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

    if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION)
    {
        auto cDesc = (ffxConfigureDescFrameGeneration*) desc;

        {
            std::lock_guard<std::mutex> lock(_newFrameMutex);
            if (cDesc->frameID > _currentFrameId)
            {
                _currentFrameId = cDesc->frameID;
                fg->StartNewFrame();
            }
        }

        LOG_DEBUG("FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION frameID: {} ", cDesc->frameID);

        if (cDesc->frameGenerationEnabled && !fg->IsActive())
        {
            fg->Activate();
            fg->ResetCounters();
        }
        else if (!cDesc->frameGenerationEnabled && fg->IsActive())
        {
            fg->Deactivate();
            fg->ResetCounters();
            return FFX_API_RETURN_OK;
        }

        ffxConfigureDescHeader* next = nullptr;
        next = desc;
        while (next->pNext != nullptr)
        {
            next = next->pNext;

            if (next->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE)
            {
                auto crDesc = (ffxConfigureDescFrameGenerationRegisterDistortionFieldResource*) next;
                LOG_DEBUG("DistortionFieldResource found: {:X}", (size_t) crDesc->distortionField.resource);

                if (fg->FrameGenerationContext() != nullptr && crDesc->distortionField.resource != nullptr)
                {
                    Dx12Resource dfr {};
                    dfr.cmdList = nullptr; // Not sure about this
                    dfr.height = cDesc->generationRect.height;
                    dfr.resource = (ID3D12Resource*) crDesc->distortionField.resource;
                    dfr.state = GetD3D12State((FfxApiResourceState) crDesc->distortionField.state);
                    dfr.type = FG_ResourceType::Distortion;
                    dfr.validity = FG_ResourceValidity::UntilPresent; // Not sure about this
                    dfr.width = cDesc->generationRect.width;
                    dfr.left = cDesc->generationRect.left;
                    dfr.top = cDesc->generationRect.top;

                    fg->SetResource(&dfr);
                }
            }
            else if (next->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12)
            {
                auto crDesc = (ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12*) next;
                LOG_DEBUG("UiResource found: {:X}", (size_t) crDesc->uiResource.resource);

                if (fg->FrameGenerationContext() != nullptr && crDesc->uiResource.resource != nullptr)
                {
                    auto validity = FG_ResourceValidity::UntilPresent;
                    if ((crDesc->flags & FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING) >
                        0)
                    {
                        LOG_WARN("FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING is set!");

                        // Not sure which cmdList to use
                        // validity = FG_ResourceValidity::ValidButMakeCopy;
                    }

                    Dx12Resource ui {};
                    ui.cmdList = nullptr; // Not sure about this
                    ui.height = cDesc->generationRect.height;
                    ui.resource = (ID3D12Resource*) crDesc->uiResource.resource;
                    ui.state = GetD3D12State((FfxApiResourceState) crDesc->uiResource.state);
                    ui.type = FG_ResourceType::UIColor;
                    ui.validity = validity; // Not sure about this
                    ui.width = cDesc->generationRect.width;
                    ui.left = cDesc->generationRect.left;
                    ui.top = cDesc->generationRect.top;

                    fg->SetResource(&ui);
                }
            }
        }

        if (cDesc->HUDLessColor.resource != nullptr)
        {
            Dx12Resource hudless {};
            hudless.cmdList = nullptr; // Not sure about this
            hudless.height = cDesc->generationRect.height;
            hudless.resource = (ID3D12Resource*) cDesc->HUDLessColor.resource;
            hudless.state = GetD3D12State((FfxApiResourceState) cDesc->HUDLessColor.state);
            hudless.type = FG_ResourceType::HudlessColor;
            hudless.validity = FG_ResourceValidity::UntilPresent; // Not sure about this
            hudless.width = cDesc->generationRect.width;
            hudless.left = cDesc->generationRect.left;
            hudless.top = cDesc->generationRect.top;

            fg->SetResource(&hudless);
        }

        fg->SetInterpolationRect(cDesc->generationRect.width, cDesc->generationRect.height);
        fg->SetInterpolationPos(cDesc->generationRect.left, cDesc->generationRect.top);

        if (cDesc->frameGenerationCallback != nullptr && cDesc->frameGenerationEnabled)
        {
            // LOG_DEBUG("Calling frameGenerationCallback");

            // ffxDispatchDescFrameGeneration ddfg {};
            // ddfg.header.type = FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION;
            // ddfg.frameID = cDesc->frameID;
            // ddfg.generationRect = cDesc->generationRect;
            // ddfg.numGeneratedFrames = 1;
            // auto result = cDesc->frameGenerationCallback(&ddfg, cDesc->frameGenerationCallbackUserContext);

            LOG_WARN("frameGenerationCallback: {:X}, frameGenerationCallbackUserContext: {:X}",
                     (size_t) cDesc->frameGenerationCallback, (size_t) cDesc->frameGenerationCallbackUserContext);
        }

        if (cDesc->presentCallback != nullptr && cDesc->frameGenerationEnabled)
        {
            // ffxCallbackDescFrameGenerationPresent cdfgp {};
            // cdfgp.header.type = FFX_API_CALLBACK_DESC_TYPE_FRAMEGENERATION_PRESENT;
            // cdfgp.frameID = cDesc->frameID;
            // cdfgp.device = _device;
            // cdfgp.isGeneratedFrame = true;
            // auto result = cDesc->presentCallback(&cdfgp, cDesc->presentCallbackUserContext);

            LOG_WARN("presentCallback: {:X}, presentCallbackUserContext: {:X}", (size_t) cDesc->presentCallback,
                     (size_t) cDesc->presentCallbackUserContext);
        }

        if (cDesc->allowAsyncWorkloads || cDesc->onlyPresentGenerated)
            LOG_DEBUG("allowAsyncWorkloads: {}, onlyPresentGenerated: {}", cDesc->allowAsyncWorkloads,
                      cDesc->onlyPresentGenerated);

        // Not used:
        //   cDesc->allowAsyncWorkloads
        //   cDesc->onlyPresentGenerated
        //   cDesc->frameID
        //   cDesc->frameGenerationCallback
        //   cDesc->frameGenerationCallbackUserContext
        //   cDesc->presentCallback
        //   cDesc->presentCallbackUserContext
        //   cDesc->swapChain

        LOG_DEBUG("FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION done");

        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE)
    {
        auto crDesc = (ffxConfigureDescFrameGenerationRegisterDistortionFieldResource*) desc;
        LOG_DEBUG("DistortionFieldResource found: {:X}", (size_t) crDesc->distortionField.resource);

        if (fg->FrameGenerationContext() != nullptr && crDesc->distortionField.resource != nullptr)
        {
            UINT width = 0;
            UINT height = 0;
            UINT left = 0;
            UINT top = 0;

            fg->GetInterpolationRect(width, height);
            fg->GetInterpolationPos(left, top);

            // Check for FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12
            ffxConfigureDescHeader* next = nullptr;
            next = desc;
            while (next->pNext != nullptr)
            {
                next = next->pNext;

                if (next->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12)
                {
                    auto crDesc = (ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12*) next;
                    LOG_DEBUG("UiResource found: {:X}", (size_t) crDesc->uiResource.resource);

                    if (fg->FrameGenerationContext() != nullptr && crDesc->uiResource.resource != nullptr)
                    {
                        auto validity = FG_ResourceValidity::UntilPresent;

                        if ((crDesc->flags &
                             FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING) > 0)
                        {
                            LOG_WARN(
                                "FFX_FRAMEGENERATION_UI_COMPOSITION_FLAG_ENABLE_INTERNAL_UI_DOUBLE_BUFFERING is set!");

                            // Not sure which cmdList to use
                            // validity = FG_ResourceValidity::ValidButMakeCopy;
                        }

                        Dx12Resource ui {};
                        ui.cmdList = nullptr; // Not sure about this
                        ui.height = height;
                        ui.resource = (ID3D12Resource*) crDesc->uiResource.resource;
                        ui.state = GetD3D12State((FfxApiResourceState) crDesc->uiResource.state);
                        ui.type = FG_ResourceType::UIColor;
                        ui.validity = validity; // Not sure about this
                        ui.width = width;
                        ui.left = left;
                        ui.top = top;

                        fg->SetResource(&ui);
                    }
                }
            }

            Dx12Resource dfr {};
            dfr.cmdList = nullptr; // Not sure about this
            dfr.height = height;
            dfr.resource = (ID3D12Resource*) crDesc->distortionField.resource;
            dfr.state = GetD3D12State((FfxApiResourceState) crDesc->distortionField.state);
            dfr.type = FG_ResourceType::Distortion;
            dfr.validity = FG_ResourceValidity::UntilPresent; // Not sure about this
            dfr.width = width;
            dfr.left = left;
            dfr.top = top;

            fg->SetResource(&dfr);
        }

        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_REGISTERUIRESOURCE_DX12)
    {
        auto crDesc = (ffxConfigureDescFrameGenerationSwapChainRegisterUiResourceDX12*) desc;
        LOG_DEBUG("UiResource found: {:X}", (size_t) crDesc->uiResource.resource);

        if (fg->FrameGenerationContext() != nullptr && crDesc->uiResource.resource != nullptr)
        {
            UINT width = 0;
            UINT height = 0;
            UINT left = 0;
            UINT top = 0;

            fg->GetInterpolationRect(width, height);
            fg->GetInterpolationPos(left, top);

            // Check for FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE
            ffxConfigureDescHeader* next = nullptr;
            next = desc;
            while (next->pNext != nullptr)
            {
                next = next->pNext;

                if (next->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_REGISTERDISTORTIONRESOURCE)
                {
                    auto crDesc = (ffxConfigureDescFrameGenerationRegisterDistortionFieldResource*) next;
                    LOG_DEBUG("DistortionFieldResource found: {:X}", (size_t) crDesc->distortionField.resource);

                    if (fg->FrameGenerationContext() != nullptr && crDesc->distortionField.resource != nullptr)
                    {
                        Dx12Resource dfr {};
                        dfr.cmdList = nullptr; // Not sure about this
                        dfr.height = height;
                        dfr.resource = (ID3D12Resource*) crDesc->distortionField.resource;
                        dfr.state = GetD3D12State((FfxApiResourceState) crDesc->distortionField.state);
                        dfr.type = FG_ResourceType::Distortion;
                        dfr.validity = FG_ResourceValidity::UntilPresent; // Not sure about this
                        dfr.width = width;
                        dfr.left = left;
                        dfr.top = top;

                        fg->SetResource(&dfr);
                    }
                }
            }

            Dx12Resource ui {};
            ui.cmdList = nullptr; // Not sure about this
            ui.height = height;
            ui.resource = (ID3D12Resource*) crDesc->uiResource.resource;
            ui.state = GetD3D12State((FfxApiResourceState) crDesc->uiResource.state);
            ui.type = FG_ResourceType::UIColor;
            ui.validity = FG_ResourceValidity::UntilPresent; // Not sure about this
            ui.width = width;
            ui.left = left;
            ui.top = top;

            fg->SetResource(&ui);
        }

        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATION_KEYVALUE)
    {
        auto cDesc = (ffxConfigureDescFrameGenerationKeyValue*) desc;
        LOG_WARN("key: {}, u64: {}, ptr: {}", cDesc->key, cDesc->u64, (size_t) cDesc->ptr);
        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_CONFIGURE_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_KEYVALUE_DX12)
    {
        auto cDesc = (ffxConfigureDescFrameGenerationSwapChainKeyValueDX12*) desc;
        LOG_WARN("key: {}, u64: {}, ptr: {}", cDesc->key, cDesc->u64, (size_t) cDesc->ptr);
        return FFX_API_RETURN_OK;
    }

    return rcContinue;
}

ffxReturnCode_t ffxQuery_Dx12FG(ffxContext* context, ffxQueryDescHeader* desc)
{
    LOG_DEBUG("");

    if (desc->type == FFX_API_QUERY_DESC_TYPE_FRAMEGENERATION_GPU_MEMORY_USAGE)
    {
    }
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONCOMMANDLIST_DX12)
    {
    }
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_INTERPOLATIONTEXTURE_DX12)
    {
    }
    else if (desc->type == FFX_API_QUERY_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_GPU_MEMORY_USAGE_DX12)
    {
    }

    return rcContinue;
}

ffxReturnCode_t ffxDispatch_Dx12FG(ffxContext* context, ffxDispatchDescHeader* desc)
{
    auto fg = State::Instance().currentFG;

    if (fg == nullptr)
    {
        LOG_ERROR("No FG Feature!");
        return FFX_API_RETURN_ERROR_RUNTIME_ERROR;
    }

    if (desc->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION)
    {
        auto cdDesc = (ffxDispatchDescFrameGeneration*) desc;
        LOG_DEBUG("FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION");
        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE)
    {
        auto cdDesc = (ffxDispatchDescFrameGenerationPrepare*) desc;
        LOG_DEBUG("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE, frameID: {}", cdDesc->frameID);

        {
            std::lock_guard<std::mutex> lock(_newFrameMutex);
            if (cdDesc->frameID > _currentFrameId)
            {
                _currentFrameId = cdDesc->frameID;
                fg->StartNewFrame();
            }
        }

        auto device = _device == nullptr ? State::Instance().currentD3D12Device : _device;
        fg->EvaluateState(device, _fgConst);

        if (fg->IsActive() && !fg->IsPaused())
        {
            // Camera Data
            bool cameraDataFound = false;
            ffxDispatchDescHeader* next = nullptr;
            next = desc;
            while (next->pNext != nullptr)
            {
                next = next->pNext;

                if (next->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE_CAMERAINFO)
                {
                    auto cameraDesc = (ffxDispatchDescFrameGenerationPrepareCameraInfo*) next;

                    fg->SetCameraData(cameraDesc->cameraPosition, cameraDesc->cameraUp, cameraDesc->cameraRight,
                                      cameraDesc->cameraForward);

                    cameraDataFound = true;
                    break;
                }
            }

            // Camera Values
            UINT dispWidth = 0;
            UINT dispHeight = 0;
            fg->GetInterpolationRect(dispWidth, dispHeight);
            auto aspectRatio = (float) dispWidth / (float) dispHeight;
            fg->SetCameraValues(cdDesc->cameraNear, cdDesc->cameraFar, cdDesc->cameraFovAngleVertical, aspectRatio);

            // Other values
            fg->SetFrameTimeDelta(cdDesc->frameTimeDelta);
            fg->SetJitter(cdDesc->jitterOffset.x, cdDesc->jitterOffset.y);
            fg->SetMVScale(cdDesc->motionVectorScale.x, cdDesc->motionVectorScale.y);
            fg->SetReset(cdDesc->unused_reset ? 1 : 0);

            if (cdDesc->depth.resource != nullptr)
            {
                Dx12Resource depth {};
                depth.cmdList = (ID3D12GraphicsCommandList*) cdDesc->commandList;
                depth.height = cdDesc->renderSize.height; // cdDesc->depth.description.width;
                depth.resource = (ID3D12Resource*) cdDesc->depth.resource;
                depth.state = GetD3D12State((FfxApiResourceState) cdDesc->depth.state);
                depth.type = FG_ResourceType::Depth;
                depth.validity = FG_ResourceValidity::JustTrackCmdlist;
                depth.width = cdDesc->renderSize.width; // cdDesc->depth.description.height;

                fg->SetResource(&depth);
            }

            if (cdDesc->motionVectors.resource != nullptr)
            {
                uint32_t width = 0;
                uint32_t height = 0;

                if (_fgConst.flags & FG_Flags::DisplayResolutionMVs)
                {
                    width = _fgConst.displayWidth;
                    height = _fgConst.displayHeight;
                }
                else
                {
                    width = cdDesc->renderSize.width;
                    height = cdDesc->renderSize.height;
                }

                Dx12Resource velocity {};
                velocity.cmdList = (ID3D12GraphicsCommandList*) cdDesc->commandList;
                velocity.height = height; // cdDesc->motionVectors.description.width;
                velocity.resource = (ID3D12Resource*) cdDesc->motionVectors.resource;
                velocity.state = GetD3D12State((FfxApiResourceState) cdDesc->motionVectors.state);
                velocity.type = FG_ResourceType::Velocity;
                velocity.validity = FG_ResourceValidity::JustTrackCmdlist;
                velocity.width = width; // cdDesc->motionVectors.description.height;

                fg->SetResource(&velocity);
            }
        }
        else
        {
            LOG_DEBUG("IsActive: {}, IsPaused: {}", fg->IsActive(), fg->IsPaused());
        }

        LOG_DEBUG("DISPATCH_DESC_TYPE_FRAMEGENERATION_PREPARE done");
        return FFX_API_RETURN_OK;
    }
    else if (desc->type == FFX_API_DISPATCH_DESC_TYPE_FRAMEGENERATIONSWAPCHAIN_WAIT_FOR_PRESENTS_DX12)
    {
        auto cdDesc = (ffxDispatchDescFrameGenerationSwapChainWaitForPresentsDX12*) desc;
        return FFX_API_RETURN_OK;
    }

    return rcContinue;
}
