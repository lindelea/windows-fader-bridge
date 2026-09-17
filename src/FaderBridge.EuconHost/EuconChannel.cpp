#include "EuconChannel.h"

#include "DiagnosticLog.h"
#include "EuBatchedMeters.h"
#include "EuBatchedMeterWriter.h"
#include "EuDefinitions.h"
#include "EuLayoutChannel.h"
#include "EuPrimitiveControl.h"
#include "EuPrimitiveKnob.h"
#include "EuPrimitiveMeter.h"
#include "EuPrimitiveSwitch.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace
{
constexpr float kMinDb = -96.0F;
constexpr float kMaxDb = 0.0F;
constexpr float kOvertravelMaxDb = 12.0F;
constexpr NEuCon::uint16 kFaderStepCount = 1024U;
constexpr NEuCon::uint16 kUnityIndex = 728U;

unsigned long long MonotonicMilliseconds()
{
    return static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

float CoordinateToNormalized(const float value)
{
    return std::clamp((value - kMinDb) / (kMaxDb - kMinDb), 0.0F, 1.0F);
}

float NormalizedToCoordinate(const float value)
{
    return kMinDb + (std::clamp(value, 0.0F, 1.0F) * (kMaxDb - kMinDb));
}

tFORMAT TrackFormatForMeterRoles(const std::vector<NEuCon::uint32>& roles)
{
    const auto hasLfe = std::find(roles.begin(), roles.end(),
        static_cast<NEuCon::uint32>(kMTR_LFE)) != roles.end();
    switch (roles.size())
    {
    case 1U: return kFORMAT_Mono;
    case 2U: return kFORMAT_Stereo;
    case 3U: return hasLfe ? kFORMAT_2dot1 : kFORMAT_LCR;
    case 4U: return kFORMAT_Quad;
    case 5U: return kFORMAT_5dot0;
    case 6U: return kFORMAT_5dot1;
    case 7U: return hasLfe ? kFORMAT_6dot1 : kFORMAT_7dot0DTS;
    case 8U: return kFORMAT_7dot1DTS;
    case 9U: return hasLfe ? kFORMAT_Unknown : kFORMAT_7dot0dot2;
    case 10U: return hasLfe ? kFORMAT_7dot1dot2 : kFORMAT_Unknown;
    default: return kFORMAT_Unknown;
    }
}

void InitializeCellLabel(EuControlKnobCell& cell, const wchar_t* shortName,
    const wchar_t* name)
{
    EuPrimitiveControl* primitive = nullptr;
    if (cell.GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) ==
        kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_IndexedString, 1U);
        primitive->LoadValueAt(0U, tEuString(shortName), tEuString(name), tEuString(name));
    }
}

void ChangeCellLabel(EuControlKnobCell& cell, const std::wstring& text)
{
    EuPrimitiveControl* primitive = nullptr;
    if (cell.GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) ==
        kERR_OK && primitive)
    {
        const auto shortText = text.substr(0U, 4U);
        const auto mediumText = text.substr(0U, 8U);
        primitive->Freeze();
        primitive->LoadValueAt(0U, shortText, mediumText, text);
        primitive->SetCurrentIndex(0U);
        primitive->Refresh();
        primitive->Thaw();
    }
}

void InitializeCellSwitch(EuControlKnobCell& cell, const NEuCon::uint32 primitiveId,
    const NEuCon::uint16 valueCount, const tSWITCH mode)
{
    EuPrimitiveControl* primitive = nullptr;
    if (cell.GetPrimitive(primitiveId, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Int, valueCount);
        primitive->LoadValueTableInterpolated(0, static_cast<NEuCon::int32>(valueCount - 1U));
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(mode);
        }
    }
}

void SetCellSwitchState(EuControlKnobCell& cell, const NEuCon::uint32 switchId,
    const NEuCon::uint32 ledId, const int value)
{
    EuPrimitiveControl* primitive = nullptr;
    if (cell.GetPrimitive(switchId, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentValue(static_cast<NEuCon::int32>(value));
        primitive->Refresh();
    }
    if (cell.GetPrimitive(ledId, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            value == 0 ? kLEDStatus_Off : kLEDStatus_On));
        primitive->Refresh();
    }
}
}

EuconChannel::EuconChannel(const int channelOrder, const NEuCon::int32 channelColor,
    const std::wstring& persistenceId,
    const std::wstring& displayName, ChangeHandler faderHandler,
    ChangeHandler knobHandler, ChangeHandler panHandler, ChangeHandler panResetHandler,
    ChangeHandler muteHandler,
    ChangeHandler soloHandler, ChangeHandler selectHandler,
    ChangeHandler recordArmHandler,
    RouteHandler outputRouteHandler, RouteHandler inputRouteHandler,
    AppActionHandler appActionHandler,
    const NEuCon::int32 trackType,
    const std::wstring& channelType)
    : channelOrder_(channelOrder),
      faderHandler_(std::move(faderHandler)),
      knobHandler_(std::move(knobHandler)),
      panHandler_(std::move(panHandler)),
      panResetHandler_(std::move(panResetHandler)),
      muteHandler_(std::move(muteHandler)),
      soloHandler_(std::move(soloHandler)),
      selectHandler_(std::move(selectHandler)),
      recordArmHandler_(std::move(recordArmHandler)),
      outputRouteHandler_(std::move(outputRouteHandler)),
      inputRouteHandler_(std::move(inputRouteHandler)),
      appActionHandler_(std::move(appActionHandler)),
      fader_(this), name_(this), number_(this), meter_(this), knobSet_(this), knob_(this),
      panKnobSet_(this), panKnob_(this), outputRouteKnobSet_(this),
      inputRouteKnobSet_(this), topLevelKnobSet_(this), windowKnobSet_(this),
      mediaKnobSet_(this)
{
    SetAttribute(kATRIBID_ProcessorType, kProcType_ChannelStrip);
    SetAttribute(kATRIBID_LayoutRule0, kRUL_EuLayoutChannel);
    SetAttribute(kATRIBID_TrackType, trackType == 0 ? kTRACK_Audio : trackType);
    SetAttribute(kATRIBID_ChannelType, channelType);
    SetAttribute(kATRIBID_ChannelOrder, channelOrder);
    // Getting Started with EuCon 12.9: surface channel color is standard
    // 0x00RRGGBB track metadata. The surface decides how to render it.
    SetAttribute2(kATRIBID_ChannelColor, channelColor, false);
    SetAttribute(kATRIBID_ModulesShowTopLevelKnobset, 1);
    SetPersistenceID(persistenceId);

    InitializeFader();
    InitializeText(name_, NameId, EuLayoutChannel::kNAM_Name, displayName);
    InitializeText(number_, NumberId, EuLayoutChannel::kNAM_ChannelNumber,
        std::to_wstring(channelOrder));
    InitializeMeter();
    InitializeKnob();
    if (panHandler_)
    {
        InitializePan();
    }
    if (soloHandler_)
    {
        InitializeSolo();
    }
    if (selectHandler_)
    {
        InitializeSelect();
    }
    if (recordArmHandler_)
    {
        InitializeRecordArm();
    }
    if (outputRouteHandler_ || inputRouteHandler_)
    {
        InitializeRouteKnobSets();
    }
    if (appActionHandler_)
    {
        InitializeApplicationKnobSets();
    }
    InitializeTopLevelKnobSet();
}

