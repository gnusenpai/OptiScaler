#include "pch.h"
#include "IdentifyGpu.h"
#include "fsr4/FSR4Upgrade.h"

#include <proxies/Dxgi_Proxy.h>
#include "nvapi/NvApiTypes.h"
#include <magic_enum.hpp>

std::optional<std::vector<GpuInformation>> IdentifyGpu::cachedInfo;
std::optional<std::vector<GpuInformation>> IdentifyGpu::cachedInfoNoDxgi;

using Microsoft::WRL::ComPtr;

// TODO: Maybe take into account some other GPU ordering, DxgiFactoryWrappedCalls::EnumAdapters?
constexpr int vendorPriority(VendorId::Value v)
{
    switch (v)
    {
    case VendorId::Nvidia:
        return 0;
    case VendorId::AMD:
        return 1;
    case VendorId::Intel:
        return 2;
    case VendorId::Microsoft:
        return 3;
    default:
        return 4;
    }
}

void sortGpus(std::vector<GpuInformation>& gpus)
{
    std::sort(gpus.begin(), gpus.end(), [](const GpuInformation& a, const GpuInformation& b)
              { return vendorPriority(a.vendorId) < vendorPriority(b.vendorId); });
}

void IdentifyGpu::checkGpuInfo()
{
    cachedInfo = std::vector<GpuInformation> {};
    auto& localCachedInfo = cachedInfo.value();

    // Call init for any case
    DxgiProxy::Init();

    ComPtr<IDXGIFactory> factory = nullptr;
    HRESULT result = DxgiProxy::CreateDxgiFactory_()(__uuidof(factory), &factory);

    if (result != S_OK || factory == nullptr)
    {
        // Will land here if getPrimaryGpu/getAllGpus are called from within DLL_PROCESS_ATTACH
        LOG_ERROR("Failed to create DXGI Factory, GPU info will be inaccurate!");
        return;
    }

    UINT adapterIndex = 0;
    DXGI_ADAPTER_DESC adapterDesc {};
    ComPtr<IDXGIAdapter> adapter;

    while (factory->EnumAdapters(adapterIndex, &adapter) == S_OK)
    {
        if (adapter == nullptr)
        {
            adapterIndex++;
            continue;
        }

        {
            ScopedSkipSpoofing skipSpoofing {};
            result = adapter->GetDesc(&adapterDesc);
        }

        if (result == S_OK)
        {
            GpuInformation gpuInfo;
            gpuInfo.luid = adapterDesc.AdapterLuid;
            gpuInfo.vendorId = (VendorId::Value) adapterDesc.VendorId;
            gpuInfo.deviceId = adapterDesc.DeviceId;
            gpuInfo.dedicatedVramInBytes = adapterDesc.DedicatedVideoMemory;

            std::wstring szName(adapterDesc.Description);
            gpuInfo.name = wstring_to_string(szName);

            // TODO: check if an invalid guid still return S_OK but nullptr
            ComPtr<IDXGIVkInteropDevice> dxgiInterop;
            if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&dxgiInterop))))
                gpuInfo.usesDxvk = true;

            // Needed to be able to query amdxc and check for vkd3d-proton
            if (gpuInfo.vendorId == VendorId::AMD || gpuInfo.usesDxvk)
                D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&gpuInfo.d3d12device));

            localCachedInfo.push_back(std::move(gpuInfo));
        }
        else
        {
            LOG_DEBUG("Can't get description of adapter: {}", adapterIndex);
        }

        adapterIndex++;
    }

    sortGpus(localCachedInfo);

    for (auto& gpuInfo : localCachedInfo)
    {
        ComPtr<ID3D12DXVKInteropDevice> vkd3dInterop;
        if (gpuInfo.usesDxvk && SUCCEEDED(gpuInfo.d3d12device->QueryInterface(IID_PPV_ARGS(&vkd3dInterop))))
            gpuInfo.usesVkd3dProton = true;

        if (gpuInfo.vendorId == VendorId::AMD && gpuInfo.d3d12device)
        {
            auto moduleAmdxc64 = KernelBaseProxy::GetModuleHandleW_()(L"amdxc64.dll");

            if (moduleAmdxc64 == nullptr)
                moduleAmdxc64 = NtdllProxy::LoadLibraryExW_Ldr(L"amdxc64.dll", NULL, 0);

            if (moduleAmdxc64 == nullptr)
                continue;

            ComPtr<IAmdExtD3DFactory> amdExtD3DFactory = nullptr;
            auto AmdExtD3DCreateInterface = (PFN_AmdExtD3DCreateInterface) KernelBaseProxy::GetProcAddress_()(
                moduleAmdxc64, "AmdExtD3DCreateInterface");

            // Kinda questionable, may require reconsideration
            if (Config::Instance()->Fsr4ForceCapable.value_or_default())
                gpuInfo.fsr4Capable = true;

            // Query amdxc for a specific intrinsics support, FSR 4 checks more but hopefully this one is enough
            if (!gpuInfo.fsr4Capable && gpuInfo.d3d12device &&
                SUCCEEDED(AmdExtD3DCreateInterface(gpuInfo.d3d12device, IID_PPV_ARGS(&amdExtD3DFactory))))
            {
                ComPtr<IAmdExtD3DShaderIntrinsics> amdExtD3DShaderIntrinsics = nullptr;

                if (SUCCEEDED(amdExtD3DFactory->CreateInterface(gpuInfo.d3d12device,
                                                                IID_PPV_ARGS(&amdExtD3DShaderIntrinsics))))
                {
                    HRESULT float8support =
                        amdExtD3DShaderIntrinsics->CheckSupport(AmdExtD3DShaderIntrinsicsSupport_Float8Conversion);
                    gpuInfo.fsr4Capable = float8support == S_OK;
                }
            }

            // Query vkd3d-proton for extensions it's using to look for the required one for FSR 4
            if (!gpuInfo.fsr4Capable && gpuInfo.usesVkd3dProton)
            {
                UINT extensionCount = 0;

                if (SUCCEEDED(vkd3dInterop->GetDeviceExtensions(&extensionCount, nullptr)) && extensionCount > 0)
                {
                    std::vector<const char*> exts(extensionCount);

                    if (SUCCEEDED(vkd3dInterop->GetDeviceExtensions(&extensionCount, exts.data())))
                    {
                        for (UINT i = 0; i < extensionCount; i++)
                        {
                            // Only RDNA4+
                            if (!strcmp("VK_EXT_shader_float8", exts[i]))
                            {
                                gpuInfo.fsr4Capable = true;
                                break;
                            }
                        }
                    }
                }
            }

            // Pre-RDNA4 GPUs on Linux can support FSR 4 but require a special envvar
            // check for the envvar and assume everything else is also setup for FSR 4 to work on those cards
            if (!gpuInfo.fsr4Capable)
            {
                const char* envvar = getenv("DXIL_SPIRV_CONFIG");
                if (envvar && strstr(envvar, "wmma_rdna3_workaround"))
                    gpuInfo.fsr4Capable = true;
            }

            // TODO: could now try to ask amdxcffx for FSR 4 and see if it returns it
            // but our FSR 4 upgrade code call this function so it gets complicated
        }
        else if (gpuInfo.vendorId == VendorId::Nvidia)
        {
            queryNvapi(gpuInfo);
        }

        if (gpuInfo.d3d12device)
        {
            gpuInfo.d3d12device->Release();
            gpuInfo.d3d12device = nullptr;
        }
    }
}

