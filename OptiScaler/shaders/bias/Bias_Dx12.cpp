#include "pch.h"
#include "Bias_Dx12.h"

#include "Bias_Common.h"
#include "precompile/Bias_Shader.h"

#include <Config.h>

bool Bias_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags);

    if (result)
    {
        _buffer->SetName(L"Bias_Buffer");
        _bufferState = InState;
    }

    return result;
}

void Bias_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool Bias_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, float InBias,
                         ID3D12Resource* OutResource)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % BIAS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0), false);
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    InternalConstants constants {};

    if (InBias < 0.0f)
        constants.Bias = 0.0f;
    else if (InBias > 0.9f)
        constants.Bias = 0.9f;
    else
        constants.Bias = InBias;

    // Copy the updated constant buffer data to the constant buffer resource
    BYTE* pCBDataBegin;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU
    auto result = _constantBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pCBDataBegin));

    if (result != S_OK)
    {
        LOG_ERROR("[{0}] _constantBuffer->Map error {1:x}", _name, (unsigned int) result);

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

    auto inDesc = InResource->GetDesc();
    dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

Bias_Dx12::Bias_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
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

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalConstants));
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
        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bias_cso), sizeof(bias_cso));
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
        ID3DBlob* _recEncodeShader = Bias_CompileShader(biasShader.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
            LOG_ERROR("[{0}] CompileShader error!", _name);

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(
                InDevice, _rootSignature, &_pipelineState, _recEncodeShader,
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(bias_cso), sizeof(bias_cso))))
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

    _init = InitHeaps(InDevice, _frameHeaps, BIAS_NUM_OF_HEAPS);
}

Bias_Dx12::~Bias_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < BIAS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
