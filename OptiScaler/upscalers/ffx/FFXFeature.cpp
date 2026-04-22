#include <pch.h>
#include <Config.h>
#include "FFXFeature.h"

double FFXFeature::GetDeltaTime()
{
    double currentTime = Util::MillisecondsNow();
    double deltaTime = (currentTime - _lastFrameTime);
    _lastFrameTime = currentTime;
    return deltaTime;
}

FFXFeature::FFXFeature(unsigned int InHandleId, NVSDK_NGX_Parameter* InParameters) : IFeature(InHandleId, InParameters)
{
    _initParameters = SetInitParameters(InParameters);
    _lastFrameTime = Util::MillisecondsNow();
}

FFXFeature::~FFXFeature()
{
    if (!IsInited())
        return;

    SetInit(false);
}
