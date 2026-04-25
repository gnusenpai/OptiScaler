#include "pch.h"
#include "DI_Dx12.h"

#include "DI_Common.h"

#include <Config.h>
#include <State.h>
#include "precompiled/DI_Shader.h"

bool DI_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, uint64_t InWidth,
                                   uint32_t InHeight, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto resourceFormat = DXGI_FORMAT_R32_FLOAT;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, InWidth,
                                                    InHeight, resourceFormat);

    if (result)
    {
        _buffer->SetName(L"DI_Buffer");
        _bufferState = InState;
    }

    return result;
}

void DI_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool DI_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % DI_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0), false);
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);

    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);

    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = 0;
    UINT dispatchHeight = 0;

    auto outDesc = OutResource->GetDesc();
    dispatchWidth = static_cast<UINT>((outDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (outDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

DI_Dx12::DI_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 1, 1, 0))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(DI_cso), sizeof(DI_cso));
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

        _recEncodeShader = DI_CompileShader(shaderCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
            LOG_ERROR("[{0}] CompileShader error!", _name);

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(
                InDevice, _rootSignature, &_pipelineState, _recEncodeShader,
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(DI_cso), sizeof(DI_cso))))
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

    _init = InitHeaps(InDevice, _frameHeaps, DI_NUM_OF_HEAPS);
}

DI_Dx12::~DI_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < DI_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