// !!! Doesn't fill out FSR  4 capability and dxvk/vkd3d-proton usages !!!
void IdentifyGpu::checkGpuInfoNoDxgi()
{
    cachedInfoNoDxgi = std::vector<GpuInformation> {};
    auto& localCachedInfo = cachedInfoNoDxgi.value();

    VkApplicationInfo appInfo {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "AdapterQuery";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "None";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo createInfo {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
        return;

    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        vkDestroyInstance(instance, nullptr);
        return;
    }

    // ScopedSkipSpoofing skipSpoofing {};

    std::vector<VkPhysicalDevice> physicalDevices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, physicalDevices.data());

    for (auto physicalDevice : physicalDevices)
    {
        VkPhysicalDeviceIDProperties idProps {};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2 {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;

        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        VkPhysicalDeviceMemoryProperties memProps {};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

        GpuInformation gpuInfo;
        for (uint32_t i = 0; i < memProps.memoryHeapCount; ++i)
        {
            if (memProps.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                gpuInfo.dedicatedVramInBytes += memProps.memoryHeaps[i].size;
        }

        if (idProps.deviceLUIDValid == VK_TRUE)
            memcpy(&gpuInfo.luid, idProps.deviceLUID, VK_LUID_SIZE);

        gpuInfo.vendorId = (VendorId::Value) props2.properties.vendorID;
        gpuInfo.deviceId = props2.properties.deviceID;
        gpuInfo.name = std::string(props2.properties.deviceName);

        localCachedInfo.push_back(std::move(gpuInfo));
    }

    vkDestroyInstance(instance, nullptr);
    sortGpus(localCachedInfo);

    for (auto& gpuInfo : localCachedInfo)
    {
        if (gpuInfo.vendorId == VendorId::Nvidia)
            queryNvapi(gpuInfo);
    }
}

void IdentifyGpu::queryNvapi(GpuInformation& gpuInfo)
{
    bool loadedHere = false;
    auto nvapiModule = KernelBaseProxy::GetModuleHandleW_()(L"nvapi64.dll");

    if (!nvapiModule)
    {
        nvapiModule = NtdllProxy::LoadLibraryExW_Ldr(L"nvapi64.dll", NULL, LOAD_LIBRARY_SEARCH_SYSTEM32);
        loadedHere = true;
    }

    // No nvapi, should not be nvidia, possibly external spoofing
    if (!nvapiModule)
        return;

    if (auto o_NvAPI_QueryInterface =
            (PFN_NvApi_QueryInterface) KernelBaseProxy::GetProcAddress_()(nvapiModule, "nvapi_QueryInterface"))
    {
        // Check for fakenvapi in system32, assume it's not nvidia if found
        if (o_NvAPI_QueryInterface(GET_ID(Fake_InformFGState)))
            return;

        // Handle we want to grab
        NvPhysicalGpuHandle hPhysicalGpu {};

        // Grab logical GPUs to extract coresponding LUID
        auto* getLogicalGPUs = GET_INTERFACE(NvAPI_SYS_GetLogicalGPUs, o_NvAPI_QueryInterface);
        NV_LOGICAL_GPUS logicalGpus {};
        logicalGpus.version = NV_LOGICAL_GPUS_VER;
        if (getLogicalGPUs)
        {
            if (auto result = getLogicalGPUs(&logicalGpus); result != NVAPI_OK)
                LOG_ERROR("NvAPI_SYS_GetLogicalGPUs failed: {}", magic_enum::enum_name(result));
        }

        auto* getLogicalGpuInfo = GET_INTERFACE(NvAPI_GPU_GetLogicalGpuInfo, o_NvAPI_QueryInterface);

        if (getLogicalGpuInfo)
        {
            for (uint32_t i = 0; i < logicalGpus.gpuHandleCount; i++)
            {
                LUID luid;
                NV_LOGICAL_GPU_DATA logicalGpuData {};
                logicalGpuData.pOSAdapterId = &luid;
                logicalGpuData.version = NV_LOGICAL_GPU_DATA_VER;
                auto logicalGpu = logicalGpus.gpuHandleData[i].hLogicalGpu;

                if (auto result = getLogicalGpuInfo(logicalGpu, &logicalGpuData); result != NVAPI_OK)
                    LOG_ERROR("NvAPI_GPU_GetLogicalGpuInfo failed: {}", magic_enum::enum_name(result));

                // We are looking at the correct GPU for this gpuInfo.luid
                if (luid.HighPart == gpuInfo.luid.HighPart && luid.LowPart == gpuInfo.luid.LowPart &&
                    logicalGpuData.physicalGpuCount > 0)
                {
                    if (logicalGpuData.physicalGpuCount > 1)
                        LOG_WARN("A logical GPU has more than a single physical GPU, we are only checking one");

                    hPhysicalGpu = logicalGpuData.physicalGpuHandles[0];
                }
            }
        }

        auto* getArchInfo = GET_INTERFACE(NvAPI_GPU_GetArchInfo, o_NvAPI_QueryInterface);
        gpuInfo.nvidiaArchInfo.version = NV_GPU_ARCH_INFO_VER;
        if (getArchInfo && hPhysicalGpu && getArchInfo(hPhysicalGpu, &gpuInfo.nvidiaArchInfo) != NVAPI_OK)
            LOG_ERROR("Couldn't get GPU Architecture");
    }

    if (loadedHere)
        NtdllProxy::FreeLibrary_Ldr(nvapiModule);

    LOG_DEBUG("Detected architecture: {}", magic_enum::enum_name(gpuInfo.nvidiaArchInfo.architecture_id));

    gpuInfo.dlssCapable = gpuInfo.nvidiaArchInfo.architecture_id >= NV_GPU_ARCHITECTURE_TU100;
}

std::vector<GpuInformation> IdentifyGpu::getAllGpus()
{
    // TODO: mutex
    if (!cachedInfo.has_value())
        checkGpuInfo();

    return cachedInfo.value();
}

GpuInformation IdentifyGpu::getPrimaryGpu()
{
    auto allGpus = getAllGpus();
    return allGpus.size() > 0 ? allGpus[0] : GpuInformation {};
}

// !!! Use the NoDxgi variants only inside DLL_PROCESS_ATTACH as they provide incomplete data !!!
std::vector<GpuInformation> IdentifyGpu::getAllGpusNoDxgi()
{
    if (!cachedInfoNoDxgi.has_value())
        checkGpuInfoNoDxgi();

    return cachedInfoNoDxgi.value();
}

// !!! Use the NoDxgi variants only inside DLL_PROCESS_ATTACH as they provide incomplete data !!!
GpuInformation IdentifyGpu::getPrimaryGpuNoDxgi()
{
    auto allGpus = getAllGpusNoDxgi();
    return allGpus.size() > 0 ? allGpus[0] : GpuInformation {};
}