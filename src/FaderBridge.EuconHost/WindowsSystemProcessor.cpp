#include "WindowsSystemProcessor.h"

#include "DiagnosticLog.h"
#include "EuDefinitions.h"
#include "EuLayoutSystem.h"
#include "EuPrimitiveControl.h"
#include "EuPrimitiveSwitch.h"

#include <utility>

WindowsSystemProcessor::WindowsSystemProcessor(ClearSoloHandler clearSoloHandler)
    : clearSoloHandler_(std::move(clearSoloHandler)), clearSolo_(this)
{
    // Getting Started with EuCon 3.5.9 and 12.5: one System Processor owns
    // global Clear Solo. It is a one-shot control with an app-owned LED.
    SetAttribute(kATRIBID_ProcessorType, kProcType_System);
    SetAttribute(kATRIBID_SimpleUserVisibleName, tEuString(L"System"));
    SetPersistenceID(tEuString(L"FaderBridge.WindowsAudio.System.v1"));

    clearSolo_.SetId(ClearSoloId);
    clearSolo_.SetAttribute(kATRIBID_LayoutName0, EuLayoutSystem::kNAM_ClearSolo);
    clearSolo_.SetPersistenceID(tEuString(L"FaderBridge.WindowsAudio.System.ClearSolo.v1"));
    EuPrimitiveControl* primitive = nullptr;
    if (clearSolo_.GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Int, 1U);
        primitive->LoadValueTableInterpolated(0, 0);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_OneShot);
        }
    }
    clearSolo_.SetLedOverride(true);
    AddControl(clearSolo_);
}

WindowsSystemProcessor::~WindowsSystemProcessor()
{
    RemoveControl(clearSolo_);
}

void WindowsSystemProcessor::SetSoloActive(const bool active)
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

void WindowsSystemProcessor::OnPrimitiveCallback(const tEVT eventType,
    const NEuCon::uint32, const NEuCon::uint32 controlId, const NEuCon::uint32,
    const NEuCon::uint32, EuPrimitiveControl*, const NEuCon::uint16, void*)
{
    // EUCON owns this callback thread. Queue only; Core Audio work is done by
    // NativeAudioController's owning MTA/MMCSS worker.
    if (eventType == kEVT_PRIM_StateChange && controlId == ClearSoloId &&
        clearSoloHandler_)
    {
        FB_TRACE("CLEAR_SOLO_SYSTEM");
        clearSoloHandler_();
    }
}
