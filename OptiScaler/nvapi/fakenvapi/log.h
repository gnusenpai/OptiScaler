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

NvAPI_Status Ok(const char* function_name);
NvAPI_Status Error(const char* function_name, NvAPI_Status status = NVAPI_ERROR);
template <typename... _Args>
void log_event(const char* event_name, std::format_string<_Args...> __fmt, _Args&&... __args) {
    spdlog::trace("EVENT,{},{},{}", event_name, get_timestamp(), std::vformat(__fmt.get(), std::make_format_args(__args...)));
}
