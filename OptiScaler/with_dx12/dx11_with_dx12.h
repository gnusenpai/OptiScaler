#pragma once

#include <shaders/depth_transfer/DT_Dx11.h>

#include <d3d12.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>

class Dx11WithDx12
{
  private:
    inline static ID3D11Device5* Dx11Device = nullptr;
    inline static ID3D11DeviceContext4* Dx11DeviceContext = nullptr;

    inline static ID3D12Device* Dx12Device = nullptr;
    inline static ID3D12CommandQueue* Dx12CommandQueue = nullptr;

    inline static std::unique_ptr<DepthTransfer_Dx11> DT = nullptr;

  public:
    // Dx11w12 part
    using D3D11_TEXTURE2D_DESC_C = struct D3D11_TEXTURE2D_DESC_C
    {
        UINT Width = 0;
        UINT Height = 0;
        DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;
        UINT BindFlags = 0;
        UINT MiscFlags = 0;
    };

    using D3D11_TEXTURE2D_RESOURCE_C = struct D3D11_TEXTURE2D_RESOURCE_C
    {
        D3D11_TEXTURE2D_DESC_C Desc = {};
        ID3D11Texture2D* SourceTexture = nullptr;
        ID3D11Texture2D* SharedTexture = nullptr;
        ID3D12Resource* Dx12Resource = nullptr;
        HANDLE Dx11Handle = NULL;
        HANDLE Dx12Handle = NULL;
    };

    static bool CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource, bool InCopy,
                                      bool InDepth, bool InDontUseNTShared);

    static bool OpenHandle(std::string name, ID3D12Device* dx12Device, ID3D11Resource* resource,
                           D3D11_TEXTURE2D_RESOURCE_C* shared);

    static void Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                     ID3D12CommandQueue* dx12CommandQueue);
};
