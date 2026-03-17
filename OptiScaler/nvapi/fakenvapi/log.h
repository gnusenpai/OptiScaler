#pragma once
#include <iostream>
#include <fstream>
#include <format>
#include <windows.h>
#include <dxgi.h>
#include <nvapi.h>
#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "fn_util.h"
#include "config.h"

#define OK() Ok(__func__)
#undef ERROR
#define ERROR() Error(__func__)
#define ERROR_VALUE(status) Error(__func__, status)

// #define FAKENVAPI_TRACE_LOGGING

#ifdef FAKENVAPI_TRACE_LOGGING
#define LOG_TRACE_FAKENVAPI(msg, ...) LOG_TRACE(msg, ##__VA_ARGS__)
#else
#define LOG_TRACE_FAKENVAPI(msg, ...)
#endif

NvAPI_Status Ok(const char* function_name);
NvAPI_Status Error(const char* function_name, NvAPI_Status status = NVAPI_ERROR);
template <typename... _Args>
void log_event(const char* event_name, std::format_string<_Args...> __fmt, _Args&&... __args)
{
    LOG_TRACE_FAKENVAPI("EVENT,{},{},{}", event_name, get_timestamp(),
                        std::vformat(__fmt.get(), std::make_format_args(__args...)));
}
