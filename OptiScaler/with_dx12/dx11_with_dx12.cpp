#include <pch.h>

#include "dx11_with_dx12.h"

#define ASSIGN_DESC(dest, src)                                                                                         \
    dest.Width = src.Width;                                                                                            \
    dest.Height = src.Height;                                                                                          \
    dest.Format = src.Format;                                                                                          \
    dest.BindFlags = src.BindFlags;                                                                                    \
    dest.MiscFlags = src.MiscFlags;

void Dx11WithDx12::Init(ID3D11Device* dx11Device, ID3D11DeviceContext* dx11Context, ID3D12Device* dx12Device,
                        ID3D12CommandQueue* dx12CommandQueue)
{
    if (dx11Device != Dx11Device)
    {
        auto contextResult = dx11Context->QueryInterface(IID_PPV_ARGS(&Dx11DeviceContext));
        if (contextResult != S_OK)
        {
            LOG_ERROR("QueryInterface ID3D11DeviceContext4 result: {0:x}", contextResult);
            return;
        }
        else
        {
            Dx11DeviceContext->Release();
        }

        if (dx11Device == nullptr)
            Dx11DeviceContext->GetDevice(&dx11Device);

        auto dx11DeviceResult = dx11Device->QueryInterface(IID_PPV_ARGS(&Dx11Device));
        if (dx11DeviceResult != S_OK)
        {
            LOG_ERROR("QueryInterface ID3D11Device5 result: {0:x}", dx11DeviceResult);
            return;
        }
        else
        {
            Dx11Device->Release();
        }

        if (DT != nullptr || DT.get() != nullptr)
            DT.reset();

        DT = std::make_unique<DepthTransfer_Dx11>("DT", Dx11Device);
    }

    Dx12Device = dx12Device;
    Dx12CommandQueue = dx12CommandQueue;
}

