#pragma once

#include "EuControlFader.h"
#include "EuControlKnobCell.h"
#include "EuControlKnobCellArray.h"
#include "EuControlMultiMeter.h"
#include "EuControlTextDisplay.h"
#include "EuProcessor.h"

#include <functional>
#include <string>

class EuconChannel final : public EuProcessor
{
public:
    using ChangeHandler = std::function<void(int channelIndex, float value)>;

    EuconChannel(int channelIndex, const std::wstring& displayName, ChangeHandler faderHandler,
                 ChangeHandler knobHandler, ChangeHandler muteHandler);
    ~EuconChannel() override;

    void SetFaderPosition(float normalizedPosition);
    void SetKnobPosition(float normalizedPosition);
    void SetMeterDb(float valueDb);
    void WriteMeterDb(class EuBatchedMeterWriter& writer, float valueDb);
    void SetMute(bool value);
    void SetName(const std::wstring& value);

    void OnPrimitiveCallback(tEVT eventType, NEuCon::uint32 eventFlags,
        NEuCon::uint32 controlId, NEuCon::uint32 arrayMemberControlId,
        NEuCon::uint32 primitiveId, EuPrimitiveControl* affectedPrimitive,
        NEuCon::uint16 newValueIndex, void* callbackEventData = nullptr) override;

private:
    enum ControlId : NEuCon::uint32
    {
        FaderId = 1,
        NameId,
        NumberId,
        MeterId,
        KnobSetId,
    };

    void InitializeFader();
    void InitializeText(EuControlTextDisplay& display, NEuCon::uint32 id,
        NEuCon::int32 layoutName, const std::wstring& text);
    void InitializeMeter();
    void InitializeKnob();

    int channelIndex_;
    ChangeHandler faderHandler_;
    ChangeHandler knobHandler_;
    ChangeHandler muteHandler_;

    EuControlFader fader_;
    EuControlTextDisplay name_;
    EuControlTextDisplay number_;
    EuControlMultiMeter meter_;
    EuControlKnobCellArray knobSet_;
    EuControlKnobCell knob_;
    tEuMeterFormat meterFormat_ = kEuInvalidMeterFormat;
    NEuCon::uint32 knobMemberId_ = 0;
};
