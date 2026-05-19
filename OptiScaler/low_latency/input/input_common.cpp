#include "pch.h"
#include "input_common.h"
#include <low_latency/low_latency_tech/ll_xell.h>

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

    // TODO: init correct currently_active_tech, desiredMode == None -> Auto

    if (auto current_tech = currently_active_tech.load())
    {
        activeOutput = current_tech->get_mode();
        return true;
    }

    LOG_ERROR("No Low Latency Tech selected");
    return false;
}

void InputCommon::add_marker_to_report(MarkerParams* marker_params)
{
    auto current_timestamp = get_timestamp() / 1000;
    static auto last_sim_start = current_timestamp;
    static auto _2nd_last_sim_start = current_timestamp;
    auto current_report = &frame_reports[marker_params->frame_id % FRAME_REPORTS_BUFFER_SIZE];

    if (current_report->frameID != marker_params->frame_id)
    {
        *current_report = FrameReport {};
    }

    current_report->frameID = marker_params->frame_id;
    current_report->gpuFrameTimeUs = (uint32_t) (last_sim_start - _2nd_last_sim_start);
    current_report->gpuActiveRenderTimeUs = 100;
    current_report->driverStartTime = current_timestamp;
    current_report->driverEndTime = current_timestamp + 100;
    current_report->gpuRenderStartTime = current_timestamp;
    current_report->gpuRenderEndTime = current_timestamp + 100;
    current_report->osRenderQueueStartTime = current_timestamp;
    current_report->osRenderQueueEndTime = current_timestamp + 100;
    switch (marker_params->marker_type)
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

InputResult InputCommon::set_low_latency_tech(IUnknown* pDevice, LowLatencyMode mode)
{
    if (!update_low_latency_tech(pDevice, mode))
        return InputResult::LowLatencyUpdateFail;

    return InputResult::Ok;
}

InputResult InputCommon::sleep(IUnknown* pDevice, const InputContext& inputContext, std::optional<uint32_t> frame_id)
{
    if (!update_low_latency_tech(pDevice))
        return InputResult::LowLatencyUpdateFail;

    if (auto current_tech = currently_active_tech.load())
        current_tech->sleep(frame_id);
    else
        return InputResult::GenericError;

    return InputResult::Ok;
}

InputResult InputCommon::set_marker(const InputContext& inputContext, IUnknown* pDevice, MarkerParams* marker_params)
{
    if (!update_low_latency_tech(pDevice))
        return InputResult::LowLatencyUpdateFail;

    if (!marker_params)
        return InputResult::InvalidParameter;

    if (inputContext.markerMode == InputMarkerMode::NoMarkers)
    {
        return InputResult::InputNotSupported;
    }
    else if (inputContext.markerMode == InputMarkerMode::PresentStartOnly &&
             marker_params->marker_type != MarkerType::PRESENT_START)
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

    LOG_TRACE_LOWLATENCY("{}: {}", magic_enum::enum_name(marker_params->marker_type), marker_params->frame_id);

    return InputResult::Ok;
}

InputResult InputCommon::set_async_marker(const InputContext& inputContext, ID3D12CommandQueue* pCommandQueue,
                                          MarkerParams* marker_params)
{
    if (!currently_active_tech.load()) // can't init using ID3D12CommandQueue, can only check if available
        return InputResult::LowLatencyUpdateFail;

    if (!marker_params)
        return InputResult::InvalidParameter;

    if (inputContext.markerMode == InputMarkerMode::NoMarkers)
    {
        return InputResult::InputNotSupported;
    }
    else if (inputContext.markerMode == InputMarkerMode::PresentStartOnly &&
             marker_params->marker_type != MarkerType::OUT_OF_BAND_PRESENT_START)
    {
        return InputResult::InputNotSupported;
    }

    // TODO: could consider adding async markers to the report but would require some rewriting
    // add_marker_to_report(marker_params);

    if (auto current_tech = currently_active_tech.load())
        current_tech->set_async_marker(pCommandQueue, marker_params);
    else
        return InputResult::GenericError;

    LOG_TRACE_LOWLATENCY("{}: {}", magic_enum::enum_name(marker_params->marker_type), marker_params->frame_id);

    return InputResult::Ok;
}

// TODO: impl
InputResult InputCommon::set_sleep_mode(const InputContext& inputContext, IUnknown* pDevice, SleepMode* sleep_mode)
{
    return InputResult::Ok;
}

// TODO: impl
InputResult InputCommon::get_sleep_status(const InputContext& inputContext, IUnknown* pDevice,
                                          SleepParams* sleep_params)
{
    return InputResult::Ok;
}

InputResult InputCommon::get_latency(const InputContext& inputContext, IUnknown* pDev, void* latency_params)
{
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
        if (activeOutput == LowLatencyMode::XeLL)
        {
            // TODO: passthrough call, if success return InputResult::Ok;
            // return InputResult::Ok;
        }

        xell_frame_report_t* reports = (xell_frame_report_t*) latency_params;
        constexpr size_t reportCount = 64; // 64 reports, if the app allocated less then it's on them

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

xell_result_t InputCommon::pass_xellD3D12SetAppQueue(const InputContext& inputContext, void* appQueue)
{
    if (inputContext.caller == LowLatencyInput::XeLL && activeInput == LowLatencyInput::XeLL &&
        activeOutput == LowLatencyMode::XeLL)
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
    if (inputContext.caller == LowLatencyInput::XeLL && activeInput == LowLatencyInput::XeLL &&
        activeOutput == LowLatencyMode::XeLL)
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
    if (inputContext.caller == LowLatencyInput::XeLL && activeInput == LowLatencyInput::XeLL &&
        activeOutput == LowLatencyMode::XeLL)
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
    if (inputContext.caller == LowLatencyInput::XeLL && activeInput == LowLatencyInput::XeLL &&
        activeOutput == LowLatencyMode::XeLL)
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
    if (inputContext.caller == LowLatencyInput::XeLL && activeInput == LowLatencyInput::XeLL &&
        activeOutput == LowLatencyMode::XeLL)
    {
        if (auto current_tech = currently_active_tech.load())
        {
            auto xell_tech = std::static_pointer_cast<XeLL>(current_tech);
            return xell_tech->xellGetLastPresentStartFrameId(p_frame_id);
        }
    }

    return XELL_RESULT_ERROR_UNKNOWN;
}