EuconChannel::~EuconChannel()
{
    topLevelKnobSet_.Freeze();
    for (const auto index : { 1U, 5U, 7U, 8U, 10U, 12U })
    {
        if (topLevelKnobs_[index])
        {
            topLevelKnobSet_.RemoveChild(topLevelMemberIds_[index]);
        }
    }
    for (std::size_t index = 0; index < topLevelKnobs_.size(); ++index)
    {
        if (topLevelKnobs_[index])
        {
            topLevelKnobSet_.Remove(topLevelMemberIds_[index]);
            topLevelKnobs_[index].reset();
        }
    }
    topLevelKnobSet_.Thaw();
    RemoveControl(topLevelKnobSet_);

    if (appActionHandler_)
    {
        std::vector<std::unique_ptr<EuControlKnobCell>> mediaCells;
        std::vector<NEuCon::uint32> mediaMemberIds;
        {
            const std::scoped_lock lock(mediaMutex_);
            mediaCells = std::move(mediaCells_);
            mediaMemberIds = std::move(mediaMemberIds_);
            mediaKinds_.clear();
        }
        mediaKnobSet_.Freeze();
        for (const auto memberId : mediaMemberIds) mediaKnobSet_.Remove(memberId);
        mediaKnobSet_.Thaw();
        mediaCells.clear();
        RemoveControl(mediaKnobSet_);
        for (std::size_t index = 0; index < windowCells_.size(); ++index)
        {
            windowKnobSet_.Remove(windowMemberIds_[index]);
            windowCells_[index].reset();
        }
        RemoveControl(windowKnobSet_);
        for (std::size_t index = 0; index < quickActionCells_.size(); ++index)
        {
            knobSet_.Remove(quickActionMemberIds_[index]);
            quickActionCells_[index].reset();
        }
    }

    for (std::size_t index = 0; index < outputRouteCells_.size(); ++index)
    {
        outputRouteKnobSet_.Remove(outputRouteMemberIds_[index]);
    }
    outputRouteCells_.clear();
    outputRouteMemberIds_.clear();
    if (outputRouteHandler_)
    {
        RemoveControl(outputRouteKnobSet_);
    }
    for (std::size_t index = 0; index < inputRouteCells_.size(); ++index)
    {
        inputRouteKnobSet_.Remove(inputRouteMemberIds_[index]);
    }
    inputRouteCells_.clear();
    inputRouteMemberIds_.clear();
    if (inputRouteHandler_)
    {
        RemoveControl(inputRouteKnobSet_);
    }
    if (solo_)
    {
        RemoveControl(*solo_);
        solo_.reset();
    }
    if (select_)
    {
        RemoveControl(*select_);
        select_.reset();
    }
    if (recordArm_)
    {
        RemoveControl(*recordArm_);
        recordArm_.reset();
    }
    if (panHandler_)
    {
        panKnobSet_.Remove(panKnobMemberId_);
        RemoveControl(panKnobSet_);
    }
    knobSet_.Remove(knobMemberId_);
    RemoveControl(knobSet_);
    RemoveControl(meter_);
    RemoveControl(number_);
    RemoveControl(name_);
    RemoveControl(fader_);
}

void EuconChannel::InitializeSolo()
{
    // Getting Started with EuCon 12.5 and the current EuLayoutChannel contract:
    // channel Solo is a persistent two-state MomentaryLatch. Windows applies
    // the intercancel policy asynchronously, so the application owns the LED
    // and confirms the authoritative state in SetSoloed().
    solo_ = std::make_unique<EuControlSwitch>(this);
    solo_->SetId(SoloId);
    solo_->SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Solo);
    EuPrimitiveControl* primitive = nullptr;
    if (solo_->GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Int, 2U);
        primitive->LoadValueTableInterpolated(0, 1);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_MomentaryLatch);
        }
    }
    solo_->SetLedOverride(true);
    AddControl(*solo_);
}

void EuconChannel::InitializeSelect()
{
    // The documented channel-layout contract places Select at this standard
    // channel role:
    // channel Select is a standard two-state MultiState switch. Windows has
    // one foreground application, so the host applies an intercancel policy
    // and sends the authoritative state back after each surface press.
    select_ = std::make_unique<EuControlSwitch>(this);
    select_->SetId(SelectId);
    select_->SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Select);
    EuPrimitiveControl* primitive = nullptr;
    if (select_->GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_Int, 2U);
        primitive->LoadValueTableInterpolated(0, 1);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_MultiState);
        }
    }
    AddControl(*select_);
}

void EuconChannel::InitializeRecordArm()
{
    // This channel uses the standard Record Arm placement as a default-device
    // selection request. It is deliberately one-shot: the surface press does
    // not own persistent state. Windows' authoritative default endpoint owns
    // the independently overridden LED.
    recordArm_ = std::make_unique<EuControlSwitch>(this);
    recordArm_->SetId(RecordArmId);
    recordArm_->SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_RecordArm);
    EuPrimitiveControl* primitive = nullptr;
    if (recordArm_->GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_Int, 1U);
        primitive->LoadValueTableInterpolated(0, 0);
        if (auto* switchPrimitive = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            switchPrimitive->SetSwitchMode(kSWITCH_OneShot);
        }
    }
    recordArm_->SetLedOverride(true);
    AddControl(*recordArm_);
}

void EuconChannel::InitializeFader()
{
    fader_.SetId(FaderId);
    fader_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Fader);

    EuPrimitiveControl* primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Slider, &primitive) == kERR_OK && primitive)
    {
        // Windows 100% maps to 0 dB. The processor may already hold 0 dB while
        // an assigned physical fader is in the overtravel zone.
        // Allow an unchanged 0 dB index to be resent to the motor.
        primitive->SetAttribute(kATRIBID_OnlySendIfDifferent, 0);
        primitive->Initialize(kTYP_Float, kFaderStepCount);
        for (NEuCon::uint16 index = 0U; index < kFaderStepCount; ++index)
        {
            const auto value = index <= kUnityIndex
                ? kMinDb + (static_cast<float>(index) / static_cast<float>(kUnityIndex)) *
                    (kMaxDb - kMinDb)
                : kMaxDb + (static_cast<float>(index - kUnityIndex) /
                    static_cast<float>((kFaderStepCount - 1U) - kUnityIndex)) * kOvertravelMaxDb;
            primitive->LoadValueAt(index, value, 2);
        }
    }

    primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_SliderTouchSense, &primitive) == kERR_OK)
    {
        if (auto* touch = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            // Touch sense is a momentary hardware state, not a toggling button.
            // It must be initialized explicitly for dependable surface touch
            // callbacks.
            touch->Initialize(kTYP_Int, 2U);
            touch->LoadValueTableInterpolated(0, 1);
            touch->SetSwitchMode(kSWITCH_Raw);
        }
    }

    primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Mute, &primitive) == kERR_OK)
    {
        if (auto* mute = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            mute->Initialize(kTYP_Int, 2U);
            mute->LoadValueTableInterpolated(0, 1);
            mute->SetSwitchMode(kSWITCH_MultiState);
        }
    }
    AddControl(fader_);
}

void EuconChannel::InitializeText(EuControlTextDisplay& display, const NEuCon::uint32 id,
    const NEuCon::int32 layoutName, const std::wstring& text)
{
    display.SetId(id);
    display.SetAttribute(kATRIBID_LayoutName0, layoutName);
    EuPrimitiveControl* primitive = nullptr;
    if (display.GetPrimitive(EuControlTextDisplay::kID_TextDisplay, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_IndexedString, 1U);
        primitive->LoadValueAt(0U, text);
    }
    AddControl(display);
}

void EuconChannel::InitializeMeter()
{
    meter_.SetId(MeterId);
    meter_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_ChannelLevelMeter);
    // Keep the regular EUCON meter primitive fully described as a fallback.
    // Meter API 3.1 ignores NumberOfMetersInChannel, so these attributes do
    // not interfere with the batched stereo format configured after register.
    meter_.SetAttribute(kATRIBID_NumberOfMetersInChannel, 1U);
    meter_.SetAttribute(kATRIBID_MeterType, kMeterType__SamplePeak);
    meter_.SetMasterMeterType(kMETERTYPE_SignalLevel);
    EuPrimitiveControl* primitive = nullptr;
    if (meter_.GetPrimitive(EuControlMultiMeter::kID_Meter0, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Float, 101U);
        primitive->LoadValueTableInterpolated(-120.0F, 0.0F);
        if (auto* meterPrimitive = dynamic_cast<EuPrimitiveMeter*>(primitive))
        {
            meterPrimitive->SetMeterType(kMETERTYPE_SignalLevel);
            meterPrimitive->SetRole(kMTR_Mono);
            meterPrimitive->SetMeterPositionMode(kMeterThermometerUp);
        }
    }
    AddControl(meter_);
    meter_.SetUserData(kMETERTYPE_SignalLevel, this);
}

