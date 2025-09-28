#pragma once

#include <pch.h>

namespace D3D1XHooks
{
void UnHookDx();
void HookDx11(HMODULE dx11Module);
void HookDx12();
void ReleaseDx12SwapChain(HWND hwnd);
} // namespace D3D1XHooks
