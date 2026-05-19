#include "pch.h"
#include "UeLowLatency.h"
#include <hooks/Reflex_Hooks.h>

#include <low_latency/low_latency_tech/ll_antilag2.h>
// #include <low_latency/low_latency_tech/ll_xell.h>
#include <low_latency/low_latency_tech/ll_latencyflex.h>

void UeLowLatency::init()
{
    if (lowLatencyFeature)
        return;

    lowLatencyFeature = new AntiLag2();

    // lowLatencyFeature = new LatencyFlex();
    // Config::Instance()->FN_LatencyFlexMode.set_volatile_value(LFXMode::ReflexIDs);

    bool inited = false;

    if (auto dx11device = State::Instance().currentD3D11Device; dx11device)
    {
        inited = lowLatencyFeature->init(dx11device);
    }

    if (auto dx12device = State::Instance().currentD3D12Device; !inited && dx12device)
    {
        inited = lowLatencyFeature->init(dx12device);
    }

    State::Instance().usingUeLL = inited;

    if (inited)
    {
        LOG_DEBUG("Inited UeLowLatency");

        SleepMode mode {};
        mode.low_latency_enabled = true;
        lowLatencyFeature->set_sleep_mode(&mode);

        ReflexHooks::setFPSLimit(0);
    }
    else
    {
        delete lowLatencyFeature;
        lowLatencyFeature = nullptr;
    }
}

void UeLowLatency::tickStart(int64_t frameId, float DeltaSeconds, bool bIdleMode)
{
    LOG_FUNC();

    if (!lowLatencyFeature)
        UeLowLatency::init();

    lowLatencyFeature->sleep();

    MarkerParams marker {};
    marker.marker_type = MarkerType::SIMULATION_START;
    marker.frame_id = frameId;
    lowLatencyFeature->set_marker(State::Instance().currentD3D12Device, marker);
}

void UeLowLatency::tickEnd(int64_t frameId, float DeltaSeconds, bool bIdleMode)
{
    LOG_FUNC();

    MarkerParams marker {};
    marker.marker_type = MarkerType::SIMULATION_END;
    marker.frame_id = frameId;
    lowLatencyFeature->set_marker(State::Instance().currentD3D12Device, marker);
}
