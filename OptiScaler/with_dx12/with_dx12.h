#pragma once

#include <d3d12.h>
#include <dxgi.h>

class WithDx12
{
  private:
    inline static D3D12_COMMAND_LIST_TYPE _commandListType = D3D12_COMMAND_LIST_TYPE_DIRECT;

    inline static ID3D12Device* _d3d12Device = nullptr;
    inline static ID3D12CommandQueue* _d3d12CommandQueue = nullptr;

    inline static bool CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel);
    inline static void GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter,
                                          D3D_FEATURE_LEVEL InFeatureLevel, bool InRequestHighPerformanceAdapter);

  public:
    static ID3D12Device* GetD3D12Device(D3D_FEATURE_LEVEL InFeatureLevel);
    static ID3D12CommandQueue* GetD3D12CommandQueue();
    static D3D12_COMMAND_LIST_TYPE GetD3D12CommandListType();
};
