#include "pch.h"
#include "RF_Dx12.h"

#include "RF_Common.h"

#include <Config.h>
#include <State.h>

#include "precompiled/RF_Shader.h"

bool RF_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource,
                       UINT64 width, UINT height, bool velocity)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % RF_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    auto inDesc = InResource->GetDesc();

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0), false);
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    RFConstants constants {};

    constants.height = height - 1;
    constants.width = static_cast<UINT>(width - 1);
    constants.offset = Config::Instance()->FGResourceFlipOffset.value_or_default() ? inDesc.Height - height : 0;
    constants.velocity = velocity ? 1 : 0;

    LOG_DEBUG("Width: {}, Height: {}, Offset", constants.width, constants.height, constants.offset);

    // Copy the updated constant buffer data to the constant buffer resource
    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
    auto result = _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (UINT) result);

        if (result == DXGI_ERROR_DEVICE_REMOVED && _device != nullptr)
            Util::GetDeviceRemovedReason(_device);

        return false;
    }

    if (pCBDataBegin == nullptr)
    {
        _constantBuffer->Unmap(0, nullptr);
        LOG_ERROR("[{0}] pCBDataBegin is null!", _name);
        return false;
    }

    memcpy(pCBDataBegin, &constants, sizeof(constants));
    _constantBuffer->Unmap(0, nullptr);

    D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
    cbvDesc.BufferLocation = _constantBuffer->GetGPUVirtualAddress();
    cbvDesc.SizeInBytes = sizeof(constants);
    _device->CreateConstantBufferView(&cbvDesc, currentHeap.GetCbvCPU(0));

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    if ((State::Instance().currentFeature->GetFeatureFlags() & NVSDK_NGX_DLSS_Feature_Flags_MVLowRes) == 0)
    {
        dispatchWidth =
            static_cast<UINT>((State::Instance().currentFeature->DisplayWidth() + InNumThreadsX - 1) / InNumThreadsX);
        dispatchHeight = (State::Instance().currentFeature->DisplayHeight() + InNumThreadsY - 1) / InNumThreadsY;
    }
    else
    {
        dispatchWidth =
            static_cast<UINT>((State::Instance().currentFeature->RenderWidth() + InNumThreadsX - 1) / InNumThreadsX);
        dispatchHeight = (State::Instance().currentFeature->RenderHeight() + InNumThreadsY - 1) / InNumThreadsY;
    }

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

RF_Dx12::RF_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 1, 1, 1))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(RFConstants));
    auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);

    auto result =
        InDevice->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ,
                                          nullptr, IID_PPV_ARGS(&_constantBuffer));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] CreateCommittedResource error {1:x}", _name, (unsigned int) result);
        return;
    }

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(RF_cso), sizeof(RF_cso));
        auto hr = InDevice->CreateComputePipelineState(&computePsoDesc, __uuidof(ID3D12PipelineState*),
                                                       (void**) &_pipelineState);

        if (FAILED(hr))
        {
            LOG_ERROR("[{0}] CreateComputePipelineState error: {1:X}", _name, hr);
            return;
        }
    }
    else
    {
        // Compile shader blobs
        ID3DBlob* _recEncodeShader = nullptr;

        _recEncodeShader = RF_CompileShader(rfCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
            LOG_ERROR("[{0}] CompileShader error!", _name);

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(
                InDevice, _rootSignature, &_pipelineState, _recEncodeShader,
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(RF_cso), sizeof(RF_cso))))
        {
            LOG_ERROR("[{0}] CreateComputeShader error!", _name);
            return;
        }

        if (_recEncodeShader != nullptr)
        {
            _recEncodeShader->Release();
            _recEncodeShader = nullptr;
        }
    }

    ScopedSkipHeapCapture skipHeapCapture {};

    _init = InitHeaps(InDevice, _frameHeaps, RF_NUM_OF_HEAPS);
}

RF_Dx12::~RF_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_pipelineState != nullptr)
    {
        _pipelineState->Release();
        _pipelineState = nullptr;
    }

    if (_rootSignature != nullptr)
    {
        _rootSignature->Release();
        _rootSignature = nullptr;
    }

    for (int i = 0; i < RF_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_constantBuffer != nullptr)
    {
        _constantBuffer->Release();
        _constantBuffer = nullptr;
    }
}
