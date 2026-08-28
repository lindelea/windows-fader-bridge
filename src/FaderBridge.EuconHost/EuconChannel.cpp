#include "EuconChannel.h"

#include "EuDefinitions.h"
#include "EuBatchedMeterWriter.h"
#include "EuLayoutChannel.h"
#include "EuPrimitiveControl.h"
#include "EuPrimitiveKnob.h"
#include "EuPrimitiveMeter.h"
#include "EuPrimitiveSwitch.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr float kMinFaderDb = -96.0F;
constexpr float kMaxFaderDb = 12.0F;
constexpr NEuCon::uint16 kFaderStepCount = 1024U;
constexpr NEuCon::uint16 kKnobStepCount = 193U;
constexpr NEuCon::uint16 kFaderUnityIndex = static_cast<NEuCon::uint16>(
    ((0.0F - kMinFaderDb) / (kMaxFaderDb - kMinFaderDb)) * (kFaderStepCount - 1U) + 0.5F);
}

EuconChannel::EuconChannel(const int channelIndex, const std::wstring& displayName,
    ChangeHandler faderHandler, ChangeHandler knobHandler, ChangeHandler muteHandler)
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
    SetPersistenceID(L"FaderBridge.SessionSlot." + std::to_wstring(channelIndex + 1));

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
        primitive->Initialize(kTYP_Float, kFaderStepCount);
        primitive->LoadValueTableInterpolated(kMinFaderDb, kMaxFaderDb);
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
    meter_.SetAttribute(kATRIBID_NumberOfMetersInChannel, 1U);
    meter_.SetAttribute(kATRIBID_MeterType, kMeterType__SamplePeak);
    meter_.SetMasterMeterType(kMETERTYPE_SignalLevel);
    EuPrimitiveControl* primitive = nullptr;
    if (meter_.GetPrimitive(EuControlMultiMeter::kID_Meter0, &primitive) == kERR_OK && primitive)
    {
        primitive->Initialize(kTYP_Float, 121U);
        primitive->LoadValueTableInterpolated(-120.0F, 0.0F);
        if (auto* meterPrimitive = dynamic_cast<EuPrimitiveMeter*>(primitive))
        {
            meterPrimitive->SetMeterType(kMETERTYPE_SignalLevel);
            meterPrimitive->SetRole(kMTR_Mono);
            meterPrimitive->SetMeterPositionMode(kMeterThermometerUp);
        }
    }
    meterFormat_ = EuMakeMeterFormat(1U,
        kEuMFMT_PL_Level | kEuMFMT_PL_Peak | kEuMFMT_PL_RMS | kEuMFMT_PL_Clip);
    meter_.SetFormat(meterFormat_, {kMTR_Mono});
    AddControl(meter_);
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
        primitive->Initialize(kTYP_Float, kKnobStepCount);
        primitive->LoadValueTableInterpolated(kMinFaderDb, kMaxFaderDb);
        if (auto* rotary = dynamic_cast<EuPrimitiveKnob*>(primitive))
        {
            rotary->SetPositionRingMode(kRingPoint);
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

void EuconChannel::SetFaderPosition(const float normalizedPosition)
{
    EuPrimitiveControl* primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Slider, &primitive) == kERR_OK && primitive)
    {
        const auto index = static_cast<NEuCon::uint16>(std::lround(
            std::clamp(normalizedPosition, 0.0F, 1.0F) * kFaderUnityIndex));
        primitive->SetCurrentIndex(index);
    }
}

void EuconChannel::SetKnobPosition(const float normalizedPosition)
{
    EuPrimitiveControl* primitive = nullptr;
    if (knob_.GetPrimitive(EuControlKnobCell::kID_Knob, &primitive) == kERR_OK && primitive)
    {
        const auto index = static_cast<NEuCon::uint16>(std::lround(
            std::clamp(normalizedPosition, 0.0F, 1.0F) * (kKnobStepCount - 1U)));
        primitive->SetCurrentIndex(index);
    }
}

void EuconChannel::SetMeterDb(const float valueDb)
{
    EuPrimitiveControl* primitive = nullptr;
    if (meter_.GetPrimitive(EuControlMultiMeter::kID_Meter0, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentValue(std::clamp(valueDb, -120.0F, 0.0F));
    }
}

void EuconChannel::WriteMeterDb(EuBatchedMeterWriter& writer, const float valueDb)
{
    const auto levelDb = std::clamp(valueDb, -120.0F, 0.0F);
    tVisibilityHandle visibilityHandle = kEuInvalidVisibilityHandle;
    if (meter_.GetVisibilityHandle(visibilityHandle) == kERR_OK &&
        visibilityHandle != kEuInvalidVisibilityHandle)
    {
        writer.SetPerLegValuesV2(meter_, visibilityHandle, meterFormat_, 0U,
            levelDb, levelDb, levelDb, levelDb >= -0.01F);
    }

    // Compatibility fallback for surfaces negotiating the legacy 2.0 meter API.
    SetMeterDb(levelDb);
}

void EuconChannel::SetMute(const bool value)
{
    EuPrimitiveControl* primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_Mute, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(value ? 1U : 0U));
    }
    primitive = nullptr;
    if (fader_.GetPrimitive(EuControlFader::kID_MuteLed, &primitive) == kERR_OK && primitive)
    {
        primitive->SetCurrentIndex(static_cast<NEuCon::uint16>(
            value ? kLEDStatus_On : kLEDStatus_Off));
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
        const auto normalizedPosition = std::clamp(
            static_cast<float>(newValueIndex) / static_cast<float>(kFaderUnityIndex),
            0.0F, 1.0F);
        faderHandler_(channelIndex_, normalizedPosition);
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
        }
        muteHandler_(channelIndex_, static_cast<float>(value));
    }
    else if (controlId == KnobSetId && arrayMemberControlId == knobMemberId_ &&
        primitiveId == EuControlKnobCell::kID_Knob)
    {
        const auto normalizedPosition = static_cast<float>(newValueIndex) /
            static_cast<float>(kKnobStepCount - 1U);
        knobHandler_(channelIndex_, normalizedPosition);
    }
}
