#pragma once
#include <low_latency/low_latency_tech/low_latency_tech.h>
#include <xell.h>

enum class LowLatencyInput
{
    None,
    AntiLag2,
    Reflex,
    XeLL
};

enum class InputMarkerMode
{
    NoMarkers,
    PresentStartOnly,
    FullMarkers, // TODO: add something about xell's partial async markers
};

struct InputContext
{
    LowLatencyInput caller;
    // bool sleepWithFrameId;
    bool noFrameId;
    InputMarkerMode markerMode;
};

enum class InputResult : uint32_t
{
    Ok,
    UsingDifferentInput,
    InputNotSupported,
    InvalidParameter,
    LowLatencyUpdateFail,
    GenericError,
};

struct TimingData
{
    std::optional<TimingEntry> timeRange; // in ns, value stored in length
    std::optional<TimingEntry> simulation;
    std::optional<TimingEntry> renderSubmit;
    std::optional<TimingEntry> present;
    std::optional<TimingEntry> driver;
    std::optional<TimingEntry> osRenderQueue;
    std::optional<TimingEntry> gpuRender;
};

class InputCommon
{
    inline static std::atomic<std::shared_ptr<LowLatencyTech>> currently_active_tech;
    inline static FrameReport frame_reports[FRAME_REPORTS_BUFFER_SIZE] {};

    inline static LowLatencyInput activeInput = LowLatencyInput::None;
    inline static LowLatencyMode activeOutput = LowLatencyMode::None;
    inline static bool enabled = false;

    static bool update_low_latency_tech(IUnknown* pDevice, std::optional<LowLatencyMode> mode = std::nullopt);
    static void add_marker_to_report(const MarkerParams& marker_params);

  public:
    static InputResult set_low_latency_tech(IUnknown* pDevice, LowLatencyMode mode);

    // TODO: Ignore all calls that are not coming for the activeInput
    static InputResult sleep(const InputContext& inputContext, IUnknown* pDevice,
                             std::optional<uint32_t> frame_id = std::nullopt);
    static InputResult set_marker(const InputContext& inputContext, IUnknown* pDevice,
                                  const MarkerParams& marker_params);
    static InputResult set_async_marker(const InputContext& inputContext, ID3D12CommandQueue* pCommandQueue,
                                        const MarkerParams& marker_params);
    static InputResult set_sleep_mode(const InputContext& inputContext, IUnknown* pDevice, SleepMode* sleep_mode);
    static InputResult get_sleep_status(const InputContext& inputContext, IUnknown* pDevice, SleepParams* sleep_params);
    static InputResult
    get_latency(const InputContext& inputContext, IUnknown* pDev,
                void* latency_params); // NV_LATENCY_RESULT_PARAMS* for reflex, xell_frame_report_t* for xell,
    static bool get_timing_data(TimingData& timingDataOut);
    // passthrough when possible, fillout with local frame_reports if not

    // XeLL-specific calls for passthrough, TODO: only allow when caller == activeOutput == activeInput == XeLL
    static xell_result_t
    pass_xellD3D12SetAppQueue(const InputContext& inputContext,
                              ID3D12CommandQueue* appQueue); // if it's ID3D12CommandQueue* then some translation with
                                                             // async markers can be made
    static xell_result_t pass_xellSetDisplayInfo(const InputContext& inputContext, void* displayInfo);
    static xell_result_t pass_xellSetFgEnabled(const InputContext& inputContext, uint32_t param1, uint32_t param2);
    static xell_result_t pass_xellSetGeneratedFramesCount(const InputContext& inputContext, uint32_t param1,
                                                          uint32_t framesCount);
    static xell_result_t pass_xellGetLastPresentStartFrameId(const InputContext& inputContext,
                                                             uint32_t* p_frame_id); // Maybe not needed
};