void EuconChannel::InitializeKnob()
{
    knobSet_.SetId(KnobSetId);
    // Session volume is an application-specific convenience parameter, not
    // DAW input gain. The official second-page convention places such
    // parameters in Quick Controls (top-level knob set 11).
    knobSet_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_TopLevelKnobSet11);
    knobSet_.SetAttribute(kATRIBID_FuncPersID, "FaderBridge.Chan.QuickControls");
    knobSet_.Freeze();
    AddControl(knobSet_);

    EuPrimitiveControl* primitive = nullptr;
    if (knob_.GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Float, 193U);
        primitive->LoadValueTableInterpolated(kMinDb, kMaxDb);
        if (auto* rotary = dynamic_cast<EuPrimitiveKnob*>(primitive))
        {
            rotary->SetPositionRingMode(kRingPoint);
        }
    }

    primitive = nullptr;
    if (knob_.GetPrimitive(EuControlKnobCell::kID_KnobTouchSense, &primitive) == kERR_OK)
    {
        if (auto* touch = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            touch->Initialize(kTYP_Int, 2U);
            touch->LoadValueTableInterpolated(0, 1);
            touch->SetSwitchMode(kSWITCH_Raw);
        }
    }

    primitive = nullptr;
    if (knob_.GetPrimitive(EuControlKnobCell::kID_KnobTopSwitch, &primitive) == kERR_OK)
    {
        if (auto* reset = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            reset->Initialize(kTYP_Int, 2U);
            reset->LoadValueTableInterpolated(0, 1);
            reset->SetSwitchMode(kSWITCH_Raw);
        }
    }

    primitive = nullptr;
    if (knob_.GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_IndexedString, 1U);
        primitive->LoadValueAt(0U, tEuString(L"Vol"), tEuString(L"Volume"), tEuString(L"Session Volume"));
    }
    knobSet_.PushBack(&knob_, knobMemberId_);
    knobSet_.Thaw();
}

void EuconChannel::InitializeApplicationKnobSets()
{
    // Quick Controls is the documented application-specific page. Keep the
    // continuous session volume in the first cell and add deterministic reset
    // operations to the remaining cells.
    const std::array<std::pair<const wchar_t*, const wchar_t*>, 5> quickLabels = {{
        { L"Pan0", L"Center Pan" },
        { L"DOut", L"Default Output" },
        { L"DIn", L"Default Input" },
        { L"UnMt", L"Unmute" },
        { L"ClrS", L"Clear Solo" },
    }};
    knobSet_.Freeze();
    for (std::size_t index = 0; index < quickActionCells_.size(); ++index)
    {
        auto cell = std::make_unique<EuControlKnobCell>(this);
        InitializeCellLabel(*cell, quickLabels[index].first, quickLabels[index].second);
        InitializeCellSwitch(*cell, EuControlKnobCell::kID_LowerSwitch, 2U, kSWITCH_Raw);
        knobSet_.PushBack(cell.get(), quickActionMemberIds_[index]);
        quickActionCells_[index] = std::move(cell);
    }
    knobSet_.Thaw();

    windowKnobSet_.SetId(WindowKnobSetId);
    windowKnobSet_.SetAttribute(kATRIBID_LayoutName0,
        EuLayoutChannel::kNAM_TopLevelKnobSet13);
    windowKnobSet_.SetAttribute(kATRIBID_FuncPersID,
        "FaderBridge.Chan.WindowControls");
    windowKnobSet_.Freeze();
    AddControl(windowKnobSet_);
    const std::array<std::pair<const wchar_t*, const wchar_t*>, 4> windowLabels = {{
        { L"Show", L"Focus Window" },
        { L"Min", L"Minimize" },
        { L"Max", L"Maximize" },
        { L"Top", L"Always On Top" },
    }};
    for (std::size_t index = 0; index < windowCells_.size(); ++index)
    {
        auto cell = std::make_unique<EuControlKnobCell>(this);
        InitializeCellLabel(*cell, windowLabels[index].first, windowLabels[index].second);
        InitializeCellSwitch(*cell, EuControlKnobCell::kID_LowerSwitch, 2U,
            index >= 2U ? kSWITCH_MomentaryLatch : kSWITCH_Raw);
        windowKnobSet_.PushBack(cell.get(), windowMemberIds_[index]);
        windowCells_[index] = std::move(cell);
    }
    windowKnobSet_.Thaw();

    mediaKnobSet_.SetId(MediaKnobSetId);
    mediaKnobSet_.SetAttribute(kATRIBID_LayoutName0,
        EuLayoutChannel::kNAM_TopLevelKnobSet9);
    mediaKnobSet_.SetAttribute(kATRIBID_FuncPersID,
        "FaderBridge.Chan.MediaControls");
    mediaKnobSet_.Freeze();
    AddControl(mediaKnobSet_);
    mediaKnobSet_.Thaw();
}

void EuconChannel::InitializePan()
{
    // Getting Started with EUCON section 8 and the current EuLayoutChannel
    // contract define Pan as a predefined function knob set. Windows Core
    // Audio exposes stereo session balance rather than a routing panner, so
    // this is one accurately labelled balance control within that function.
    panKnobSet_.SetId(PanKnobSetId);
    panKnobSet_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Pan);
    panKnobSet_.SetAttribute(kATRIBID_FuncPersID, kChanFuncID_Pan);
    panKnobSet_.Freeze();
    AddControl(panKnobSet_);

    EuPrimitiveControl* primitive = nullptr;
    if (panKnob_.GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Float, 201U);
        primitive->LoadValueTableInterpolated(-100.0F, 100.0F);
        for (int percentage = 100; percentage > 0; --percentage)
        {
            const auto index = static_cast<NEuCon::uint16>(100 - percentage);
            const auto shortText = std::to_wstring(percentage) + L"L";
            const auto longText = std::to_wstring(percentage) + L"% L";
            primitive->LoadValueAt(index, shortText, shortText, longText);
        }
        primitive->LoadValueAt(100U, tEuString(L"C"), tEuString(L"Center"),
            tEuString(L"Center"));
        for (int percentage = 1; percentage <= 100; ++percentage)
        {
            const auto index = static_cast<NEuCon::uint16>(100 + percentage);
            const auto shortText = std::to_wstring(percentage) + L"R";
            const auto longText = std::to_wstring(percentage) + L"% R";
            primitive->LoadValueAt(index, shortText, shortText, longText);
        }
        if (auto* rotary = dynamic_cast<EuPrimitiveKnob*>(primitive))
        {
            rotary->SetPositionRingMode(kRingCenterAnchored);
        }
    }

    primitive = nullptr;
    if (panKnob_.GetPrimitive(EuControlKnobCell::kID_KnobTouchSense, &primitive) == kERR_OK)
    {
        if (auto* touch = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            touch->Initialize(kTYP_Int, 2U);
            touch->LoadValueTableInterpolated(0, 1);
            touch->SetSwitchMode(kSWITCH_Raw);
        }
    }

    primitive = nullptr;
    if (panKnob_.GetPrimitive(EuControlKnobCell::kID_KnobTopSwitch, &primitive) == kERR_OK)
    {
        if (auto* topSwitch = dynamic_cast<EuPrimitiveSwitch*>(primitive))
        {
            // Getting Started with EUCON 14.7.1 defines knob-top press as the
            // conventional parameter-default action. Pan's default is Center.
            topSwitch->Initialize(kTYP_Int, 2U);
            topSwitch->LoadValueTableInterpolated(0, 1);
            topSwitch->SetSwitchMode(kSWITCH_Raw);
        }
    }

    primitive = nullptr;
    if (panKnob_.GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->Initialize(kTYP_IndexedString, 1U);
        primitive->LoadValueAt(0U, tEuString(L"Pan"), tEuString(L"Balance"),
            tEuString(L"Windows Session Balance"));
    }
    panKnobSet_.PushBack(&panKnob_, panKnobMemberId_);
    panKnobSet_.Thaw();
}

void EuconChannel::InitializeRouteKnobSets()
{
    if (outputRouteHandler_)
    {
        outputRouteKnobSet_.SetId(OutputRouteKnobSetId);
        outputRouteKnobSet_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Mix);
        outputRouteKnobSet_.SetAttribute(kATRIBID_FuncPersID, kChanFuncID_Mix);
        AddControl(outputRouteKnobSet_);
        RebuildRouteKnobSet(outputRouteKnobSet_, outputRouteCells_,
            outputRouteMemberIds_, {}, {}, true);
    }
    if (inputRouteHandler_)
    {
        inputRouteKnobSet_.SetId(InputRouteKnobSetId);
        inputRouteKnobSet_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Input);
        inputRouteKnobSet_.SetAttribute(kATRIBID_FuncPersID, kChanFuncID_Input);
        AddControl(inputRouteKnobSet_);
        RebuildRouteKnobSet(inputRouteKnobSet_, inputRouteCells_,
            inputRouteMemberIds_, {}, {}, false);
    }
}

