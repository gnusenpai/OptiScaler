#pragma once
#include "FFXFeature.h"
#include <upscalers/IFeature_Dx11wDx12.h>

#include "dx12/ffx_api_dx12.h"
#include "proxies/FfxApi_Proxy.h"

class FFXFeatureDx11on12 : public FFXFeature, public IFeature_Dx11wDx12
{
  private:
    bool _baseInit = false;
    NVSDK_NGX_Parameter* SetParameters(NVSDK_NGX_Parameter* InParameters);

  protected:
    bool InitFSR3(const NVSDK_NGX_Parameter* InParameters);

  public:
    std::string Name() const { return "FSR3 w/Dx12"; }
    feature_version Version() override { return FFXFeature::Version(); }

    FFXFeatureDx11on12(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters);

    bool Init(ID3D11Device* InDevice, ID3D11DeviceContext* InContext, NVSDK_NGX_Parameter* InParameters) override;
    bool Evaluate(ID3D11DeviceContext* InDeviceContext, NVSDK_NGX_Parameter* InParameters) override;

    bool IsWithDx12() final { return true; }

    ~FFXFeatureDx11on12()
    {
        if (State::Instance().isShuttingDown)
            return;

        if (_context != nullptr)
            FfxApiProxy::D3D12_DestroyContext(&_context, NULL);
    }
};
