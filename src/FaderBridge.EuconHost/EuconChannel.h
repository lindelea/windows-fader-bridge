#pragma once

#include "EuControlFader.h"
#include "EuControlKnobCell.h"
#include "EuControlKnobCellArray.h"
#include "EuControlMultiMeter.h"
#include "EuControlTextDisplay.h"
#include "EuProcessor.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>

class EuBatchedMeterWriter;

class EuconChannel final : public EuProcessor
{
public:
    using ChangeHandler = std::function<void(float value,
        NEuCon::uint16 rawIndex, float rawTableValue)>;

    EuconChannel(int channelOrder, NEuCon::int32 channelColor,
                 const std::wstring& persistenceId,
                 const std::wstring& displayName, ChangeHandler faderHandler,
                 ChangeHandler knobHandler, ChangeHandler muteHandler);
    ~EuconChannel() override;

    void SetFaderNormalized(float value);
    void SetKnobNormalized(float value);
    void SetName(const std::wstring& value);
    void SetOrder(int channelOrder);
    void ApplyPendingFaderRebound();
    void SetMuted(bool muted);
    void PostRegisterMeterInitialization();
    void SetMeterVisibility(bool visible, tVisibilityHandle handle, tEuMeterFormat format);
    void WriteMeterDb(EuBatchedMeterWriter& writer, float valueDb, bool clip);

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

    std::atomic<int> channelOrder_;
    ChangeHandler faderHandler_;
    ChangeHandler knobHandler_;
    ChangeHandler muteHandler_;

    EuControlFader fader_;
    EuControlTextDisplay name_;
    EuControlTextDisplay number_;
    EuControlMultiMeter meter_;
    EuControlKnobCellArray knobSet_;
    EuControlKnobCell knob_;
    NEuCon::uint32 knobMemberId_ = 0;
    std::atomic_bool faderReboundPending_ = false;
    std::atomic_bool faderTouched_ = false;
    std::atomic<unsigned long long> faderTouchReleaseDeadline_ = 0;
    std::atomic<int> lastMeterResult_ = -1;
    std::atomic_bool meterFallbackLogged_ = false;
    std::mutex meterMutex_;
    bool meterVisible_ = false;
    tVisibilityHandle meterVisibilityHandle_ = kEuInvalidVisibilityHandle;
    tEuMeterFormat meterFormat_ = kEuInvalidMeterFormat;
};
