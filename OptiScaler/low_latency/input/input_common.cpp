#include "pch.h"
#include "input_common.h"
#include <low_latency/low_latency_tech/ll_xell.h>

// private
bool InputCommon::update_low_latency_tech(IUnknown* pDevice, std::optional<LowLatencyMode> mode)
{
    LowLatencyMode desiredMode = LowLatencyMode::None;

    if (!pDevice)
    {
        // Some outputs might call sleep with nullptr
        if (activeOutput != LowLatencyMode::None && !mode.has_value())
            return true; // Allow it if we already have an output and not trying to set manually
        else
            return false;
    }

    if (mode.has_value())
        desiredMode = mode.value();

    if (desiredMode != LowLatencyMode::None && desiredMode == activeOutput)
    {
        // No need to do anything
        return true;
    }

    // TODO: Add system for selecting input
    if (activeInput == LowLatencyInput::Reflex)
        return true;
    activeInput = LowLatencyInput::Reflex;

    // TODO: init correct currently_active_tech, desiredMode == None -> Auto
    auto new_tech_xell = std::make_shared<XeLL>();
    if (new_tech_xell->init(pDevice))
    {
        LOG_INFO("LowLatency algo: XeLL");
        currently_active_tech.store(std::move(new_tech_xell));
    }

    if (auto current_tech = currently_active_tech.load())
    {
        activeOutput = current_tech->get_mode();
        return true;
    }

    LOG_ERROR("No Low Latency Tech selected");
    return false;
}

void InputCommon::add_marker_to_report(const MarkerParams& marker_params)
{
    auto current_timestamp = get_timestamp() / 1000;
    static auto last_sim_start = current_timestamp;
    static auto _2nd_last_sim_start = current_timestamp;
    auto current_report = &frame_reports[marker_params.frame_id % FRAME_REPORTS_BUFFER_SIZE];

    if (current_report->frameID != marker_params.frame_id)
    {
        *current_report = FrameReport {};
    }

    current_report->frameID = marker_params.frame_id;
    current_report->gpuFrameTimeUs = (uint32_t) (last_sim_start - _2nd_last_sim_start);
    current_report->gpuActiveRenderTimeUs = 100;
    current_report->driverStartTime = current_timestamp;
    current_report->driverEndTime = current_timestamp + 100;
    current_report->gpuRenderStartTime = current_timestamp;
    current_report->gpuRenderEndTime = current_timestamp + 100;
    current_report->osRenderQueueStartTime = current_timestamp;
    current_report->osRenderQueueEndTime = current_timestamp + 100;
    switch (marker_params.marker_type)
    {
    case MarkerType::SIMULATION_START:
        _2nd_last_sim_start = last_sim_start;
        last_sim_start = get_timestamp() / 1000;
        current_report->simStartTime = last_sim_start;
        break;
    case MarkerType::SIMULATION_END:
        current_report->simEndTime = get_timestamp() / 1000;
        break;
    case MarkerType::RENDERSUBMIT_START:
        current_report->renderSubmitStartTime = get_timestamp() / 1000;
        break;
    case MarkerType::RENDERSUBMIT_END:
        current_report->renderSubmitEndTime = get_timestamp() / 1000;
        break;
    case MarkerType::PRESENT_START:
        current_report->presentStartTime = get_timestamp() / 1000;
        break;
    case MarkerType::PRESENT_END:
        current_report->presentEndTime = get_timestamp() / 1000;
        break;
    case MarkerType::INPUT_SAMPLE:
        current_report->inputSampleTime = get_timestamp() / 1000;
        break;
    default:
        break;
    }
}

// public
InputResult InputCommon::set_low_latency_tech(IUnknown* pDevice, LowLatencyMode mode)
{
    if (!update_low_latency_tech(pDevice, mode))
        return InputResult::LowLatencyUpdateFail;

    return InputResult::Ok;
}

