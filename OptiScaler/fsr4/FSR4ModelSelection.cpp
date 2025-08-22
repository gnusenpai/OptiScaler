#include "FSR4ModelSelection.h"
#include <scanner/scanner.h>
#include <detours/detours.h>

PFN_getModelBlob FSR4ModelSelection::o_getModelBlob = nullptr;

uint64_t FSR4ModelSelection::hkgetModelBlob(uint32_t preset, uint64_t unknown, uint64_t* source, uint64_t* size)
{
    LOG_FUNC();

    // Fixup for Quality preset sometimes using model 0, sometimes using model 1
    if (State::Instance().currentFeature)
    {
        auto target = State::Instance().currentFeature->TargetWidth();
        auto render = State::Instance().currentFeature->RenderWidth();

        auto ratio = (float) target / (float) render;

        // Include Ultra Quality in the fix as well
        if (preset == 0 && ratio >= 1.29f)
            preset = 1;
    }

    if (Config::Instance()->Fsr4Model.has_value())
    {
        preset = Config::Instance()->Fsr4Model.value();
    }

    State::Instance().currentFsr4Model = preset;

    auto result = o_getModelBlob(preset, unknown, source, size);

    return result;
}

void FSR4ModelSelection::Hook(HMODULE module, bool unhookOld)
{
    if (module == nullptr)
        return;

    if (o_getModelBlob != nullptr && unhookOld)
    {
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());

        DetourDetach(&(PVOID&) o_getModelBlob, hkgetModelBlob);
        o_getModelBlob = nullptr;

        DetourTransactionCommit();
    }

    if (o_getModelBlob == nullptr)
    {
        const char* pattern = "83 F9 05 0F 87";
        o_getModelBlob = (PFN_getModelBlob) scanner::GetAddress(module, pattern);

        if (o_getModelBlob)
        {
            LOG_DEBUG("Hooking model selection");

            DetourTransactionBegin();
            DetourUpdateThread(GetCurrentThread());

            DetourAttach(&(PVOID&) o_getModelBlob, hkgetModelBlob);

            DetourTransactionCommit();
        }
        else
        {
            LOG_ERROR("Couldn't hook model selection");
        }
    }
    else
    {
        LOG_DEBUG("Didn't rehook");
    }
}