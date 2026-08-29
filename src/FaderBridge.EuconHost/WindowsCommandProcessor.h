#pragma once

#include "EuControlSwitch.h"
#include "EuControlSwitchArray.h"
#include "EuDefinitions.h"
#include "EuProcessor.h"

#include <functional>

class WindowsCommandProcessor final : public EuProcessor
{
public:
    using MonoToggleHandler = std::function<void()>;

    explicit WindowsCommandProcessor(MonoToggleHandler monoToggleHandler);
    ~WindowsCommandProcessor() override;

    void SetMonoAudioEnabled(bool enabled);

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

    MonoToggleHandler monoToggleHandler_;
    EuControlSwitchArray windowsAudioCommands_;
    EuControlSwitch monoAudio_;
    NEuCon::uint32 monoAudioMemberId_ = 0U;
    bool monoAudioEnabled_ = false;
};
