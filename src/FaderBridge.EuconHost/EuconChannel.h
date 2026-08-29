#pragma once

#include "EuControlFader.h"
#include "EuControlKnobCell.h"
#include "EuControlKnobCellArray.h"
#include "EuControlMultiMeter.h"
#include "EuControlSwitch.h"
#include "EuControlTextDisplay.h"
#include "EuProcessor.h"

#include <atomic>
#include <array>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

class EuBatchedMeterWriter;

class EuconChannel final : public EuProcessor
{
public:
    using ChangeHandler = std::function<void(float value,
        NEuCon::uint16 rawIndex, float rawTableValue)>;
    using RouteHandler = std::function<void(const std::wstring& endpointId)>;
    enum class AppAction
    {
        ResetVolume,
        ResetPan,
        DefaultOutput,
        DefaultInput,
        Unmute,
        ClearSolo,
        WindowFocus,
        WindowMinimize,
        WindowMaximize,
        WindowTopmost,
        MediaPlayPause,
        MediaPrevious,
        MediaNext,
        MediaStop,
        MediaSeek,
        MediaShuffle,
        MediaRepeat,
    };
    using AppActionHandler = std::function<void(AppAction action, float value,
        NEuCon::uint16 rawIndex, float rawTableValue)>;

    struct RouteOption
    {
        std::wstring id;
        std::wstring name;
        NEuCon::int32 color = 0x00FFFFFF;
    };

    EuconChannel(int channelOrder, NEuCon::int32 channelColor,
                 const std::wstring& persistenceId,
                 const std::wstring& displayName, ChangeHandler faderHandler,
                 ChangeHandler knobHandler, ChangeHandler panHandler,
                 ChangeHandler panResetHandler,
                 ChangeHandler muteHandler,
                 ChangeHandler soloHandler = {}, ChangeHandler selectHandler = {},
                 ChangeHandler recordArmHandler = {},
                 RouteHandler outputRouteHandler = {}, RouteHandler inputRouteHandler = {},
                 AppActionHandler appActionHandler = {},
                 NEuCon::int32 trackType = 0,
                 const std::wstring& channelType = L"Audio");
    ~EuconChannel() override;