InputResult InputCommon::sleep(const InputContext& inputContext, IUnknown* pDevice, std::optional<uint32_t> frame_id)
{
    if (!update_low_latency_tech(pDevice))
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.caller != activeInput)
        return InputResult::UsingDifferentInput;

    if (auto current_tech = currently_active_tech.load())
        current_tech->sleep(frame_id);
    else
        return InputResult::GenericError;

    return InputResult::Ok;
}

InputResult InputCommon::set_marker(const InputContext& inputContext, IUnknown* pDevice,
                                    const MarkerParams& marker_params)
{
    if (!update_low_latency_tech(pDevice))
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.caller != activeInput)
        return InputResult::UsingDifferentInput;

    if (inputContext.markerMode == InputMarkerMode::NoMarkers)
    {
        return InputResult::InputNotSupported;
    }
    else if (inputContext.markerMode == InputMarkerMode::PresentStartOnly &&
             marker_params.marker_type != MarkerType::PRESENT_START)
    {
        return InputResult::InputNotSupported;
    }

    // update_effective_fg_state();

    // update_enabled_override();

    add_marker_to_report(marker_params);

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_marker(pDevice, marker_params);
    else
        return InputResult::GenericError;

    LOG_TRACE_LOWLATENCY("{}: {}", magic_enum::enum_name(marker_params.marker_type), marker_params.frame_id);

    return InputResult::Ok;
}

InputResult InputCommon::set_async_marker(const InputContext& inputContext, ID3D12CommandQueue* pCommandQueue,
                                          const MarkerParams& marker_params)
{
    // Always allow XeLL's async markers through
    bool xellAsyncMarker = inputContext.caller == LowLatencyInput::XeLL &&
                           (marker_params.marker_type == MarkerType::OUT_OF_BAND_RENDERSUBMIT_START ||
                            marker_params.marker_type == MarkerType::OUT_OF_BAND_RENDERSUBMIT_END);

    if (inputContext.caller != activeInput && !xellAsyncMarker)
        return InputResult::UsingDifferentInput;

    if (!currently_active_tech.load()) // can't init using ID3D12CommandQueue, can only check if available
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.markerMode == InputMarkerMode::NoMarkers)
    {
        return InputResult::InputNotSupported;
    }
    else if (inputContext.markerMode == InputMarkerMode::PresentStartOnly &&
             marker_params.marker_type != MarkerType::OUT_OF_BAND_PRESENT_START)
    {
        return InputResult::InputNotSupported;
    }

    // TODO: could consider adding async markers to the report but would require some rewriting
    // add_marker_to_report(marker_params);

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_async_marker(pCommandQueue, marker_params);
    else
        return InputResult::GenericError;

    LOG_TRACE_LOWLATENCY("{}: {}", magic_enum::enum_name(marker_params.marker_type), marker_params.frame_id);

    return InputResult::Ok;
}

// TODO: impl
InputResult InputCommon::set_sleep_mode(const InputContext& inputContext, IUnknown* pDevice, SleepMode* sleep_mode)
{
    if (!update_low_latency_tech(pDevice))
        return InputResult::LowLatencyUpdateFail;

    if (inputContext.caller != activeInput)
        return InputResult::UsingDifferentInput;

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_sleep_mode(sleep_mode);
    else
        return InputResult::GenericError;

    return InputResult::Ok;
}

InputResult InputCommon::get_sleep_status(const InputContext& inputContext, IUnknown* pDevice,
                                          SleepParams* sleep_params)
{
    if (!update_low_latency_tech(pDevice))
        return InputResult::LowLatencyUpdateFail;

    // Get functions don't really need to worry about this check
    // if (inputContext.caller != activeInput)
    //     return InputResult::UsingDifferentInput;

    if (auto current_tech = currently_active_tech.load())
        current_tech->get_sleep_status(sleep_params);
    else
        return InputResult::GenericError;

    return InputResult::Ok;
}