void EuconChannel::InitializeTopLevelKnobSet()
{
    // The documented EUCON knob-set hierarchy requires a real top-level
    // container instead of relying on a
    // surface-generated top page. This lets EuControl/WSControl map the same
    // application model to any compatible surface.
    topLevelKnobSet_.SetId(TopLevelKnobSetId);
    topLevelKnobSet_.SetAttribute(kATRIBID_LayoutName0,
        EuLayoutChannel::kNAM_TopLevelKnobset);
    topLevelKnobSet_.Freeze();
    AddControl(topLevelKnobSet_);

    struct TopLevelSpec
    {
        const wchar_t* shortName;
        const wchar_t* name;
        const char* functionId;
        bool enabled;
    };
    const std::array<TopLevelSpec, 16> specs = {{
        { L"", L"", kChanFuncID_Inserts, false },
        { L"In", L"Input", kChanFuncID_Input, static_cast<bool>(inputRouteHandler_) },
        { L"", L"", kChanFuncID_Dynamics, false },
        { L"", L"", kChanFuncID_EQ, false },
        { L"", L"", kChanFuncID_AuxSend, false },
        { L"Pan", L"Pan", kChanFuncID_Pan, static_cast<bool>(panHandler_) },
        { L"", L"", kChanFuncID_Group, false },
        { L"Out", L"Output", kChanFuncID_Mix, static_cast<bool>(outputRouteHandler_) },
        { L"Media", L"Media", "FaderBridge.Chan.MediaControls",
            static_cast<bool>(appActionHandler_) },
        { L"", L"", "FaderBridge.Chan.Instruments", false },
        { L"QCtrl", L"Quick", "FaderBridge.Chan.QuickControls", true },
        { L"", L"", "FaderBridge.Chan.Filters", false },
        { L"Win", L"Window", "FaderBridge.Chan.WindowControls",
            static_cast<bool>(appActionHandler_) },
        { L"", L"", "FaderBridge.Chan.PanRelated", false },
        { L"", L"", "FaderBridge.Chan.GroupRelated", false },
        { L"", L"", "FaderBridge.Chan.OutputRelated", false },
    }};

    for (std::size_t index = 0; index < specs.size(); ++index)
    {
        const auto& spec = specs[index];
        auto cell = std::make_unique<EuControlKnobCell>(this);
        cell->SetAttribute2(kATRIBID_FuncPersID, spec.functionId, false);
        cell->SetAttribute2(kATRIBID_NumberOfChildren, spec.enabled ? 1 : 0, false);
        EuPrimitiveControl* primitive = nullptr;
        if (cell->GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) ==
            kERR_OK && primitive)
        {
            primitive->Initialize(kTYP_IndexedString, 1U);
            primitive->LoadValueAt(0U, tEuString(spec.shortName), tEuString(spec.name),
                tEuString(spec.name));
        }
        topLevelKnobSet_.PushBack(cell.get(), topLevelMemberIds_[index]);
        topLevelKnobs_[index] = std::move(cell);
    }

    const auto addChild = [this](const std::size_t index, EuControlKnobCellArray& child)
    {
        topLevelKnobs_[index]->Freeze();
        topLevelKnobSet_.AddChild(topLevelMemberIds_[index], child);
    };
    if (inputRouteHandler_) addChild(1U, inputRouteKnobSet_);
    if (panHandler_) addChild(5U, panKnobSet_);
    if (outputRouteHandler_) addChild(7U, outputRouteKnobSet_);
    if (appActionHandler_) addChild(8U, mediaKnobSet_);
    addChild(10U, knobSet_);
    if (appActionHandler_) addChild(12U, windowKnobSet_);
    topLevelKnobSet_.Thaw();
}

void EuconChannel::RebuildRouteKnobSet(EuControlKnobCellArray& knobSet,
    std::vector<std::unique_ptr<EuControlKnobCell>>& cells,
    std::vector<NEuCon::uint32>& memberIds,
    const std::vector<RouteOption>& options, const std::wstring& selectedId,
    const bool output)
{
    std::vector<std::unique_ptr<EuControlKnobCell>> oldCells;
    std::vector<NEuCon::uint32> oldMemberIds;
    {
        const std::scoped_lock lock(routeMutex_);
        oldCells = std::move(cells);
        oldMemberIds = std::move(memberIds);
        (output ? outputRouteOptions_ : inputRouteOptions_).clear();
    }

    knobSet.Freeze();
    for (const auto memberId : oldMemberIds)
    {
        knobSet.Remove(memberId);
    }
    oldCells.clear();

    std::vector<RouteOption> allOptions;
    allOptions.reserve(options.size() + 1U);
    allOptions.push_back({ L"", L"Default", 0x00FFFFFF });
    allOptions.insert(allOptions.end(), options.begin(), options.end());

    std::vector<std::unique_ptr<EuControlKnobCell>> newCells;
    std::vector<NEuCon::uint32> newMemberIds;
    newCells.reserve(allOptions.size());
    newMemberIds.reserve(allOptions.size());
    for (const auto& option : allOptions)
    {
        auto cell = std::make_unique<EuControlKnobCell>(this);
        EuPrimitiveControl* primitive = nullptr;
        if (cell->GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK && primitive)
        {
            primitive->Initialize(kTYP_Int, 1U);
            primitive->LoadValueAt(0U, 0);
            if (auto* rotary = dynamic_cast<EuPrimitiveKnob*>(primitive))
            {
                rotary->SetPositionRingMode(kRingOff);
            }
        }
        if (cell->GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) ==
            kERR_OK && primitive)
        {
            primitive->Initialize(kTYP_IndexedString, 1U);
            const auto shortName = option.name.substr(0U, 4U);
            const auto mediumName = option.name.substr(0U, 8U);
            primitive->LoadValueAt(0U, shortName, mediumName, option.name);
        }
        if (cell->GetPrimitive(EuControlKnobCell::kID_LowerSwitch, &primitive) ==
            kERR_OK && primitive)
        {
            primitive->Initialize(kTYP_Int, 2U);
            primitive->LoadValueTableInterpolated(0, 1);
            if (auto* routeSwitch = dynamic_cast<EuPrimitiveSwitch*>(primitive))
            {
                routeSwitch->SetSwitchMode(kSWITCH_MomentaryLatch);
            }
        }
        if (cell->GetPrimitive(EuControlKnobCell::kID_FunctionLed, &primitive) ==
            kERR_OK && primitive)
        {
            primitive->SetAttribute(kATRIBID_ARGBColor, option.color,
                kAttrNotify_PrimitiveControl);
        }
        NEuCon::uint32 memberId = 0U;
        knobSet.PushBack(cell.get(), memberId);
        newMemberIds.push_back(memberId);
        newCells.push_back(std::move(cell));
    }
    knobSet.Thaw();

    {
        const std::scoped_lock lock(routeMutex_);
        cells = std::move(newCells);
        memberIds = std::move(newMemberIds);
        (output ? outputRouteOptions_ : inputRouteOptions_) = std::move(allOptions);
        (output ? selectedOutputRouteId_ : selectedInputRouteId_) = selectedId;
    }
    UpdateRouteSelection(output, selectedId);
}

void EuconChannel::UpdateRouteSelection(const bool output, const std::wstring& selectedId)
{
    std::vector<std::pair<EuControlKnobCell*, bool>> updates;
    {
        const std::scoped_lock lock(routeMutex_);
        auto& options = output ? outputRouteOptions_ : inputRouteOptions_;
        auto& cells = output ? outputRouteCells_ : inputRouteCells_;
        (output ? selectedOutputRouteId_ : selectedInputRouteId_) = selectedId;
        const auto count = std::min(options.size(), cells.size());
        updates.reserve(count);
        for (std::size_t index = 0; index < count; ++index)
        {
            updates.emplace_back(cells[index].get(), options[index].id == selectedId);
        }
    }
    for (const auto& [cell, selected] : updates)
    {
        EuPrimitiveControl* primitive = nullptr;
        if (cell->GetPrimitive(EuControlKnobCell::kID_LowerSwitch, &primitive) ==
            kERR_OK && primitive)
        {
            primitive->SetCurrentValue(selected ? 1 : 0);
        }
        if (cell->GetPrimitive(EuControlKnobCell::kID_LowerSwitchLed, &primitive) ==
            kERR_OK && primitive)
        {
            primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
                selected ? kLEDStatus_On : kLEDStatus_Off));
            primitive->Refresh();
        }
    }
}