    void SetFaderNormalized(float value);
    void SetKnobNormalized(float value);
    void SetPan(float value);
    void SetName(const std::wstring& value);
    void SetOrder(int channelOrder);
    void ApplyPendingFaderRebound();
    void SetMuted(bool muted);
    void SetSoloed(bool soloed);
    void SetSelected(bool selected);
    void SetRecordArmed(bool armed);
    void SetRouteOptions(const std::vector<RouteOption>& outputOptions,
        const std::wstring& selectedOutputId,
        const std::vector<RouteOption>& inputOptions,
        const std::wstring& selectedInputId);
    void SetTrackMetadata(NEuCon::int32 trackType, const std::wstring& channelType);
    void SetWindowState(bool available, bool foreground, bool minimized,
        bool maximized, bool topmost);
    void SetMediaState(bool available, bool playing, bool canPlayPause,
        bool canPrevious, bool canNext, bool canStop, bool hasPosition, bool canSeek,
        bool canShuffle, bool shuffle, bool canRepeat, int repeatMode,
        float position, const std::wstring& title, const std::wstring& artist);
    void PostRegisterMeterInitialization(bool forceMono,
        const std::vector<NEuCon::uint32>& roles);
    void ConfigureMeter(bool forceMono, const std::vector<NEuCon::uint32>& roles);
    void SetMeterVisibility(bool visible, tVisibilityHandle handle, tEuMeterFormat format);
    void WriteMeterDb(EuBatchedMeterWriter& writer,
        const std::vector<float>& valuesDb);

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
        PanKnobSetId,
        SoloId,
        SelectId,
        RecordArmId,
        TopLevelKnobSetId,
        OutputRouteKnobSetId,
        InputRouteKnobSetId,
        WindowKnobSetId,
        MediaKnobSetId,
    };

    void InitializeFader();
    void InitializeText(EuControlTextDisplay& display, NEuCon::uint32 id,
        NEuCon::int32 layoutName, const std::wstring& text);
    void InitializeMeter();
    void InitializeKnob();
    void InitializePan();
    void InitializeSolo();
    void InitializeSelect();
    void InitializeRecordArm();
    void InitializeRouteKnobSets();
    void InitializeTopLevelKnobSet();
    void InitializeApplicationKnobSets();
    void RebuildRouteKnobSet(EuControlKnobCellArray& knobSet,
        std::vector<std::unique_ptr<EuControlKnobCell>>& cells,
        std::vector<NEuCon::uint32>& memberIds,
        const std::vector<RouteOption>& options, const std::wstring& selectedId,
        bool output);
    void UpdateRouteSelection(bool output, const std::wstring& selectedId);
    enum class MediaCellKind
    {
        Title,
        Artist,
        PlayPause,
        Previous,
        Next,
        Stop,
        Position,
        Seek,
        Shuffle,
        Repeat,
    };
    void RebuildMediaKnobSet(const std::vector<MediaCellKind>& desiredKinds,
        const std::wstring& title, const std::wstring& artist);
    void UpdateMediaLabel(MediaCellKind kind, const std::wstring& text);

    std::atomic<int> channelOrder_;
    ChangeHandler faderHandler_;
    ChangeHandler knobHandler_;
    ChangeHandler panHandler_;
    ChangeHandler panResetHandler_;
    ChangeHandler muteHandler_;
    ChangeHandler soloHandler_;
    ChangeHandler selectHandler_;
    ChangeHandler recordArmHandler_;
    RouteHandler outputRouteHandler_;
    RouteHandler inputRouteHandler_;
    AppActionHandler appActionHandler_;

    EuControlFader fader_;
    EuControlTextDisplay name_;
    EuControlTextDisplay number_;
    EuControlMultiMeter meter_;
    EuControlKnobCellArray knobSet_;
    EuControlKnobCell knob_;
    EuControlKnobCellArray panKnobSet_;
    EuControlKnobCell panKnob_;
    EuControlKnobCellArray outputRouteKnobSet_;
    EuControlKnobCellArray inputRouteKnobSet_;
    EuControlKnobCellArray topLevelKnobSet_;
    EuControlKnobCellArray windowKnobSet_;
    EuControlKnobCellArray mediaKnobSet_;
    std::array<std::unique_ptr<EuControlKnobCell>, 16> topLevelKnobs_;
    std::array<NEuCon::uint32, 16> topLevelMemberIds_{};
    std::array<std::unique_ptr<EuControlKnobCell>, 5> quickActionCells_;
    std::array<NEuCon::uint32, 5> quickActionMemberIds_{};
    std::array<std::unique_ptr<EuControlKnobCell>, 4> windowCells_;
    std::array<NEuCon::uint32, 4> windowMemberIds_{};
    std::vector<std::unique_ptr<EuControlKnobCell>> mediaCells_;
    std::vector<NEuCon::uint32> mediaMemberIds_;
    std::vector<MediaCellKind> mediaKinds_;
    std::mutex mediaMutex_;
    std::vector<std::unique_ptr<EuControlKnobCell>> outputRouteCells_;
    std::vector<std::unique_ptr<EuControlKnobCell>> inputRouteCells_;
    std::vector<NEuCon::uint32> outputRouteMemberIds_;
    std::vector<NEuCon::uint32> inputRouteMemberIds_;
    std::vector<RouteOption> outputRouteOptions_;
    std::vector<RouteOption> inputRouteOptions_;
    std::wstring selectedOutputRouteId_;
    std::wstring selectedInputRouteId_;
    std::mutex routeMutex_;
    std::unique_ptr<EuControlSwitch> solo_;
    std::unique_ptr<EuControlSwitch> select_;
    std::unique_ptr<EuControlSwitch> recordArm_;
    NEuCon::uint32 knobMemberId_ = 0;
    NEuCon::uint32 panKnobMemberId_ = 0;
    std::atomic_bool faderReboundPending_ = false;
    std::atomic_bool faderTouched_ = false;
    std::atomic<unsigned long long> faderTouchReleaseDeadline_ = 0;
    std::atomic<int> lastMeterResult_ = -1;
    std::atomic_bool meterFallbackLogged_ = false;
    std::mutex meterMutex_;
    bool meterVisible_ = false;
    tVisibilityHandle meterVisibilityHandle_ = kEuInvalidVisibilityHandle;
    tEuMeterFormat meterFormat_ = kEuInvalidMeterFormat;
    std::vector<NEuCon::uint32> configuredMeterRoles_;
    bool meterConfigured_ = false;
};
