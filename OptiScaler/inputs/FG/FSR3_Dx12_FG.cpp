#include "FSR3_Dx12_FG.h"

#include "Config.h"
#include "Util.h"

#include "resource.h"
#include "NVNGX_Parameter.h"

#include <proxies/KernelBase_Proxy.h>

#include <scanner/scanner.h>

#include "detours/detours.h"

#include "fsr3/dx12/ffx_dx12.h"
#include "fsr3/ffx_fsr3.h"
#include "fsr3/ffx_frameinterpolation.h"

typedef Fsr3::FfxErrorCode (*PFN_ffxReplaceSwapchainForFrameinterpolationDX12)(Fsr3::FfxCommandQueue gameQueue,
                                                                               Fsr3::FfxSwapchain& gameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxCreateFrameinterpolationSwapchainDX12)(DXGI_SWAP_CHAIN_DESC* desc,
                                                                           ID3D12CommandQueue* queue,
                                                                           IDXGIFactory* dxgiFactory,
                                                                           Fsr3::FfxSwapchain& outGameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxCreateFrameinterpolationSwapchainForHwndDX12)(
    HWND hWnd, DXGI_SWAP_CHAIN_DESC1* desc1, DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreenDesc, ID3D12CommandQueue* queue,
    IDXGIFactory* dxgiFactory, Fsr3::FfxSwapchain& outGameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxWaitForPresents)(Fsr3::FfxSwapchain gameSwapChain);
typedef Fsr3::FfxErrorCode (*PFN_ffxRegisterFrameinterpolationUiResourceDX12)(Fsr3::FfxSwapchain gameSwapChain,
                                                                              Fsr3::FfxResource uiResource);
typedef Fsr3::FfxErrorCode (*PFN_ffxGetFrameinterpolationCommandlistDX12)(Fsr3::FfxSwapchain gameSwapChain,
                                                                          Fsr3::FfxCommandList& gameCommandlist);
typedef Fsr3::FfxResource (*PFN_ffxGetFrameinterpolationTextureDX12)(Fsr3::FfxSwapchain gameSwapChain);

typedef Fsr3::FfxErrorCode (*PFN_ffxSetFrameGenerationConfigToSwapchainDX12)(Fsr3::FfxFrameGenerationConfig* config);

typedef Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationContextCreate)(
    FfxFrameInterpolationContext* context, FfxFrameInterpolationContextDescription* contextDescription);

typedef Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationDispatch)(FfxFrameInterpolationContext* context,
                                                                FfxFrameInterpolationDispatchDescription* params);

typedef Fsr3::FfxErrorCode (*PFN_ffxFrameInterpolationContextDestroy)(FfxFrameInterpolationContext* context);

typedef Fsr3::FfxErrorCode (*PFN_ffxFsr3ConfigureFrameGeneration)(void* context,
                                                                  Fsr3::FfxFrameGenerationConfig* config);

void HookFSR3FGExeInputs() {}

void HookFSR3FGInputs(HMODULE module) {}
