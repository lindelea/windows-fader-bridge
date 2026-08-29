#pragma once

#include "EuControlSwitch.h"
#include "EuControlSwitchArray.h"
#include "EuDefinitions.h"
#include "EuProcessor.h"

#include <functional>

class WindowsCommandProcessor final : public EuProcessor
{
public:
    using CommandHandler = std::function<void()>;

    WindowsCommandProcessor(CommandHandler monoToggleHandler,
        CommandHandler clearSoloHandler);
    ~WindowsCommandProcessor() override;

    void SetMonoAudioEnabled(bool enabled);
    void SetSoloActive(bool active);

    void OnPrimitiveCallback(tEVT eventType,
        NEuCon::uint32 eventFlags,
        NEuCon::uint32 controlId,
        NEuCon::uint32 arrayMemberControlId,
        NEuCon::uint32 primitiveId,
        EuPrimitiveControl* affectedPrimitive,
        NEuCon::uint16 newValueIndex,
        void* callbackEventData = nullptr) override;

private:
    static constexpr NEuCon::uint32 WindowsAudioContainerId = 1U;

    CommandHandler monoToggleHandler_;
    CommandHandler clearSoloHandler_;
    EuControlSwitchArray windowsAudioCommands_;
    EuControlSwitch monoAudio_;
    EuControlSwitch clearSolo_;
    NEuCon::uint32 monoAudioMemberId_ = 0U;
    NEuCon::uint32 clearSoloMemberId_ = 0U;
    bool monoAudioEnabled_ = false;
    bool soloActive_ = false;
};
