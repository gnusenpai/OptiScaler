#include "pch.h"
#include "input_system_internal.h"

#include <detours/detours.h>

namespace OptiInput
{
SHORT RealGetAsyncKeyStateSafe(int vk)
{
    ScopedHookBypass bypass;

    if (o_GetAsyncKeyState != nullptr)
        return o_GetAsyncKeyState(vk);

    return ::GetAsyncKeyState(vk);
}

SHORT RealGetKeyStateSafe(int vk)
{
    ScopedHookBypass bypass;

    if (o_GetKeyState != nullptr)
        return o_GetKeyState(vk);

    return ::GetKeyState(vk);
}

BOOL RealGetCursorPosSafe(LPPOINT point)
{
    ScopedHookBypass bypass;

    if (o_GetCursorPos != nullptr)
        return o_GetCursorPos(point);

    return ::GetCursorPos(point);
}

namespace
{
void ResolveOptionalUser32Exports()
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");

    if (user32 == nullptr)
        user32 = LoadLibraryW(L"user32.dll");

    if (user32 == nullptr)
        return;

    if (o_GetPhysicalCursorPos == nullptr)
    {
        o_GetPhysicalCursorPos =
            reinterpret_cast<GetPhysicalCursorPos_t>(GetProcAddress(user32, "GetPhysicalCursorPos"));
    }

    if (o_SetPhysicalCursorPos == nullptr)
    {
        o_SetPhysicalCursorPos =
            reinterpret_cast<SetPhysicalCursorPos_t>(GetProcAddress(user32, "SetPhysicalCursorPos"));
    }
}
} // namespace

bool InstallHooks()
{
    if (_state.HooksInstalled)
        return true;

    LOG_INFO("installing Win32 input hooks currentPid:{} targetPid:{} inputPid:{}", _state.CurrentProcessId,
             _state.TargetProcessId, _state.InputProcessId);

    ResolveOptionalUser32Exports();

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourAttach(reinterpret_cast<PVOID*>(&o_PeekMessageA), hkPeekMessageA);
    DetourAttach(reinterpret_cast<PVOID*>(&o_PeekMessageW), hkPeekMessageW);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetMessageA), hkGetMessageA);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetMessageW), hkGetMessageW);

    DetourAttach(reinterpret_cast<PVOID*>(&o_GetAsyncKeyState), hkGetAsyncKeyState);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetKeyState), hkGetKeyState);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetKeyboardState), hkGetKeyboardState);

    DetourAttach(reinterpret_cast<PVOID*>(&o_GetCursorPos), hkGetCursorPos);
    DetourAttach(reinterpret_cast<PVOID*>(&o_SetCursorPos), hkSetCursorPos);

    if (o_GetPhysicalCursorPos != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&o_GetPhysicalCursorPos), hkGetPhysicalCursorPos);

    if (o_SetPhysicalCursorPos != nullptr)
        DetourAttach(reinterpret_cast<PVOID*>(&o_SetPhysicalCursorPos), hkSetPhysicalCursorPos);

    DetourAttach(reinterpret_cast<PVOID*>(&o_GetMessagePos), hkGetMessagePos);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetMouseMovePointsEx), hkGetMouseMovePointsEx);
    DetourAttach(reinterpret_cast<PVOID*>(&o_ClipCursor), hkClipCursor);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetClipCursor), hkGetClipCursor);

    DetourAttach(reinterpret_cast<PVOID*>(&o_SendInput), hkSendInput);
    DetourAttach(reinterpret_cast<PVOID*>(&o_mouse_event), hkmouse_event);
    DetourAttach(reinterpret_cast<PVOID*>(&o_PostMessageA), hkPostMessageA);
    DetourAttach(reinterpret_cast<PVOID*>(&o_PostMessageW), hkPostMessageW);
    DetourAttach(reinterpret_cast<PVOID*>(&o_SendMessageA), hkSendMessageA);
    DetourAttach(reinterpret_cast<PVOID*>(&o_SendMessageW), hkSendMessageW);
    DetourAttach(reinterpret_cast<PVOID*>(&o_CreateFileA), hkCreateFileA);
    DetourAttach(reinterpret_cast<PVOID*>(&o_CreateFileW), hkCreateFileW);
    DetourAttach(reinterpret_cast<PVOID*>(&o_ReadFile), hkReadFile);
    DetourAttach(reinterpret_cast<PVOID*>(&o_DeviceIoControl), hkDeviceIoControl);
    DetourAttach(reinterpret_cast<PVOID*>(&o_CloseHandle), hkCloseHandle);

    DetourAttach(reinterpret_cast<PVOID*>(&o_GetRawInputData), hkGetRawInputData);
    DetourAttach(reinterpret_cast<PVOID*>(&o_GetRawInputBuffer), hkGetRawInputBuffer);
    DetourAttach(reinterpret_cast<PVOID*>(&o_RegisterRawInputDevices), hkRegisterRawInputDevices);

    DetourAttach(reinterpret_cast<PVOID*>(&o_SetWindowsHookExA), hkSetWindowsHookExA);
    DetourAttach(reinterpret_cast<PVOID*>(&o_SetWindowsHookExW), hkSetWindowsHookExW);
    DetourAttach(reinterpret_cast<PVOID*>(&o_UnhookWindowsHookEx), hkUnhookWindowsHookEx);

    const LONG result = DetourTransactionCommit();

    _state.HooksInstalled = result == NO_ERROR;
    if (_state.HooksInstalled)
        LOG_INFO("Win32 input hooks installed");
    else
        LOG_ERROR("Win32 input hook installation failed result:{}", result);

    return _state.HooksInstalled;
}

