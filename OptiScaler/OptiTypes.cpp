#include "pch.h"

#include "OptiTypes.h"
#include <misc/IdentifyGpu.h>
#include <unordered_map>

std::string UpscalerDisplayName(Upscaler upscaler, API api)
{
    bool fsr4Capable = IdentifyGpu::getPrimaryGpu().fsr4Capable;

    switch (upscaler)
    {
    case Upscaler::FSR21:
        return "FSR 2.1.2";

    case Upscaler::FSR22:
        return "FSR 2.2.1";

    case Upscaler::FFX:
        if (fsr4Capable && api == API::DX12)
            return "FSR 3.X/4";
        else
            return "FSR 3.X";

    case Upscaler::FSR21_11on12:
        return "FSR 2.1.2 w/Dx12";

    case Upscaler::FSR22_11on12:
        return "FSR 2.2.1 w/Dx12";

    case Upscaler::FFX_11on12:
        if (fsr4Capable)
            return "FSR 3.X/4 w/Dx12";
        else
            return "FSR 3.X w/Dx12";

    case Upscaler::XeSS:
        return "XeSS";

    case Upscaler::XeSS_11on12:
        return "XeSS w/Dx12";

    case Upscaler::DLSS:
        return "DLSS";
    }

    return "????";
}

// Converts enum to the string codes for config
std::string UpscalerToCode(Upscaler upscaler)
{
    switch (upscaler)
    {
    case Upscaler::XeSS:
        return "xess";
    case Upscaler::XeSS_11on12:
        return "xess_12";
    case Upscaler::FSR21:
        return "fsr21";
    case Upscaler::FSR21_11on12:
        return "fsr21_12";
    case Upscaler::FSR22:
        return "fsr22";
    case Upscaler::FSR22_11on12:
        return "fsr22_12";
    case Upscaler::FFX:
        return "fsr31";
    case Upscaler::FFX_11on12:
        return "fsr31_12";
    case Upscaler::DLSS:
        return "dlss";
    case Upscaler::DLSSD:
        return "dlssd";
    default: // Upscaler::Reset and unknown
        return "";
    }
}

// Converts string codes into enum for config
Upscaler CodeToUpscaler(const std::string& code)
{
    static const std::unordered_map<std::string, Upscaler> mapping = {
        { "xess", Upscaler::XeSS },   { "xess_12", Upscaler::XeSS_11on12 },
        { "fsr21", Upscaler::FSR21 }, { "fsr21_12", Upscaler::FSR21_11on12 },
        { "fsr22", Upscaler::FSR22 }, { "fsr22_12", Upscaler::FSR22_11on12 },
        { "fsr31", Upscaler::FFX }, { "fsr31_12", Upscaler::FFX_11on12 },
        { "dlss", Upscaler::DLSS },   { "dlssd", Upscaler::DLSSD }
    };

    auto it = mapping.find(code);
    return (it != mapping.end()) ? it->second : Upscaler::Reset;
}