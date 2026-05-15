#pragma once

#include <low_latency/low_latency_tech/low_latency_tech.h>

typedef void (*PFN_FLTickExternal)(int64_t frameId, float DeltaSeconds, bool bIdleMode);
typedef void (*PFN_setTickCallback)(PFN_FLTickExternal callback);

class UeLowLatency
{
    inline static LowLatencyTech* lowLatencyFeature = nullptr;

  public:
    static void init();
    static void tickEnd(int64_t frameId, float DeltaSeconds, bool bIdleMode);
    static void tickStart(int64_t frameId, float DeltaSeconds, bool bIdleMode);
};
