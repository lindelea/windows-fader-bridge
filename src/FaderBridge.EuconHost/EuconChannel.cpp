#include "EuconChannel.h"

#include "DiagnosticLog.h"
#include "EuBatchedMeters.h"
#include "EuBatchedMeterWriter.h"
#include "EuDefinitions.h"
#include "EuLayoutChannel.h"
#include "EuPrimitiveControl.h"
#include "EuPrimitiveKnob.h"
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
}

EuconChannel::EuconChannel(const int channelIndex, const std::wstring& persistenceId,
    const std::wstring& displayName, ChangeHandler faderHandler,
    ChangeHandler knobHandler, ChangeHandler muteHandler)
    : channelIndex_(channelIndex),
      faderHandler_(std::move(faderHandler)),
      knobHandler_(std::move(knobHandler)),
      muteHandler_(std::move(muteHandler)),
      fader_(this), name_(this), number_(this), meter_(this), knobSet_(this), knob_(this)
{
    SetAttribute(kATRIBID_ProcessorType, kProcType_ChannelStrip);
    SetAttribute(kATRIBID_LayoutRule0, kRUL_EuLayoutChannel);
    SetAttribute(kATRIBID_ChannelType, L"Audio");
    SetAttribute(kATRIBID_ChannelOrder, channelIndex + 1);
    SetPersistenceID(persistenceId);

    InitializeFader();
    InitializeText(name_, NameId, EuLayoutChannel::kNAM_Name, displayName);
    InitializeText(number_, NumberId, EuLayoutChannel::kNAM_ChannelNumber,
        std::to_wstring(channelIndex + 1));
    InitializeMeter();
    InitializeKnob();
}

EuconChannel::~EuconChannel()
{
    knobSet_.Remove(knobMemberId_);
    RemoveControl(knobSet_);
    RemoveControl(meter_);
    RemoveControl(number_);
    RemoveControl(name_);
    RemoveControl(fader_);
}

void EuconChannel::InitializeFader()
{
    fader_.SetId(FaderId);
    fader_.SetAttribute(kATRIBID_LayoutName0, EuLayoutChannel::kNAM_Fader);

    EuPrimitiveControl* primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Slider, &primitive) == kERR_OK && primitive)
    {
        // Windows 100% maps to 0 dB. CH1 may already hold 0 dB in the
        // processor model while its physical fader is in the overtravel zone.
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
            // It must be initialized explicitly or the S3's touch callbacks are
            // not dependable.
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
    EuPrimitiveControl* primitive = nullptr;
    if (meter_.GetPrimitive(EuControlMultiMeter::kID_Meter0, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Float, 101U);
        primitive->LoadValueTableInterpolated(-120.0F, 0.0F);
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
        FB_TRACE("MOTOR_CMD ch=%d value=%.4f index=%u", channelIndex_ + 1,
            value, static_cast<unsigned>(index));
        primitive->SetCurrentIndex(index);
        // CH1 can retain the motor value until another surface event (for
        // example Sel) flushes the transaction. Force immediate delivery.
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
        FB_TRACE("KNOB_RING_CMD ch=%d value=%.4f index=%u", channelIndex_ + 1,
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

void EuconChannel::PostRegisterMeterInitialization()
{
    meter_.SetAttribute2(kATRIBID_MeterType, kMeterType__SamplePeak, true);
    SetAttribute2(kATRIBID_MeterType, kMeterType__SamplePeak, true);
    SetAttribute2(kATRIBID_TrackFormat, kFORMAT_Stereo, true);

    const auto flags = static_cast<NEuCon::uint32>(
        kEuMFMT_PL_Level | kEuMFMT_PL_Peak | kEuMFMT_PL_Clip |
        kEuMFMT_PM_MasterPeak | kEuMFMT_PM_MasterClip);
    meter_.SetFormat(EuMakeMeterFormat(2U, flags), { kMTR_Left, kMTR_Right });
}

void EuconChannel::SetMeterVisibility(const bool visible, const tVisibilityHandle handle,
    const tEuMeterFormat format)
{
    const std::scoped_lock lock(meterMutex_);
    meterVisible_ = visible;
    meterVisibilityHandle_ = handle;
    meterFormat_ = format;
}

void EuconChannel::WriteMeterDb(EuBatchedMeterWriter& writer, const float valueDb, const bool clip)
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

    // EuConApp normally obtains these through VisibilityChangedV2. Recover once
    // from the meter object if the initial callback raced application startup.
    if (!visible || handle == kEuInvalidVisibilityHandle || format == kEuInvalidMeterFormat)
    {
        std::vector<NEuCon::uint32> roles;
        if (meter_.GetVisibilityHandle(handle) != kERR_OK ||
            meter_.GetFormat(format, roles) != kERR_OK ||
            handle == kEuInvalidVisibilityHandle || format == kEuInvalidMeterFormat)
        {
            return;
        }
        SetMeterVisibility(true, handle, format);
    }

    const auto level = std::clamp(valueDb, -120.0F, 12.0F);
    const auto peak = level + 2.0F;
    writer.SetPerMeterValuesV2(meter_, handle, format, peak, clip);
    writer.SetPerLegValuesV2(meter_, handle, format, 0U,
        level, peak, kMeter_DefaultValueForContext, clip);
    writer.SetPerLegValuesV2(meter_, handle, format, 1U,
        level, peak, kMeter_DefaultValueForContext, clip);
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
        FB_TRACE("FADER_EVT ch=%d index=%u raw=%.2f touch=%d human=%d", channelIndex_ + 1,
            static_cast<unsigned>(newValueIndex), value,
            faderTouched_.load(std::memory_order_acquire) ? 1 : 0, humanMove ? 1 : 0);
        if (humanMove)
        {
            faderHandler_(channelIndex_, CoordinateToNormalized(value), newValueIndex, value);
        }
        if (value > kMaxDb)
        {
            // EUCON invokes this callback on its own thread. Defer motor output
            // to the host update thread; CH1 can ignore a self-write made here.
            faderReboundPending_.store(true);
        }
    }
    else if (controlId == FaderId && primitiveId == EuControlFader::kID_SliderTouchSense)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        const auto touched = value != 0;
        FB_TRACE("FADER_TOUCH ch=%d state=%d index=%u", channelIndex_ + 1,
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
        muteHandler_(channelIndex_, static_cast<float>(value), newValueIndex,
            static_cast<float>(value));
    }
    else if (controlId == KnobSetId && arrayMemberControlId == knobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_Knob)
    {
        NEuCon::float32 value = 0.0F;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("KNOB_EVT ch=%d index=%u raw=%.2f", channelIndex_ + 1,
            static_cast<unsigned>(newValueIndex), value);
        knobHandler_(channelIndex_, CoordinateToNormalized(value), newValueIndex, value);
    }
    else if (controlId == KnobSetId && arrayMemberControlId == knobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_KnobTouchSense)
    {
        NEuCon::int32 value = 0;
        affectedPrimitive->GetValueAt(newValueIndex, value);
        FB_TRACE("KNOB_TOUCH ch=%d state=%d index=%u", channelIndex_ + 1,
            value != 0 ? 1 : 0, static_cast<unsigned>(newValueIndex));
    }
}