InputResult InputCommon::get_latency(const InputContext& inputContext, IUnknown* pDev, void* latency_params)
{
    // if (inputContext.caller != activeInput)
    //     return InputResult::UsingDifferentInput;

    if (!update_low_latency_tech(pDev))
        return InputResult::LowLatencyUpdateFail;

    if (!latency_params)
        return InputResult::InvalidParameter;

    if (inputContext.caller == LowLatencyInput::Reflex)
    {
        if (activeOutput == LowLatencyMode::Reflex)
        {
            // TODO: passthrough call, if success return InputResult::Ok;
            // return InputResult::Ok;
        }

        NV_LATENCY_RESULT_PARAMS* reports = (NV_LATENCY_RESULT_PARAMS*) latency_params;

        if (reports->version != NV_LATENCY_RESULT_PARAMS_VER1)
        {
            LOG_ERROR("Unsupported version {}", reports->version);
            return InputResult::InvalidParameter;
        }

        // Assume no frame reports collected yet, report all zeros
        if (frame_reports[FRAME_REPORTS_BUFFER_SIZE - 1].frameID == 0)
        {
            std::memset(reports->frameReport, 0, sizeof(reports->frameReport));
            // spdlog::warn("GetLatency: Not enough data to report");
            return InputResult::GenericError;
        }

        // Sort frame reports, find the oldest
        size_t minIdx = 0;
        uint64_t minID = frame_reports[0].frameID;
        for (size_t i = 1; i < FRAME_REPORTS_BUFFER_SIZE; i++)
        {
            if (frame_reports[i].frameID < minID)
            {
                minID = frame_reports[i].frameID;
                minIdx = i;
            }
        }

        // Copy starting from older before wrapping around
        size_t firstChunk = std::min<uint64_t>(NVAPI_BUFFER_SIZE, FRAME_REPORTS_BUFFER_SIZE - minIdx);
        std::memcpy(reports->frameReport, frame_reports + minIdx, firstChunk * sizeof(FrameReport));

        // Copy the rest after wrapping around
        if (firstChunk < NVAPI_BUFFER_SIZE)
        {
            std::memcpy(reports->frameReport + firstChunk, frame_reports,
                        (NVAPI_BUFFER_SIZE - firstChunk) * sizeof(FrameReport));
        }

        return InputResult::Ok;
    }
    else if (inputContext.caller == LowLatencyInput::XeLL)
    {
        xell_frame_report_t* reports = (xell_frame_report_t*) latency_params;
        constexpr size_t reportCount = 64; // 64 reports, if the app allocated less then it's on them

        if (activeOutput == LowLatencyMode::XeLL)
        {
            if (auto current_tech = currently_active_tech.load())
            {
                auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
                auto result = xell_tech->xellGetFramesReports(reports);

                if (result == XELL_RESULT_SUCCESS)
                    return InputResult::Ok;
            }
        }

        // Hopefully not too slow
        // XeLL doesn't seem to make any guarantees about ordering so no sort needed
        for (auto i = 0; i < reportCount; i++)
        {
            auto& reportOut = reports[i];
            auto& reportIn = frame_reports[i];

            reportOut.m_frame_id = reportIn.frameID & 0xFFFFFFFF;
            reportOut.m_sim_start_ts = reportIn.simStartTime;
            reportOut.m_sim_end_ts = reportIn.simEndTime;
            reportOut.m_render_submit_start_ts = reportIn.renderSubmitStartTime;
            reportOut.m_render_submit_end_ts = reportIn.renderSubmitEndTime;
            reportOut.m_present_start_ts = reportIn.presentStartTime;
            reportOut.m_present_end_ts = reportIn.presentEndTime;
        }

        return InputResult::Ok;
    }
    else
    {
        return InputResult::InputNotSupported;
    }
}

