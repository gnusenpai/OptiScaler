#include "pch.h"
#include "FT_Dx12.h"
#include "FT_Common.h"

#include "precompile/FT_Shader.h"

#include <Config.h>

#include <magic_enum.hpp>

static DXGI_FORMAT GetCreateFormat(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    // Common UNORM 8-bit
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        return DXGI_FORMAT_R8G8B8A8_TYPELESS;

    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        return DXGI_FORMAT_B8G8R8A8_TYPELESS;

    // 10:10:10:2
    case DXGI_FORMAT_R10G10B10A2_UNORM:
        return DXGI_FORMAT_R10G10B10A2_TYPELESS;
    }

    return fmt;
}

bool FT_Dx12::CreateBufferResource(ID3D12Device* InDevice, ID3D12Resource* InSource, D3D12_RESOURCE_STATES InState)
{
    DXGI_FORMAT createFormat;
    if (InSource != nullptr)
    {
        auto inFormat = InSource->GetDesc().Format;
        createFormat = format;
        LOG_INFO("Input Format: {}, Create Format: {}", magic_enum::enum_name(inFormat),
                 magic_enum::enum_name(createFormat));
    }
    else
    {
        return false;
    }

    auto resourceFlags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    auto result =
        Shader_Dx12::CreateBufferResource(InDevice, InSource, InState, &_buffer, resourceFlags, 0, 0, createFormat);

    if (result)
    {
        _buffer->SetName(L"FT_Buffer");
        _bufferState = InState;
    }

    return result;
}

void FT_Dx12::SetBufferState(ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState)
{
    return Shader_Dx12::SetBufferState(InCommandList, InState, _buffer, &_bufferState);
}

bool FT_Dx12::Dispatch(ID3D12GraphicsCommandList* InCmdList, ID3D12Resource* InResource, ID3D12Resource* OutResource)
{
    if (!_init || _device == nullptr || InCmdList == nullptr || InResource == nullptr || OutResource == nullptr)
        return false;

    LOG_DEBUG("[{0}] Start!", _name);

    _counter++;
    _counter = _counter % FT_NUM_OF_HEAPS;
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

    auto inDesc = InResource->GetDesc();
    dispatchWidth = static_cast<UINT>((inDesc.Width + InNumThreadsX - 1) / InNumThreadsX);
    dispatchHeight = (inDesc.Height + InNumThreadsY - 1) / InNumThreadsY;

    InCmdList->Dispatch(dispatchWidth, dispatchHeight, 1);

    return true;
}

FT_Dx12::FT_Dx12(std::string InName, ID3D12Device* InDevice, DXGI_FORMAT InFormat)
    : Shader_Dx12(InName, InDevice), format(InFormat)
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

    // Compile shader blobs
    ID3DBlob* _recEncodeShader = nullptr;

    if (Config::Instance()->UsePrecompiledShaders.value_or_default())
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC computePsoDesc = {};
        computePsoDesc.pRootSignature = _rootSignature;
        computePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

        computePsoDesc.CS = CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(FT_cso), sizeof(FT_cso));

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
        _recEncodeShader = FT_CompileShader(FT_ShaderCode.c_str(), "CSMain", "cs_5_0");

        if (_recEncodeShader == nullptr)
            LOG_ERROR("[{0}] CompileShader error!", _name);

        // create pso objects
        if (!Shader_Dx12::CreateComputeShader(
                InDevice, _rootSignature, &_pipelineState, _recEncodeShader,
                CD3DX12_SHADER_BYTECODE(reinterpret_cast<const void*>(FT_cso), sizeof(FT_cso))))
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

    _init = InitHeaps(InDevice, _frameHeaps, FT_NUM_OF_HEAPS);
}

bool FT_Dx12::IsFormatCompatible(DXGI_FORMAT InFormat)
{
    // Bold move: accept all formats
    return true;
}

FT_Dx12::~FT_Dx12()
{
    if (!_init || State::Instance().isShuttingDown)
        return;

    for (int i = 0; i < FT_NUM_OF_HEAPS; i++)
    {
        _frameHeaps[i].ReleaseHeaps();
    }

    if (_buffer != nullptr)
    {
        _buffer->Release();
        _buffer = nullptr;
    }
}