void EuconChannel::SetRouteOptions(const std::vector<RouteOption>& outputOptions,
    const std::wstring& selectedOutputId,
    const std::vector<RouteOption>& inputOptions,
    const std::wstring& selectedInputId)
{
    const auto sameOptions = [](const std::vector<RouteOption>& current,
        const std::vector<RouteOption>& desired)
    {
        if (current.size() != desired.size() + 1U) return false;
        for (std::size_t index = 0; index < desired.size(); ++index)
        {
            const auto& left = current[index + 1U];
            const auto& right = desired[index];
            if (left.id != right.id || left.name != right.name || left.color != right.color)
            {
                return false;
            }
        }
        return true;
    };

    bool rebuildOutput = false;
    bool rebuildInput = false;
    bool updateOutput = false;
    bool updateInput = false;
    {
        const std::scoped_lock lock(routeMutex_);
        rebuildOutput = outputRouteHandler_ && !sameOptions(outputRouteOptions_, outputOptions);
        rebuildInput = inputRouteHandler_ && !sameOptions(inputRouteOptions_, inputOptions);
        updateOutput = outputRouteHandler_ && selectedOutputRouteId_ != selectedOutputId;
        updateInput = inputRouteHandler_ && selectedInputRouteId_ != selectedInputId;
    }
    if (rebuildOutput)
    {
        RebuildRouteKnobSet(outputRouteKnobSet_, outputRouteCells_, outputRouteMemberIds_,
            outputOptions, selectedOutputId, true);
    }
    else if (updateOutput)
    {
        UpdateRouteSelection(true, selectedOutputId);
    }
    if (rebuildInput)
    {
        RebuildRouteKnobSet(inputRouteKnobSet_, inputRouteCells_, inputRouteMemberIds_,
            inputOptions, selectedInputId, false);
    }
    else if (updateInput)
    {
        UpdateRouteSelection(false, selectedInputId);
    }
}

void EuconChannel::SetWindowState(const bool available, const bool foreground,
    const bool minimized, const bool maximized, const bool topmost)
{
    if (!appActionHandler_)
    {
        return;
    }
    SetCellSwitchState(*windowCells_[0], EuControlKnobCell::kID_LowerSwitch,
        EuControlKnobCell::kID_LowerSwitchLed, available && foreground ? 1 : 0);
    SetCellSwitchState(*windowCells_[1], EuControlKnobCell::kID_LowerSwitch,
        EuControlKnobCell::kID_LowerSwitchLed, available && minimized ? 1 : 0);
    SetCellSwitchState(*windowCells_[2], EuControlKnobCell::kID_LowerSwitch,
        EuControlKnobCell::kID_LowerSwitchLed, available && maximized ? 1 : 0);
    SetCellSwitchState(*windowCells_[3], EuControlKnobCell::kID_LowerSwitch,
        EuControlKnobCell::kID_LowerSwitchLed, available && topmost ? 1 : 0);
}

void EuconChannel::RebuildMediaKnobSet(
    const std::vector<MediaCellKind>& desiredKinds, const std::wstring& title,
    const std::wstring& artist)
{
    std::vector<std::unique_ptr<EuControlKnobCell>> oldCells;
    std::vector<NEuCon::uint32> oldMemberIds;
    {
        const std::scoped_lock lock(mediaMutex_);
        oldCells = std::move(mediaCells_);
        oldMemberIds = std::move(mediaMemberIds_);
        mediaKinds_.clear();
    }

    mediaKnobSet_.Freeze();
    for (const auto memberId : oldMemberIds) mediaKnobSet_.Remove(memberId);
    oldCells.clear();

    std::vector<std::unique_ptr<EuControlKnobCell>> newCells;
    std::vector<NEuCon::uint32> newMemberIds;
    newCells.reserve(desiredKinds.size());
    newMemberIds.reserve(desiredKinds.size());
    for (const auto kind : desiredKinds)
    {
        auto cell = std::make_unique<EuControlKnobCell>(this);
        const wchar_t* shortLabel = L"";
        const wchar_t* longLabel = L"";
        switch (kind)
        {
        case MediaCellKind::Title:
            InitializeCellLabel(*cell, title.substr(0U, 4U).c_str(), title.c_str());
            break;
        case MediaCellKind::Artist:
            InitializeCellLabel(*cell, artist.substr(0U, 4U).c_str(), artist.c_str());
            break;
        case MediaCellKind::PlayPause: shortLabel = L"Play"; longLabel = L"Play Pause"; break;
        case MediaCellKind::Previous: shortLabel = L"Prev"; longLabel = L"Previous"; break;
        case MediaCellKind::Next: shortLabel = L"Next"; longLabel = L"Next"; break;
        case MediaCellKind::Stop: shortLabel = L"Stop"; longLabel = L"Stop"; break;
        case MediaCellKind::Position: shortLabel = L"Pos"; longLabel = L"Position"; break;
        case MediaCellKind::Seek: shortLabel = L"Time"; longLabel = L"Position"; break;
        case MediaCellKind::Shuffle: shortLabel = L"Shuf"; longLabel = L"Shuffle"; break;
        case MediaCellKind::Repeat: shortLabel = L"Rpt"; longLabel = L"Repeat"; break;
        }
        if (kind != MediaCellKind::Title && kind != MediaCellKind::Artist)
        {
            InitializeCellLabel(*cell, shortLabel, longLabel);
        }
        if (kind == MediaCellKind::Seek)
        {
            EuPrimitiveControl* primitive = nullptr;
            if (cell->GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK &&
                primitive)
            {
                primitive->Initialize(kTYP_Float, 1001U);
                primitive->LoadValueTableInterpolated(0.0F, 100.0F);
                if (auto* rotary = dynamic_cast<EuPrimitiveKnob*>(primitive))
                {
                    rotary->SetPositionRingMode(kRingPoint);
                }
            }
        }
        else if (kind != MediaCellKind::Title && kind != MediaCellKind::Artist &&
            kind != MediaCellKind::Position)
        {
            const auto repeat = kind == MediaCellKind::Repeat;
            const auto latched = kind == MediaCellKind::PlayPause ||
                kind == MediaCellKind::Shuffle || repeat;
            InitializeCellSwitch(*cell, EuControlKnobCell::kID_LowerSwitch,
                repeat ? 3U : 2U, latched ? kSWITCH_MomentaryLatch : kSWITCH_Raw);
        }
        NEuCon::uint32 memberId = 0U;
        mediaKnobSet_.PushBack(cell.get(), memberId);
        newMemberIds.push_back(memberId);
        newCells.push_back(std::move(cell));
    }
    mediaKnobSet_.Thaw();
    {
        const std::scoped_lock lock(mediaMutex_);
        mediaCells_ = std::move(newCells);
        mediaMemberIds_ = std::move(newMemberIds);
        mediaKinds_ = desiredKinds;
    }
    FB_TRACE("MEDIA_MODEL track=%d cells=%u", channelOrder_.load(),
        static_cast<unsigned>(desiredKinds.size()));
}

void EuconChannel::UpdateMediaLabel(const MediaCellKind kind,
    const std::wstring& text)
{
    EuControlKnobCell* cell = nullptr;
    {
        const std::scoped_lock lock(mediaMutex_);
        const auto found = std::find(mediaKinds_.begin(), mediaKinds_.end(), kind);
        if (found != mediaKinds_.end())
        {
            cell = mediaCells_[static_cast<std::size_t>(
                std::distance(mediaKinds_.begin(), found))].get();
        }
    }
    if (cell) ChangeCellLabel(*cell, text);
}