bool Dx11WithDx12::CopyTextureFrom11To12(ID3D11Resource* InResource, D3D11_TEXTURE2D_RESOURCE_C* OutResource,
                                         bool InCopy, bool InDepth, bool InDontUseNTShared)
{
    ID3D11Texture2D* originalTexture = nullptr;
    D3D11_TEXTURE2D_DESC desc {};

    auto result = InResource->QueryInterface(IID_PPV_ARGS(&originalTexture));

    if (result != S_OK || originalTexture == nullptr)
        return false;

    originalTexture->GetDesc(&desc);

    // check shared nt handle usage later
    if (!(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) && !(desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) &&
        !InDontUseNTShared)
    {
        if (desc.Width != OutResource->Desc.Width || desc.Height != OutResource->Desc.Height ||
            desc.Format != OutResource->Desc.Format || desc.BindFlags != OutResource->Desc.BindFlags ||
            OutResource->SharedTexture == nullptr ||
            !(OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
        {
            if (OutResource->SharedTexture != nullptr)
            {
                OutResource->SharedTexture->Release();

                if (OutResource->Dx12Handle != NULL &&
                    (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
                    CloseHandle(OutResource->Dx12Handle);

                OutResource->Dx11Handle = NULL;
                OutResource->Dx12Handle = NULL;
            }

            desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            desc.Usage = D3D11_USAGE_DEFAULT;

            ASSIGN_DESC(OutResource->Desc, desc);

            result = Dx11Device->CreateTexture2D(&desc, nullptr, &OutResource->SharedTexture);

            if (result != S_OK)
            {
                LOG_ERROR("CreateTexture2D error: {0:x}", result);
                return false;
            }

            IDXGIResource1* resource;

            result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                return false;
            }

            // Get shared handle
            DWORD access = DXGI_SHARED_RESOURCE_READ;

            if (!InCopy)
                access |= DXGI_SHARED_RESOURCE_WRITE;

            result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);

            if (result != S_OK)
            {
                LOG_ERROR("GetSharedHandle error: {0:x}", result);
                return false;
            }

            resource->Release();
        }

        if (InCopy && OutResource->SharedTexture != nullptr)
            Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
    }
    else if ((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) == 0 && InDontUseNTShared)
    {
        if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS || desc.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
        {
            if (DT == nullptr || DT.get() == nullptr)
                DT = std::make_unique<DepthTransfer_Dx11>("DT", Dx11Device);

            if (DT->Buffer() == nullptr)
                DT->CreateBufferResource(Dx11Device, InResource);

            if (DT->Dispatch(Dx11Device, Dx11DeviceContext, originalTexture, DT->Buffer()))
            {
                IDXGIResource1* resource = nullptr;
                result = DT->Buffer()->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK || resource == nullptr)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    return false;
                }

                OutResource->SharedTexture = DT->Buffer();

                // Get shared handle
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);

                if (result != S_OK)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    resource->Release();
                    return false;
                }

                resource->Release();
            }
        }
        else
        {
            if (desc.Width != OutResource->Desc.Width || desc.Height != OutResource->Desc.Height ||
                desc.Format != OutResource->Desc.Format || desc.BindFlags != OutResource->Desc.BindFlags ||
                OutResource->SharedTexture == nullptr ||
                (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
            {
                if (OutResource->SharedTexture != nullptr)
                {
                    OutResource->SharedTexture->Release();

                    if (OutResource->Dx12Handle != NULL &&
                        (OutResource->Desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE))
                        CloseHandle(OutResource->Dx12Handle);

                    OutResource->Dx11Handle = NULL;
                    OutResource->Dx12Handle = NULL;
                }

                if (desc.Format == DXGI_FORMAT_R24G8_TYPELESS)
                    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

                desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
                ASSIGN_DESC(OutResource->Desc, desc);
                desc.Usage = D3D11_USAGE_DEFAULT;

                result = Dx11Device->CreateTexture2D(&desc, nullptr, &OutResource->SharedTexture);

                IDXGIResource1* resource;
                result = OutResource->SharedTexture->QueryInterface(IID_PPV_ARGS(&resource));

                if (result != S_OK)
                {
                    LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                    return false;
                }

                // Get shared handle
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);

                if (result != S_OK)
                {
                    LOG_ERROR("GetSharedHandle error: {0:x}", result);
                    resource->Release();
                    return false;
                }

                resource->Release();
            }

            if (InCopy && OutResource->SharedTexture != nullptr)
                Dx11DeviceContext->CopyResource(OutResource->SharedTexture, InResource);
        }
    }
    else
    {
        if (OutResource->SharedTexture != InResource)
        {
            IDXGIResource1* resource;

            result = originalTexture->QueryInterface(IID_PPV_ARGS(&resource));

            if (result != S_OK || resource == nullptr)
            {
                LOG_ERROR("QueryInterface(resource) error: {0:x}", result);
                return false;
            }

            // Get shared handle
            if ((desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED) != 0 &&
                (desc.MiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0)
            {
                DWORD access = DXGI_SHARED_RESOURCE_READ;

                if (!InCopy)
                    access |= DXGI_SHARED_RESOURCE_WRITE;

                result = resource->CreateSharedHandle(NULL, access, NULL, &OutResource->Dx11Handle);
            }
            else
            {
                result = resource->GetSharedHandle(&OutResource->Dx11Handle);
            }

            if (result != S_OK)
            {
                LOG_ERROR("GetSharedHandle error: {0:x}", result);
                return false;
            }

            resource->Release();

            OutResource->SharedTexture = (ID3D11Texture2D*) InResource;
        }
    }

    originalTexture->Release();
    return true;
}

bool Dx11WithDx12::OpenHandle(std::string name, ID3D12Device* dx12Device, ID3D11Resource* resource,
                              D3D11_TEXTURE2D_RESOURCE_C* shared)
{
    if (resource != nullptr && shared->Dx12Handle != shared->Dx11Handle)
    {
        if (shared->Dx12Handle != NULL)
            CloseHandle(shared->Dx12Handle);

        auto result = dx12Device->OpenSharedHandle(shared->Dx11Handle, IID_PPV_ARGS(&shared->Dx12Resource));

        if (result != S_OK)
        {
            LOG_ERROR("{} OpenSharedHandle error: {:X}", name, result);
            return false;
        }

        shared->Dx12Handle = shared->Dx11Handle;
    }

    return true;
}
