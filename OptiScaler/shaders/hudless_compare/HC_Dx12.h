#pragma once

#include <pch.h>

#include <d3d12.h>
#include <d3dx/d3dx12.h>
#include <dxgi1_6.h>

class HC_Dx12
{
  private:
    struct alignas(256) InternalCompareParams
    {
        float DiffThreshold = 0.02f;
        float PinkAmount = 1.0f;
        float InvOutputSize[2] = { 0, 0 };
    };

    std::string _name = "";
    bool _init = false;

    ID3D12RootSignature* _rootSignature = nullptr;
    ID3D12PipelineState* _pipelineState = nullptr;

    ID3D12DescriptorHeap* _srvHeap[2] = {};
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuSrv0Handle[2] {};
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuSrv1Handle[2] {};
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuRtv0Handle[2] {};
    D3D12_CPU_DESCRIPTOR_HANDLE _cpuCbv0Handle[2] {};
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuSrv0Handle[2] {};
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuSrv1Handle[2] {};
    D3D12_GPU_DESCRIPTOR_HANDLE _gpuCbv0Handle[2] {};
    int _counter = 0;

    ID3D12Device* _device = nullptr;
    ID3D12Resource* _buffer[2] = {};
    ID3D12Resource* _constantBuffer = nullptr;
    D3D12_RESOURCE_STATES _bufferState[2] = { D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COMMON };

    ID3D12GraphicsCommandList* _commandList[2] = {};
    ID3D12CommandAllocator* _commandAllocator[2] = {};

    static DXGI_FORMAT ToSRGB(DXGI_FORMAT f);

    static DXGI_FORMAT TranslateTypelessFormats(DXGI_FORMAT format);

    static void ResourceBarrier(ID3D12GraphicsCommandList* InCommandList, ID3D12Resource* InResource,
                                D3D12_RESOURCE_STATES InBeforeState, D3D12_RESOURCE_STATES InAfterState);

  public:
    bool CreateBufferResource(UINT index, ID3D12Device* InDevice, ID3D12Resource* InSource,
                              D3D12_RESOURCE_STATES InState);
    void SetBufferState(UINT index, ID3D12GraphicsCommandList* InCommandList, D3D12_RESOURCE_STATES InState);
    bool Dispatch(IDXGISwapChain3* sc, ID3D12CommandQueue* queue, ID3D12Resource* hudless, D3D12_RESOURCE_STATES state);

    bool IsInit() const { return _init; }

    HC_Dx12(std::string InName, ID3D12Device* InDevice);

    ~HC_Dx12();
};