void EuconChannel::SetMediaState(const bool available, const bool playing,
    const bool canPlayPause, const bool canPrevious, const bool canNext,
    const bool canStop, const bool hasPosition, const bool canSeek, const bool canShuffle,
    const bool shuffle, const bool canRepeat, const int repeatMode,
    const float position, const std::wstring& title, const std::wstring& artist)
{
    if (!appActionHandler_) return;

    std::vector<MediaCellKind> desiredKinds;
    if (available)
    {
        if (!title.empty()) desiredKinds.push_back(MediaCellKind::Title);
        if (!artist.empty()) desiredKinds.push_back(MediaCellKind::Artist);
        if (canPlayPause) desiredKinds.push_back(MediaCellKind::PlayPause);
        if (canPrevious) desiredKinds.push_back(MediaCellKind::Previous);
        if (canNext) desiredKinds.push_back(MediaCellKind::Next);
        if (canStop) desiredKinds.push_back(MediaCellKind::Stop);
        if (hasPosition)
        {
            desiredKinds.push_back(canSeek ? MediaCellKind::Seek : MediaCellKind::Position);
        }
        if (canShuffle) desiredKinds.push_back(MediaCellKind::Shuffle);
        if (canRepeat) desiredKinds.push_back(MediaCellKind::Repeat);
    }
    bool rebuild = false;
    {
        const std::scoped_lock lock(mediaMutex_);
        rebuild = mediaKinds_ != desiredKinds;
    }
    if (rebuild) RebuildMediaKnobSet(desiredKinds, title, artist);
    UpdateMediaLabel(MediaCellKind::Title, title);
    UpdateMediaLabel(MediaCellKind::Artist, artist);
    UpdateMediaLabel(MediaCellKind::Position,
        L"Pos " + std::to_wstring(static_cast<int>(std::lround(
            std::clamp(position, 0.0F, 1.0F) * 100.0F))) + L"%");

    std::vector<std::pair<MediaCellKind, EuControlKnobCell*>> cells;
    {
        const std::scoped_lock lock(mediaMutex_);
        for (std::size_t index = 0; index < mediaKinds_.size(); ++index)
        {
            cells.emplace_back(mediaKinds_[index], mediaCells_[index].get());
        }
    }
    for (const auto& [kind, cell] : cells)
    {
        if (kind == MediaCellKind::PlayPause)
        {
            SetCellSwitchState(*cell, EuControlKnobCell::kID_LowerSwitch,
                EuControlKnobCell::kID_LowerSwitchLed, playing ? 1 : 0);
        }
        else if (kind == MediaCellKind::Shuffle)
        {
            SetCellSwitchState(*cell, EuControlKnobCell::kID_LowerSwitch,
                EuControlKnobCell::kID_LowerSwitchLed, shuffle ? 1 : 0);
        }
        else if (kind == MediaCellKind::Repeat)
        {
            SetCellSwitchState(*cell, EuControlKnobCell::kID_LowerSwitch,
                EuControlKnobCell::kID_LowerSwitchLed, std::clamp(repeatMode, 0, 2));
        }
        else if (kind == MediaCellKind::Seek)
        {
            EuPrimitiveControl* primitive = nullptr;
            if (cell->GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) ==
                kERR_OK && primitive)
            {
                primitive->SetCurrentValue(std::clamp(position, 0.0F, 1.0F) * 100.0F);
                primitive->Refresh();
            }
        }
    }
}

bool EuconChannel::SetFaderNormalized(const float value)
{
    EuPrimitiveControl* primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Slider, &primitive) == kERR_OK && primitive)
    {
        // The input callback established the exact Windows range: indices
        // 0..728 are -96..0 dB. Write that same coordinate directly instead
        // of asking the SDK to reverse-search a 1024-entry table that also
        // contains the 0..+12 dB overtravel region.
        const auto index = static_cast<NEuCon::uint16>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * static_cast<float>(kUnityIndex)));
        FB_TRACE("MOTOR_CMD track=%d value=%.4f index=%u", channelOrder_.load(),
            value, static_cast<unsigned>(index));
        const auto setResult = primitive->SetCurrentIndex(index);
        // Ensure an unchanged target is delivered after an overtravel callback.
        const auto refreshResult = setResult == kERR_OK ? primitive->Refresh() : setResult;
        FB_TRACE("MOTOR_RESULT track=%d index=%u set=%d refresh=%d",
            channelOrder_.load(), static_cast<unsigned>(index),
            static_cast<int>(setResult), static_cast<int>(refreshResult));
        return setResult == kERR_OK && refreshResult == kERR_OK;
    }
    return false;
}

void EuconChannel::SetKnobNormalized(const float value)
{
    EuPrimitiveControl* primitive = nullptr;
    if (knob_.GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK && primitive)
    {
        const auto index = static_cast<NEuCon::uint16>(std::lround(
            std::clamp(value, 0.0F, 1.0F) * 192.0F));
        FB_TRACE("KNOB_RING_CMD track=%d value=%.4f index=%u", channelOrder_.load(),
            value, static_cast<unsigned>(index));
        primitive->SetCurrentIndex(index);
    }
}

void EuconChannel::SetPan(const float value)
{
    if (!panHandler_)
    {
        return;
    }
    EuPrimitiveControl* primitive = nullptr;
    if (panKnob_.GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK && primitive)
    {
        const auto index = static_cast<NEuCon::uint16>(std::lround(
            (std::clamp(value, -1.0F, 1.0F) + 1.0F) * 100.0F));
        FB_TRACE("PAN_RING_CMD track=%d value=%.4f index=%u", channelOrder_.load(),
            value, static_cast<unsigned>(index));
        primitive->SetCurrentIndex(index);
    }
}

void EuconChannel::SetName(const std::wstring& value)
{
    EuPrimitiveControl* primitive = nullptr;
    if (name_.GetPrimitive(EuControlTextDisplay::kID_TextDisplay, &primitive) == kERR_OK && primitive)
    {
        primitive->ChangeText(value);
    }
}

void EuconChannel::SetOrder(const int channelOrder)
{
    if (channelOrder_.exchange(channelOrder) == channelOrder)
    {
        return;
    }

    // Getting Started with EuCon, section 12.4: a channel processor stays
    // with its track. Reordering changes only ChannelOrder and channel number,
    // while the owning node is frozen by EuconHost.
    SetAttribute2(kATRIBID_ChannelOrder, channelOrder, true);
    EuPrimitiveControl* primitive = nullptr;
    if (number_.GetPrimitive(EuControlTextDisplay::kID_TextDisplay, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->ChangeText(std::to_wstring(channelOrder));
    }
}

void EuconChannel::ApplyPendingFaderRebound()
{
    if (faderReboundPending_.exchange(false))
    {
        SetFaderNormalized(1.0F);
    }
}

void EuconChannel::SetMuted(const bool muted)
{
    EuPrimitiveControl* primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Mute, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentValue(muted ? 1 : 0);
    }

    primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_MuteLed, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            muted ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
}

