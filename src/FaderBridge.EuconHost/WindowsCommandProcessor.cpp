#include "WindowsCommandProcessor.h"

#include "DiagnosticLog.h"
#include "EuPrimitiveControl.h"
#include "EuPrimitiveSwitch.h"

#include <utility>

WindowsCommandProcessor::WindowsCommandProcessor(CommandHandler monoToggleHandler,
    CommandHandler clearSoloHandler)
    : monoToggleHandler_(std::move(monoToggleHandler)),
      clearSoloHandler_(std::move(clearSoloHandler)),
      windowsAudioCommands_(this), monoAudio_(this), clearSolo_(this)
{
    SetAttribute(kATRIBID_ProcessorType, kProcType_Command);
    SetAttribute(kATRIBID_ContainsSoftKeys, 1);
    SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Key Commands"));
    SetAttribute(kATRIBID_DoNotSort, 1);
    SetPersistenceID(tEuString(L"FaderBridge.WindowsAudio.Commands.v1"));

    windowsAudioCommands_.SetId(WindowsAudioContainerId);
    windowsAudioCommands_.SetAttribute(
        kATRIBID_SimpleUserVisibleName, tEuString(L"Windows Audio"));
    windowsAudioCommands_.SetAttribute(kATRIBID_DoNotSort, 1);
    windowsAudioCommands_.SetPersistenceID(
        tEuString(L"FaderBridge.WindowsAudio.Commands.Container.v1"));
    AddControl(windowsAudioCommands_);

    monoAudio_.SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Mono Audio"));
    monoAudio_.SetPersistenceID(
        tEuString(L"FaderBridge.WindowsAudio.Commands.MonoAudio.v1"));
    EuPrimitiveControl* primitive = nullptr;
    if (monoAudio_.GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_Int, 1U);
        primitive->LoadValueTableInterpolated(0, 0);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_OneShot);
        }
    }
    // Windows is authoritative. The command is one-shot and its LED reflects
    // the current Windows accessibility setting, including external changes.
    monoAudio_.SetLedOverride(true);
    windowsAudioCommands_.PushBack(&monoAudio_, monoAudioMemberId_);

    clearSolo_.SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"Clear Solo"));
    clearSolo_.SetPersistenceID(
        tEuString(L"FaderBridge.WindowsAudio.Commands.ClearSolo.v1"));
    primitive = nullptr;
    if (clearSolo_.GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_Int, 1U);
        primitive->LoadValueTableInterpolated(0, 0);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_OneShot);
        }
    }
    // Section 12.5 requires Clear Solo to remain lit while any channel is
    // soloed. This assignable command mirrors the standard System control.
    clearSolo_.SetLedOverride(true);
    windowsAudioCommands_.PushBack(&clearSolo_, clearSoloMemberId_);
}

WindowsCommandProcessor::~WindowsCommandProcessor()
{
    windowsAudioCommands_.Remove(clearSoloMemberId_);
    windowsAudioCommands_.Remove(monoAudioMemberId_);
    RemoveControl(windowsAudioCommands_);
}

void WindowsCommandProcessor::SetSoloActive(const bool active)
{
    if (soloActive_ == active)
    {
        return;
    }
    soloActive_ = active;
    EuPrimitiveControl* primitive = nullptr;
    if (clearSolo_.GetPrimitive(EuControlSwitch::kID_Led, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            active ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
}

void WindowsCommandProcessor::SetMonoAudioEnabled(const bool enabled)
{
    if (monoAudioEnabled_ == enabled)
    {
        return;
    }
    monoAudioEnabled_ = enabled;
    EuPrimitiveControl* primitive = nullptr;
    if (monoAudio_.GetPrimitive(EuControlSwitch::kID_Led, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            enabled ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
    FB_TRACE("MONO_AUDIO_LED enabled=%d", enabled ? 1 : 0);
}

void WindowsCommandProcessor::OnPrimitiveCallback(const tEVT eventType,
    const NEuCon::uint32,
    const NEuCon::uint32 controlId,
    const NEuCon::uint32 arrayMemberControlId,
    const NEuCon::uint32,
    EuPrimitiveControl*,
    const NEuCon::uint16,
    void*)
{
    // EUCON owns this callback thread. Queue only; all Windows work and all
    // EUCON feedback writes happen on their existing owning threads.
    if (eventType != kEVT_PRIM_StateChange || controlId != WindowsAudioContainerId)
    {
        return;
    }
    if (arrayMemberControlId == monoAudioMemberId_ && monoToggleHandler_)
    {
        FB_TRACE("MONO_AUDIO_SURFACE_TOGGLE");
        monoToggleHandler_();
    }
    else if (arrayMemberControlId == clearSoloMemberId_ && clearSoloHandler_)
    {
        FB_TRACE("CLEAR_SOLO_COMMAND");
        clearSoloHandler_();
    }
}
