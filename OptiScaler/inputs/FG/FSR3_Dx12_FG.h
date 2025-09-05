#pragma once

#include "pch.h"

const UINT fgContext = 0x1337;

void HookFSR3FGExeInputs();
void HookFSR3FGInputs(HMODULE module);
