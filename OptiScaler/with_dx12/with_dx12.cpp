#include <pch.h>

#include "with_dx12.h"

#include <proxies/DXGI_Proxy.h>
#include <proxies/D3D12_Proxy.h>

void WithDx12::GetHardwareAdapter(IDXGIFactory1* InFactory, IDXGIAdapter** InAdapter, D3D_FEATURE_LEVEL InFeatureLevel,
                                  bool InRequestHighPerformanceAdapter)
{
    LOG_FUNC();

    *InAdapter = nullptr;

    IDXGIAdapter1* adapter;

    IDXGIFactory6* factory6;
    if (SUCCEEDED(InFactory->QueryInterface(IID_PPV_ARGS(&factory6))))
    {
        LOG_DEBUG("Using IDXGIFactory6 & EnumAdapterByGpuPreference");

        for (UINT adapterIndex = 0;
             DXGI_ERROR_NOT_FOUND != factory6->EnumAdapterByGpuPreference(adapterIndex,
                                                                          InRequestHighPerformanceAdapter == true
                                                                              ? DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE
                                                                              : DXGI_GPU_PREFERENCE_UNSPECIFIED,
                                                                          IID_PPV_ARGS(&adapter));
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            *InAdapter = adapter;
            break;
        }
    }
    else
    {
        LOG_DEBUG("Using InFactory & EnumAdapters1");
        for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != InFactory->EnumAdapters1(adapterIndex, &adapter);
             ++adapterIndex)
        {
            DXGI_ADAPTER_DESC1 desc;
            adapter->GetDesc1(&desc);

            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue;

            // Check to see whether the adapter supports Direct3D 12, but don't create the actual device yet.
            auto result = D3d12Proxy::D3D12CreateDevice_()(adapter, InFeatureLevel, _uuidof(ID3D12Device), nullptr);

            if (result == S_FALSE)
            {
                LOG_DEBUG("D3D12CreateDevice test result: {:X}", (UINT) result);
                *InAdapter = adapter;
                break;
            }
        }
    }
}

ID3D12Device* WithDx12::GetD3D12Device(D3D_FEATURE_LEVEL InFeatureLevel)
{
    if (CreateDx12Device(InFeatureLevel))
        return _d3d12Device;
    else
        return nullptr;
}

ID3D12CommandQueue* WithDx12::GetD3D12CommandQueue() { return _d3d12CommandQueue; }

D3D12_COMMAND_LIST_TYPE WithDx12::GetD3D12CommandListType() { return _commandListType; }

bool WithDx12::CreateDx12Device(D3D_FEATURE_LEVEL InFeatureLevel)
{
    LOG_FUNC();

    ScopedSkipSpoofing skipSpoofing {};
    ScopedSkipVulkanHooks skipVulkanHooks {};

    HRESULT result;

    if (State::Instance().currentD3D12Device == nullptr ||
        ((State::Instance().gameQuirks & GameQuirk::ForceCreateD3D12Device) && _d3d12Device == nullptr))
    {
        IDXGIFactory2* factory = nullptr;

        if (DxgiProxy::Module() == nullptr)
            result = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        else
            result = DxgiProxy::CreateDxgiFactory2_()(0, __uuidof(factory), &factory);

        if (result != S_OK)
        {
            LOG_ERROR("Can't create factory: {0:x}", result);
            return false;
        }

        IDXGIAdapter* hwAdapter = nullptr;
        GetHardwareAdapter(factory, &hwAdapter, InFeatureLevel, true);

        if (hwAdapter == nullptr)
            LOG_WARN("Can't get hwAdapter, will try nullptr!");

        if (D3d12Proxy::Module() == nullptr)
            result = D3D12CreateDevice(hwAdapter, InFeatureLevel, IID_PPV_ARGS(&_d3d12Device));
        else
            result = D3d12Proxy::D3D12CreateDevice_()(hwAdapter, InFeatureLevel, IID_PPV_ARGS(&_d3d12Device));

        if (result != S_OK)
        {
            LOG_ERROR("Can't create device: {:X}", (UINT) result);
            return false;
        }

        if (hwAdapter != nullptr)
        {
            DXGI_ADAPTER_DESC desc {};
            if (hwAdapter->GetDesc(&desc) == S_OK)
            {
                auto adapterDesc = wstring_to_string(desc.Description);
                LOG_INFO("D3D12Device created with adapter: {}", adapterDesc);
                // State::Instance().DeviceAdapterNames[_d3d12Device] = adapterDesc;
            }
        }
    }

    if (_d3d12CommandQueue == nullptr)
    {
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queueDesc.Type = _commandListType;

        // CreateCommandQueue
        result = _d3d12Device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&_d3d12CommandQueue));

        if (result != S_OK || _d3d12CommandQueue == nullptr)
        {
            LOG_DEBUG("CreateCommandQueue result: {0:x}", result);
            return false;
        }
    }

    return true;
}
