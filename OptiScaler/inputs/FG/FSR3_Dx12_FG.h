#pragma once

#include "pch.h"

const size_t scContext = 0x13375CC;
const size_t fgContext = 0x133757C;

void HookFSR3FGExeInputs();
void HookFSR3FGInputs(HMODULE module);