void RemoveHooks()
{
    if (!_state.HooksInstalled)
        return;

    LOG_INFO("removing Win32 input hooks");

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

    DetourDetach(reinterpret_cast<PVOID*>(&o_PeekMessageA), hkPeekMessageA);
    DetourDetach(reinterpret_cast<PVOID*>(&o_PeekMessageW), hkPeekMessageW);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetMessageA), hkGetMessageA);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetMessageW), hkGetMessageW);

    DetourDetach(reinterpret_cast<PVOID*>(&o_GetAsyncKeyState), hkGetAsyncKeyState);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetKeyState), hkGetKeyState);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetKeyboardState), hkGetKeyboardState);

    DetourDetach(reinterpret_cast<PVOID*>(&o_GetCursorPos), hkGetCursorPos);
    DetourDetach(reinterpret_cast<PVOID*>(&o_SetCursorPos), hkSetCursorPos);

    if (o_GetPhysicalCursorPos != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_GetPhysicalCursorPos), hkGetPhysicalCursorPos);

    if (o_SetPhysicalCursorPos != nullptr)
        DetourDetach(reinterpret_cast<PVOID*>(&o_SetPhysicalCursorPos), hkSetPhysicalCursorPos);

    DetourDetach(reinterpret_cast<PVOID*>(&o_GetMessagePos), hkGetMessagePos);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetMouseMovePointsEx), hkGetMouseMovePointsEx);
    DetourDetach(reinterpret_cast<PVOID*>(&o_ClipCursor), hkClipCursor);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetClipCursor), hkGetClipCursor);

    DetourDetach(reinterpret_cast<PVOID*>(&o_SendInput), hkSendInput);
    DetourDetach(reinterpret_cast<PVOID*>(&o_mouse_event), hkmouse_event);
    DetourDetach(reinterpret_cast<PVOID*>(&o_PostMessageA), hkPostMessageA);
    DetourDetach(reinterpret_cast<PVOID*>(&o_PostMessageW), hkPostMessageW);
    DetourDetach(reinterpret_cast<PVOID*>(&o_SendMessageA), hkSendMessageA);
    DetourDetach(reinterpret_cast<PVOID*>(&o_SendMessageW), hkSendMessageW);
    DetourDetach(reinterpret_cast<PVOID*>(&o_CreateFileA), hkCreateFileA);
    DetourDetach(reinterpret_cast<PVOID*>(&o_CreateFileW), hkCreateFileW);
    DetourDetach(reinterpret_cast<PVOID*>(&o_ReadFile), hkReadFile);
    DetourDetach(reinterpret_cast<PVOID*>(&o_DeviceIoControl), hkDeviceIoControl);
    DetourDetach(reinterpret_cast<PVOID*>(&o_CloseHandle), hkCloseHandle);

    DetourDetach(reinterpret_cast<PVOID*>(&o_GetRawInputData), hkGetRawInputData);
    DetourDetach(reinterpret_cast<PVOID*>(&o_GetRawInputBuffer), hkGetRawInputBuffer);
    DetourDetach(reinterpret_cast<PVOID*>(&o_RegisterRawInputDevices), hkRegisterRawInputDevices);

    DetourDetach(reinterpret_cast<PVOID*>(&o_SetWindowsHookExA), hkSetWindowsHookExA);
    DetourDetach(reinterpret_cast<PVOID*>(&o_SetWindowsHookExW), hkSetWindowsHookExW);
    DetourDetach(reinterpret_cast<PVOID*>(&o_UnhookWindowsHookEx), hkUnhookWindowsHookEx);

    const LONG result = DetourTransactionCommit();
    if (result != NO_ERROR)
        LOG_WARN("Win32 input hook removal completed with result:{}", result);

    _state.HooksInstalled = false;
}

} // namespace OptiInput
