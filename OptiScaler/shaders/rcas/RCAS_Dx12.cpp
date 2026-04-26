#include "pch.h"

#include "RCAS_Dx12.h"

#include "precompile/RCAS_Shader.h"
#include "precompile/da_sharpen_Shader.h"

#include <Config.h>

bool RCAS_Dx12::CreatePipelineState(ID3D12Device* InDevice, const void* InShaderData, size_t InShaderSize,
                                    ID3D12PipelineState** OutPipelineState)
{
    D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
    computePsoDesc.pRootSignature = _rootSignature;
    computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(InShaderData, InShaderSize);
    auto hr = InDevice->CreateComputePipelineState(&computePsoDesc, __uuidof(ID3D12PipelineState*),
                                                   (void**) OutPipelineState);

    if (FAILED(hr))
    {
        LOG_ERROR("[{0}] CreateComputePipelineState error: {1:X}", _name, hr);
        return false;
    }

    return true;
}

bool RCAS_Dx12::CreatePipelineState(ID3D12Device* InDevice, const std::string& InShaderCode,
                                    ID3D12PipelineState** OutPipelineState, D3D12_SHADER_BYTECODE byteCode)
{
    ID3DBlob* shaderBlob = CompileShader(InShaderCode.c_str(), "CSMain", "cs_5_0");

    auto result = Shader_Dx12::CreateComputeShader(InDevice, _rootSignature, OutPipelineState, shaderBlob, byteCode);

    if (shaderBlob != nullptr)
        shaderBlob->Release();

    return result;
}

bool RCAS_Dx12::DispatchRCAS(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                             ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                             FrameDescriptorHeap& currentHeap)
{
    if (InMotionVectors == nullptr || _device == nullptr)
        return false;

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateShaderResourceView(_device, InMotionVectors, currentHeap.GetSrvCPU(1));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    InternalConstants constants {};

    auto outDesc = OutResource->GetDesc();
    auto mvsDesc = InMotionVectors->GetDesc();

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;

    FillMotionConstants(constants, InConstants);

    if (!CreateConstantsBuffer(_device, _constantBuffer, constants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineState);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    auto inDesc = InResource->GetDesc();
    UINT dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool RCAS_Dx12::DispatchDepthAdaptive(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                                      ID3D12Resource* InMotionVectors, ID3D12Resource* InDepth,
                                      RcasConstants InConstants, ID3D12Resource* OutResource,
                                      FrameDescriptorHeap& currentHeap)
{
    if (InDepth == nullptr || _pipelineStateDA == nullptr || _device == nullptr)
        return false;

    CreateShaderResourceView(_device, InResource, currentHeap.GetSrvCPU(0));
    CreateShaderResourceView(_device, InMotionVectors, currentHeap.GetSrvCPU(1));
    CreateShaderResourceView(_device, InDepth, currentHeap.GetSrvCPU(2));
    CreateUnorderedAccessView(_device, OutResource, currentHeap.GetUavCPU(0), 0);

    InternalConstantsDA constants {};

    auto outDesc = OutResource->GetDesc();
    auto mvsDesc = InMotionVectors->GetDesc();
    auto depthDesc = InDepth->GetDesc();

    constants.OutputWidth = (uint32_t) outDesc.Width;
    constants.OutputHeight = outDesc.Height;
    constants.MotionWidth = (uint32_t) mvsDesc.Width;
    constants.MotionHeight = mvsDesc.Height;
    constants.DepthWidth = (uint32_t) depthDesc.Width;
    constants.DepthHeight = depthDesc.Height;

    FillMotionConstants(constants, InConstants);

    if (!CreateConstantsBuffer(_device, _constantBuffer, constants, currentHeap.GetCbvCPU(0)))
    {
        LOG_ERROR("[{0}] Failed to create a constants buffer", _name);
        return false;
    }

    ID3D12DescriptorHeap* heaps[] = { currentHeap.GetHeapCSU() };
    InCmdList->SetDescriptorHeaps(_countof(heaps), heaps);
    InCmdList->SetComputeRootSignature(_rootSignature);
    InCmdList->SetPipelineState(_pipelineStateDA);
    InCmdList->SetComputeRootDescriptorTable(0, currentHeap.GetTableGPUStart());

    UINT dispatchWidth = static_cast<UINT>((constants.OutputWidth + InNumThreadsX - 1) / InNumThreadsX);
    UINT dispatchHeight = (constants.OutputHeight + InNumThreadsY - 1) / InNumThreadsY;
    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

bool RCAS_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                         D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;

    auto result = Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags);

    if (result)
    {
        _buffer->SetName(L"RCAS_Buffer");
        _bufferState = InState;
    }

    return result;
}

void RCAS_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool RCAS_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource,
                         ID3D12Resource* InMotionVectors, RcasConstants InConstants, ID3D12Resource* OutResource,
                         ID3D12Resource* InDepth)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr ||
        InMotionVectors == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % RCAS_NUM_OF_HEAPS;
    FrameDescriptorHeap& currentHeap = _frameHeaps[_counter];

    const bool useDepthAdaptive = Config::Instance()->UseDepthAwareSharpen.value_or_default() && InDepth != nullptr;

    if (useDepthAdaptive)
        return DispatchDepthAdaptive(InCmdList, InResource, InMotionVectors, InDepth, InConstants, OutResource,
                                     currentHeap);

    return DispatchRCAS(InCmdList, InResource, InMotionVectors, InConstants, OutResource, currentHeap);
}

RCAS_Dx12::RCAS_Dx12(std::string InName, ID3D12Device* InDevice) : Shader_Dx12(InName, InDevice)
{
    if (InDevice == nullptr)
    {
        LOG_ERROR("InDevice is nullptr!");
        return;
    }

    LOG_DEBUG("{0} start!", _name);

    if (!SetupRootSignature(InDevice, 3, 1, 1))
    {
        LOG_ERROR("Failed to setup root signature");
        return;
    }

    D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(sizeof(InternalConstantsDA));
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
        if (!CreatePipelineState(InDevice, reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso), &_pipelineState))
            return;

        if (!CreatePipelineState(InDevice, reinterpret_cast<const void*>(da_sharpen_cso), sizeof(da_sharpen_cso),
                                 &_pipelineStateDA))
            return;
    }
    else
    {
        if (!CreatePipelineState(InDevice, rcasCode, &_pipelineState,
                                 CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(rcas_cso), sizeof(rcas_cso))))
        {
            LOG_ERROR("[{0}] CreateComputeShader error!", _name);
            return;
        }

        if (!CreatePipelineState(
                InDevice, daSharpenCode, &_pipelineStateDA,
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(da_sharpen_cso), sizeof(da_sharpen_cso))))
        {
            LOG_ERROR("[{0}] CreateComputeShader error for depth adaptive shader!", _name);
            return;
        }
    }

    ScopedSkipHeapCapture skipHeapCapture {};

    _init = InitHeaps(InDevice, _frameHeaps, RCAS_NUM_OF_HEAPS);
}

RCAS_Dx12::~RCAS_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    if (_pipelineStateDA != nullptr)
    {
        _pipelineStateDA->Release();
        _pipelineStateDA = nullptr;
    }

    for (int i = 0; i < RCAS_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