void EuconChannel::SetSoloed(const bool soloed)
{
    if (!solo_)
    {
        return;
    }
    EuPrimitiveControl* primitive = nullptr;
    if (solo_->GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentValue(soloed ? 1 : 0);
    }
    primitive = nullptr;
    if (solo_->GetPrimitive(EuControlSwitch::kID_Led, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            soloed ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
}

void EuconChannel::SetSelected(const bool selected)
{
    if (!select_)
    {
        return;
    }
    EuPrimitiveControl* primitive = nullptr;
    if (select_->GetPrimitive(EuControlSwitch::kID_Switch, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->SetCurrentValue(selected ? 1 : 0);
        primitive->Refresh();
    }
}

void EuconChannel::SetRecordArmed(const bool armed)
{
    if (!recordArm_)
    {
        return;
    }
    EuPrimitiveControl* primitive = nullptr;
    if (recordArm_->GetPrimitive(EuControlSwitch::kID_Led, &primitive) == kERR_OK &&
        primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            armed ? kLEDStatus_On : kLEDStatus_Off));
        primitive->Refresh();
    }
}

void EuconChannel::SetTrackMetadata(const NEuCon::int32 trackType,
    const std::wstring& channelType)
{
    SetAttribute2(kATRIBID_TrackType, trackType, true);
    SetAttribute2(kATRIBID_ChannelType, channelType, true);
}

void EuconChannel::PostRegisterMeterInitialization(const bool forceMono,
    const std::vector<NEuCon::uint32>& roles)
{
    const auto meterTypeResult = meter_.SetAttribute2(
        kATRIBID_MeterType, kMeterType__SamplePeak, true);
    const auto processorMeterTypeResult = SetAttribute2(
        kATRIBID_MeterType, kMeterType__SamplePeak, true);
    ConfigureMeter(forceMono, roles);
    FB_TRACE("METER_SETUP track=%d meterType=%d processorType=%d",
        channelOrder_.load(), static_cast<int>(meterTypeResult),
        static_cast<int>(processorMeterTypeResult));
}

void EuconChannel::ConfigureMeter(const bool forceMono,
    const std::vector<NEuCon::uint32>& sourceRoles)
{
    std::vector<NEuCon::uint32> roles = forceMono
        ? std::vector<NEuCon::uint32>{ kMTR_Mono } : sourceRoles;
    if (roles.empty())
    {
        roles.push_back(kMTR_Mono);
    }
    if (roles.size() > kEuMaxLegsPerMeter_3_1_API)
    {
        roles.resize(kEuMaxLegsPerMeter_3_1_API);
    }
    if (meterConfigured_ && roles == configuredMeterRoles_)
    {
        return;
    }

    const auto trackFormat = TrackFormatForMeterRoles(roles);
    const auto trackFormatResult = SetAttribute2(kATRIBID_TrackFormat, trackFormat, true);
    const auto flags = static_cast<NEuCon::uint32>(
        kEuMFMT_PL_Level | kEuMFMT_PL_Peak | kEuMFMT_PL_Clip |
        kEuMFMT_PM_MasterPeak | kEuMFMT_PM_MasterClip);
    const auto formatResult = meter_.SetFormat(
        EuMakeMeterFormat(static_cast<NEuCon::uint32>(roles.size()), flags), roles);
    if (formatResult == kERR_OK)
    {
        configuredMeterRoles_ = std::move(roles);
        meterConfigured_ = true;
    }
    FB_TRACE("METER_FORMAT track=%d mono=%d legs=%u trackFormat=%d trackResult=%d formatResult=%d",
        channelOrder_.load(), forceMono ? 1 : 0,
        static_cast<unsigned>(configuredMeterRoles_.size()),
        static_cast<int>(trackFormat), static_cast<int>(trackFormatResult),
        static_cast<int>(formatResult));
}

void EuconChannel::SetMeterVisibility(const bool visible, const tVisibilityHandle handle,
    const tEuMeterFormat format)
{
    const std::scoped_lock lock(meterMutex_);
    meterVisible_ = visible;
    meterVisibilityHandle_ = handle;
    meterFormat_ = format;
    meterFallbackLogged_.store(false);
    lastMeterResult_.store(-1);
    FB_TRACE("METER_VIS track=%d visible=%d handle=%u format=%u",
        channelOrder_.load(), visible ? 1 : 0, static_cast<unsigned>(handle),
        static_cast<unsigned>(format));
}

void EuconChannel::WriteMeterDb(EuBatchedMeterWriter& writer,
    const std::vector<float>& valuesDb)
{
    tVisibilityHandle handle = kEuInvalidVisibilityHandle;
    tEuMeterFormat format = kEuInvalidMeterFormat;
    bool visible = false;
    {
        const std::scoped_lock lock(meterMutex_);
        visible = meterVisible_;
        handle = meterVisibilityHandle_;
        format = meterFormat_;
    }

    // Getting Started with EuCon 13.3.9-13.3.10 requires the application to
    // use visibility, handle and format saved by the node callback. Do not
    // query the meter object on this hot path or infer visibility.
    if (!visible || handle == kEuInvalidVisibilityHandle || format == kEuInvalidMeterFormat)
    {
        if (!meterFallbackLogged_.exchange(true))
        {
            FB_TRACE("METER_FALLBACK track=%d visible=%d handle=%u format=%u",
                channelOrder_.load(), visible ? 1 : 0, static_cast<unsigned>(handle),
                static_cast<unsigned>(format));
        }
        // The legacy write is an isolated compatibility fallback while the
        // missing visibility callback is being investigated. It is never sent
        // alongside a valid 3.1 batched meter update.
        const auto masterLevel = valuesDb.empty() ? -120.0F :
            *std::max_element(valuesDb.begin(), valuesDb.end());
        EuPrimitiveControl* primitive = nullptr;
        if (meter_.GetPrimitive(EuControlMultiMeter::kID_Meter0, &primitive) == kERR_OK &&
            primitive)
        {
            primitive->SetCurrentValue(std::clamp(masterLevel, -120.0F, 0.0F));
            primitive->Refresh();
        }
        return;
    }

    const auto masterLevel = valuesDb.empty() ? -120.0F :
        *std::max_element(valuesDb.begin(), valuesDb.end());
    const auto masterClip = masterLevel >= -0.01F;
    auto combinedResult = writer.SetPerMeterValuesV2(meter_, handle, format,
        masterLevel, masterClip);
    const auto legCount = EuMeterFormatNumLegs(format);
    for (NEuCon::uint32 leg = 0U; leg < legCount; ++leg)
    {
        float level = -120.0F;
        if (legCount == 1U)
        {
            level = masterLevel;
        }
        else if (leg < valuesDb.size())
        {
            level = valuesDb[leg];
        }
        level = std::clamp(level, -120.0F, 0.0F);
        const auto legResult = writer.SetPerLegValuesV2(meter_, handle, format, leg,
            level, level, kMeter_DefaultValueForContext, level >= -0.01F);
        if (combinedResult == kEUBMR_OK && legResult != kEUBMR_OK)
        {
            combinedResult = legResult;
        }
    }
    if (lastMeterResult_.exchange(static_cast<int>(combinedResult)) !=
        static_cast<int>(combinedResult))
    {
        FB_TRACE("METER_BATCH track=%d result=%d legs=%u handle=%u format=%u",
            channelOrder_.load(), static_cast<int>(combinedResult),
            static_cast<unsigned>(legCount), static_cast<unsigned>(handle),
            static_cast<unsigned>(format));
    }
}

void EuconChannel::OnProcessorCallback(const tEVT eventType, NEuCon::uint32, void* data)
{
    if (eventType != kEVT_AttributeChange || !data) return;
    const auto& change = *static_cast<const AttributeChangeData*>(data);
    if (change.mAttributeKeyType != kATRIB_KEYTYPE_Int ||
        change.mAttributeValueType != kATRIB_VALUETYPE_Int ||
        change.mIntAttributeKey != kATRIBID_SurfaceIsVisible) return;
    const bool visible = change.mIntAttributeValue != 0;
    const bool previous = surfaceVisible_.exchange(visible);
    if (visible && !previous) feedbackRefreshRequested_.store(true);
    FB_TRACE("CHANNEL_VIS track=%d visible=%d previous=%d",
        channelOrder_.load(), visible ? 1 : 0, previous ? 1 : 0);
}

void EuconChannel::OnPrimitiveCallback(const tEVT eventType, NEuCon::uint32 eventFlags,
    const NEuCon::uint32 controlId, const NEuCon::uint32 arrayMemberControlId,
    const NEuCon::uint32 primitiveId, EuPrimitiveControl* affectedPrimitive,
    const NEuCon::uint16 newValueIndex, void*)
{
    if (eventType != kEVT_PRIM_StateChange || !affectedPrimitive ||
        (eventFlags & kPRIMITIVE_FORCE_UPDATE))
    {
        return;
    }

    if (controlId == FaderId && primitiveId == EuControlFader::kID_Slider)
    {
        NEuCon::float32 value = 0.0F;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        const auto now = MonotonicMilliseconds();
        const auto humanMove = faderTouched_.load(std::memory_order_acquire) ||
            now <= faderTouchReleaseDeadline_.load(std::memory_order_acquire);
        FB_TRACE("FADER_EVT track=%d index=%u raw=%.2f touch=%d human=%d", channelOrder_.load(),
            static_cast<unsigned>(newValueIndex), value,
            faderTouched_.load(std::memory_order_acquire) ? 1 : 0, humanMove ? 1 : 0);
        if (humanMove)
        {
            faderHandler_(CoordinateToNormalized(value), newValueIndex, value);
        }
        if (value > kMaxDb)
        {
            // EUCON invokes this callback on its own thread. Defer motor output
            // to the host update thread and keep SDK output out of this callback.
            faderReboundPending_.store(true);
        }
    }
    else if (controlId == FaderId && primitiveId == EuControlFader::kID_SliderTouchSense)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        const auto touched = value != 0;
        FB_TRACE("FADER_TOUCH track=%d state=%d index=%u", channelOrder_.load(),
            touched ? 1 : 0, static_cast<unsigned>(newValueIndex));
        faderTouched_.store(touched, std::memory_order_release);
        faderTouchReleaseDeadline_.store(
            touched ? 0ULL : MonotonicMilliseconds() + 75ULL, std::memory_order_release);
    }
    else if (controlId == FaderId && primitiveId == EuControlFader::kID_Mute)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        EuPrimitiveControl* led = nullptr;
        if (fader_.GetPrimitive(EuControlFader::kID_MuteLed, &led) == kERR_OK && led)
        {
            led->SetCurrentIndex(static_cast<NEuCon::uint16>(
                value == 0 ? kLEDStatus_Off : kLEDStatus_On));
            led->Refresh();
        }
        muteHandler_(static_cast<float>(value), newValueIndex,
            static_cast<float>(value));
    }
    else if (controlId == RecordArmId && recordArmHandler_)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("RECORD_ARM_EVT track=%d state=%d index=%u", channelOrder_.load(), value,
            static_cast<unsigned>(newValueIndex));
        recordArmHandler_(static_cast<float>(value), newValueIndex,
            static_cast<float>(value));
    }
    else if (controlId == SoloId && primitiveId == EuControlSwitch::kID_Switch && soloHandler_)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("SOLO_EVT track=%d state=%d index=%u", channelOrder_.load(), value,
            static_cast<unsigned>(newValueIndex));
        soloHandler_(static_cast<float>(value), newValueIndex,
            static_cast<float>(value));
    }
    else if (controlId == SelectId && primitiveId == EuControlSwitch::kID_Switch &&
        selectHandler_)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("SELECT_EVT track=%d state=%d index=%u", channelOrder_.load(), value,
            static_cast<unsigned>(newValueIndex));
        selectHandler_(static_cast<float>(value), newValueIndex,
            static_cast<float>(value));
    }
    else if (controlId == KnobSetId && arrayMemberControlId == knobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_Knob)
    {
        NEuCon::float32 value = 0.0F;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("KNOB_EVT track=%d index=%u raw=%.2f", channelOrder_.load(),
            static_cast<unsigned>(newValueIndex), value);
        knobHandler_(CoordinateToNormalized(value), newValueIndex, value);
    }
    else if (controlId == KnobSetId && arrayMemberControlId == knobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_KnobTouchSense)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("KNOB_TOUCH track=%d state=%d index=%u", channelOrder_.load(),
            value != 0 ? 1 : 0, static_cast<unsigned>(newValueIndex));
    }
    else if (controlId == KnobSetId && arrayMemberControlId == knobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_KnobTopSwitch && appActionHandler_)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        if (value != 0)
        {
            appActionHandler_(AppAction::ResetVolume, 1.0F, newValueIndex,
                static_cast<float>(value));
        }
    }
    else if (controlId == KnobSetId && primitiveId == EuControlKnobCell::kID_LowerSwitch &&
        appActionHandler_)
    {
        const auto found = std::find(quickActionMemberIds_.begin(),
            quickActionMemberIds_.end(), arrayMemberControlId);
        if (found != quickActionMemberIds_.end())
        {
            NEuCon::int32 value = 0;
            affectedPrimitive->GetValueAt(newValueIndex, value);
            if (value != 0)
            {
                static constexpr std::array<AppAction, 5> actions = {{
                    AppAction::ResetPan, AppAction::DefaultOutput,
                    AppAction::DefaultInput, AppAction::Unmute,
                    AppAction::ClearSolo,
                }};
                const auto index = static_cast<std::size_t>(
                    std::distance(quickActionMemberIds_.begin(), found));
                appActionHandler_(actions[index], 1.0F, newValueIndex,
                    static_cast<float>(value));
            }
        }
    }
    else if (controlId == PanKnobSetId && arrayMemberControlId == panKnobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_Knob && panHandler_)
    {
        NEuCon::float32 value = 0.0F;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("PAN_EVT track=%d index=%u raw=%.2f", channelOrder_.load(),
            static_cast<unsigned>(newValueIndex), value);
        panHandler_(std::clamp(value / 100.0F, -1.0F, 1.0F), newValueIndex, value);
    }
    else if (controlId == PanKnobSetId && arrayMemberControlId == panKnobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_KnobTouchSense)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("PAN_TOUCH track=%d state=%d index=%u", channelOrder_.load(),
            value != 0 ? 1 : 0, static_cast<unsigned>(newValueIndex));
    }
    else if (controlId == PanKnobSetId && arrayMemberControlId == panKnobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_KnobTopSwitch && panResetHandler_)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("PAN_RESET_EVT track=%d state=%d index=%u", channelOrder_.load(), value,
            static_cast<unsigned>(newValueIndex));
        if (value != 0)
        {
            panResetHandler_(0.0F, newValueIndex, 0.0F);
        }
    }
    else if (controlId == WindowKnobSetId &&
        primitiveId == EuControlKnobCell::kID_LowerSwitch && appActionHandler_)
    {
        const auto found = std::find(windowMemberIds_.begin(), windowMemberIds_.end(),
            arrayMemberControlId);
        if (found != windowMemberIds_.end())
        {
            NEuCon::int32 value = 0;
            affectedPrimitive->GetValueAt(newValueIndex, value);
            const auto index = static_cast<std::size_t>(
                std::distance(windowMemberIds_.begin(), found));
            static constexpr std::array<AppAction, 4> actions = {{
                AppAction::WindowFocus, AppAction::WindowMinimize,
                AppAction::WindowMaximize, AppAction::WindowTopmost,
            }};
            if (index >= 2U || value != 0)
            {
                appActionHandler_(actions[index], static_cast<float>(value),
                    newValueIndex, static_cast<float>(value));
            }
        }
    }
    else if (controlId == MediaKnobSetId && appActionHandler_)
    {
        MediaCellKind kind{};
        bool foundMember = false;
        {
            const std::scoped_lock lock(mediaMutex_);
            const auto found = std::find(mediaMemberIds_.begin(), mediaMemberIds_.end(),
                arrayMemberControlId);
            if (found != mediaMemberIds_.end())
            {
                kind = mediaKinds_[static_cast<std::size_t>(
                    std::distance(mediaMemberIds_.begin(), found))];
                foundMember = true;
            }
        }
        if (!foundMember || kind == MediaCellKind::Title || kind == MediaCellKind::Artist ||
            kind == MediaCellKind::Position)
        {
            return;
        }
        AppAction action{};
        switch (kind)
        {
        case MediaCellKind::PlayPause: action = AppAction::MediaPlayPause; break;
        case MediaCellKind::Previous: action = AppAction::MediaPrevious; break;
        case MediaCellKind::Next: action = AppAction::MediaNext; break;
        case MediaCellKind::Stop: action = AppAction::MediaStop; break;
        case MediaCellKind::Position: return;
        case MediaCellKind::Seek: action = AppAction::MediaSeek; break;
        case MediaCellKind::Shuffle: action = AppAction::MediaShuffle; break;
        case MediaCellKind::Repeat: action = AppAction::MediaRepeat; break;
        default: return;
        }
        if (kind == MediaCellKind::Seek && primitiveId == EuControlKnobCell::kID_Knob)
        {
            NEuCon::float32 value = 0.0F;
            affectedPrimitive->GetValueAt(newValueIndex, value);
            appActionHandler_(action, std::clamp(value / 100.0F, 0.0F, 1.0F),
                newValueIndex, value);
        }
        else if (kind != MediaCellKind::Seek &&
            primitiveId == EuControlKnobCell::kID_LowerSwitch)
        {
            NEuCon::int32 value = 0;
            affectedPrimitive->GetValueAt(newValueIndex, value);
            const auto momentary = kind == MediaCellKind::Previous ||
                kind == MediaCellKind::Next || kind == MediaCellKind::Stop;
            if (!momentary || value != 0)
            {
                appActionHandler_(action, static_cast<float>(value), newValueIndex,
                    static_cast<float>(value));
            }
        }
    }
    else if ((controlId == OutputRouteKnobSetId || controlId == InputRouteKnobSetId) &&
        primitiveId == EuControlKnobCell::kID_LowerSwitch)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        if (value == 0)
        {
            return;
        }
        const auto output = controlId == OutputRouteKnobSetId;
        std::wstring endpointId;
        {
            const std::scoped_lock lock(routeMutex_);
            const auto& memberIds = output ? outputRouteMemberIds_ : inputRouteMemberIds_;
            const auto& options = output ? outputRouteOptions_ : inputRouteOptions_;
            const auto found = std::find(memberIds.begin(), memberIds.end(),
                arrayMemberControlId);
            if (found == memberIds.end())
            {
                return;
            }
            const auto index = static_cast<std::size_t>(std::distance(memberIds.begin(), found));
            if (index >= options.size())
            {
                return;
            }
            endpointId = options[index].id;
        }
        FB_TRACE("ROUTE_EVT track=%d flow=%s endpoint=%ls", channelOrder_.load(),
            output ? "render" : "capture", endpointId.empty() ? L"default" : endpointId.c_str());
        auto& handler = output ? outputRouteHandler_ : inputRouteHandler_;
        if (handler)
        {
            handler(endpointId);
        }
    }
}