#define UPDATE_TIMING_ENTRY(name, type)                                                                                \
    if (frameReport.name##EndTime >= frameReport.name##StartTime)                                                      \
    {                                                                                                                  \
        double name##Pos = (double) (frameReport.name##StartTime - start) / rangeNs;                                   \
        double name##Length = (double) (frameReport.name##EndTime - frameReport.name##StartTime) / rangeNs;            \
        timingDataOut.type = TimingEntry { .position = name##Pos, .length = name##Length };                            \
    }                                                                                                                  \
    else                                                                                                               \
    {                                                                                                                  \
        timingDataOut.type.reset();                                                                                    \
    }

bool InputCommon::get_timing_data(TimingData& timingDataOut)
{
    InputContext tempContext {};
    if (activeOutput == LowLatencyMode::Reflex)
    {
        // TODO: allocate struct and get everything from reflex
        return true;
    }

    // auto processFrameReport = [&](const auto& frameReport) -> bool
    //{
    //     uint64_t start = UINT64_MAX;
    //     uint64_t end = 0;

    //    // Please don't look, just thought it would be least work
    //    auto pTimes = (const uint64_t*) &frameReport.simStartTime;
    //    for (auto i = 0; i < 11; i++)
    //    {
    //        auto& time = pTimes[i];
    //        if (time == 0)
    //            continue;

    //        if (time < start)
    //            start = time;

    //        if (time > end)
    //            end = time;
    //    }

    //    if (end < start)
    //        return false;

    //    double rangeNs = static_cast<double>(end - start);

    //    timingData[TimingType::TimeRange] = TimingEntry { .position = 0, .length = rangeNs };
    //    UPDATE_TIMING_ENTRY(sim, Simulation)
    //    UPDATE_TIMING_ENTRY(renderSubmit, RenderSubmit)
    //    UPDATE_TIMING_ENTRY(present, Present)
    //    UPDATE_TIMING_ENTRY(driver, Driver)
    //    UPDATE_TIMING_ENTRY(osRenderQueue, OsRenderQueue)
    //    UPDATE_TIMING_ENTRY(gpuRender, GpuRender)

    //    if (frameReport.frameID != 0)
    //        State::Instance().reflexFrameId = frameReport.frameID;

    //    return true;
    //};

    // if (_lastSleepDev && o_NvAPI_D3D_GetLatency)
    //{
    //     // Not calling free on this but it's static so hopefully fine
    //     static NV_LATENCY_RESULT_PARAMS* results = new NV_LATENCY_RESULT_PARAMS();
    //     results->version = NV_LATENCY_RESULT_PARAMS_VER;

    //    if (auto result = hkNvAPI_D3D_GetLatency(_lastSleepDev, results); result != NVAPI_OK)
    //    {
    //        LOG_WARN("NvAPI_D3D_GetLatency failed: {}", magic_enum::enum_name(result));
    //        return false;
    //    }

    //    // 64th element has the latest data
    //    return processFrameReport(results->frameReport[63]);
    //}

    // if (_lastVkSleepDev && o_NvAPI_Vulkan_GetLatency)
    //{
    //     // Not calling free on this but it's static so hopefully fine
    //     static NV_VULKAN_LATENCY_RESULT_PARAMS* results = new NV_VULKAN_LATENCY_RESULT_PARAMS();
    //     results->version = NV_VULKAN_LATENCY_RESULT_PARAMS_VER;

    //    if (auto result = hkNvAPI_Vulkan_GetLatency(_lastVkSleepDev, results); result != NVAPI_OK)
    //    {
    //        LOG_WARN("NvAPI_Vulkan_GetLatency failed: {}", magic_enum::enum_name(result));
    //        return false;
    //    }

    //    // 64th element has the latest data
    //    return processFrameReport(results->frameReport[63]);
    //}

    {
        size_t maxIdx = 0;
        uint64_t maxID = frame_reports[0].frameID;
        for (size_t i = 1; i < FRAME_REPORTS_BUFFER_SIZE; i++)
        {
            if (frame_reports[i].frameID > maxID)
            {
                maxID = frame_reports[i].frameID;
                maxIdx = i;
            }
        }

        auto highestValidId = (NVAPI_BUFFER_SIZE + maxIdx) % FRAME_REPORTS_BUFFER_SIZE;
        auto& frameReport = frame_reports[highestValidId]; // grab last one

        uint64_t start = UINT64_MAX;
        uint64_t end = 0;

        // Please don't look, just thought it would be least work
        auto pTimes = (const uint64_t*) &frameReport.simStartTime;
        for (auto i = 0; i < 11; i++)
        {
            auto& time = pTimes[i];
            if (time == 0)
                continue;

            if (time < start)
                start = time;

            if (time > end)
                end = time;
        }

        if (end < start)
            return false;

        double rangeNs = static_cast<double>(end - start);

        timingDataOut.timeRange = TimingEntry { .position = 0, .length = rangeNs };
        UPDATE_TIMING_ENTRY(sim, simulation)
        UPDATE_TIMING_ENTRY(renderSubmit, renderSubmit)
        UPDATE_TIMING_ENTRY(present, present)
        UPDATE_TIMING_ENTRY(driver, driver)
        UPDATE_TIMING_ENTRY(osRenderQueue, osRenderQueue)
        UPDATE_TIMING_ENTRY(gpuRender, gpuRender)

        if (frameReport.frameID != 0)
            State::Instance().reflexFrameId = frameReport.frameID;

        return true;
    };

    return false;
}

xell_result_t InputCommon::pass_xellD3D12SetAppQueue(const InputContext& inputContext, ID3D12CommandQueue* appQueue)
{
    if (inputContext.caller == LowLatencyInput::XeLL && activeOutput == LowLatencyMode::XeLL)
    {
        if (auto current_tech = currently_active_tech.load())
        {
            auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
            return xell_tech->xellD3D12SetAppQueue(appQueue);
        }
    }

    return XELL_RESULT_ERROR_UNKNOWN;
}

xell_result_t InputCommon::pass_xellSetDisplayInfo(const InputContext& inputContext, void* displayInfo)
{
    if (inputContext.caller == LowLatencyInput::XeLL && activeOutput == LowLatencyMode::XeLL)
    {
        if (auto current_tech = currently_active_tech.load())
        {
            auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
            return xell_tech->xellSetDisplayInfo(displayInfo);
        }
    }

    return XELL_RESULT_ERROR_UNKNOWN;
}

xell_result_t InputCommon::pass_xellSetFgEnabled(const InputContext& inputContext, uint32_t param1, uint32_t param2)
{
    if (inputContext.caller == LowLatencyInput::XeLL && activeOutput == LowLatencyMode::XeLL)
    {
        if (auto current_tech = currently_active_tech.load())
        {
            auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
            return xell_tech->xellSetFgEnabled(param1, param2);
        }
    }

    return XELL_RESULT_ERROR_UNKNOWN;
}

xell_result_t InputCommon::pass_xellSetGeneratedFramesCount(const InputContext& inputContext, uint32_t param1,
                                                            uint32_t framesCount)
{
    if (inputContext.caller == LowLatencyInput::XeLL && activeOutput == LowLatencyMode::XeLL)
    {
        if (auto current_tech = currently_active_tech.load())
        {
            auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
            return xell_tech->xellSetGeneratedFramesCount(param1, framesCount);
        }
    }

    return XELL_RESULT_ERROR_UNKNOWN;
}

xell_result_t InputCommon::pass_xellGetLastPresentStartFrameId(const InputContext& inputContext, uint32_t* p_frame_id)
{
    if (inputContext.caller == LowLatencyInput::XeLL && activeOutput == LowLatencyMode::XeLL)
    {
        if (auto current_tech = currently_active_tech.load())
        {
            auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
            return xell_tech->xellGetLastPresentStartFrameId(p_frame_id);
        }
    }

    return XELL_RESULT_ERROR_UNKNOWN;
}
