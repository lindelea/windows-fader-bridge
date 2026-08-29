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
}

EuconChannel::EuconChannel(const int channelOrder, const NEuCon::int32 channelColor,
    const std::wstring& persistenceId,
    const std::wstring& displayName, ChangeHandler faderHandler,
    ChangeHandler knobHandler, ChangeHandler muteHandler,
    ChangeHandler soloHandler, ChangeHandler selectHandler,
    ChangeHandler recordArmHandler,
    const NEuCon::int32 trackType,
    const std::wstring& channelType)
    : channelOrder_(channelOrder),
      faderHandler_(std::move(faderHandler)),
      knobHandler_(std::move(knobHandler)),
      muteHandler_(std::move(muteHandler)),
      soloHandler_(std::move(soloHandler)),
      selectHandler_(std::move(selectHandler)),
      recordArmHandler_(std::move(recordArmHandler)),
      fader_(this), name_(this), number_(this), meter_(this), knobSet_(this), knob_(this)
{
    SetAttribute(kATRIBID_ProcessorType, kProcType_ChannelStrip);
    SetAttribute(kATRIBID_LayoutRule0, kRUL_EuLayoutChannel);
    SetAttribute(kATRIBID_TrackType, trackType == 0 ? kTRACK_Audio : trackType);
    SetAttribute(kATRIBID_ChannelType, channelType);
    SetAttribute(kATRIBID_ChannelOrder, channelOrder);
    // Getting Started with EuCon 12.9: surface channel color is standard
    // 0x00RRGGBB track metadata. The surface decides how to render it.
    SetAttribute2(kATRIBID_ChannelColor, channelColor, false);
    SetPersistenceID(persistenceId);

    InitializeFader();
    InitializeText(name_, NameId, EuLayoutChannel::kNAM_Name, displayName);
    InitializeText(number_, NumberId, EuLayoutChannel::kNAM_ChannelNumber,
        std::to_wstring(channelOrder));
    InitializeMeter();
    InitializeKnob();
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
}

EuconChannel::~EuconChannel()
{
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
    // Getting Started with EuCon 12.5 and the current EuConApp pattern:
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
    knobSet_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Input);
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
    if (knob_.GetPrimitive(EuControlKnobCell::kID_KnobLabelDisplay, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_IndexedString, 1U);
        primitive->LoadValueAt(0U, tEuString(L"Vol"), tEuString(L"Volume"), tEuString(L"Session Volume"));
    }
    knobSet_.PushBack(&knob_, knobMemberId_);
}

void EuconChannel::SetFaderNormalized(const float value)
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
        primitive->SetCurrentIndex(index);
        // Ensure an unchanged target is delivered after an overtravel callback.
        primitive->Refresh();
    }
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

void EuconChannel::OnPrimitiveCallback(const tEVT eventType, NEuCon::uint32,
    const NEuCon::uint32 controlId, const NEuCon::uint32 arrayMemberControlId,
    const NEuCon::uint32 primitiveId, EuPrimitiveControl* affectedPrimitive,
    const NEuCon::uint16 newValueIndex, void*)
{
    if (eventType != kEVT_PRIM_StateChange || !affectedPrimitive)
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
}
