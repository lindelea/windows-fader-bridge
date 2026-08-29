#pragma once

#include "WindowsCommand.h"

#include "EuControlSwitch.h"
#include "EuControlSwitchArray.h"
#include "EuDefinitions.h"
#include "EuProcessor.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

class WindowsCommandProcessor final : public EuProcessor
{
public:
    using CommandHandler = std::function<void()>;
    using WindowsCommandHandler = std::function<void(WindowsCommand)>;

    WindowsCommandProcessor(CommandHandler monoToggleHandler,
        CommandHandler clearSoloHandler,
        WindowsCommandHandler windowsCommandHandler);
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

    struct CommandEntry
    {
        WindowsCommand command{};
        std::unique_ptr<EuControlSwitch> control;
        NEuCon::uint32 memberId = 0U;
    };

    struct CommandCategory
    {
        NEuCon::uint32 controlId = 0U;
        std::unique_ptr<EuControlSwitchArray> container;
        std::vector<CommandEntry> commands;
    };

    struct CommandDefinition
    {
        WindowsCommand command;
        const wchar_t* name;
        const wchar_t* persistenceToken;
    };

    void AddCategory(NEuCon::uint32 controlId, const wchar_t* name,
        const wchar_t* persistenceToken, const CommandDefinition* commands,
        std::size_t commandCount);

    CommandHandler monoToggleHandler_;
    CommandHandler clearSoloHandler_;
    WindowsCommandHandler windowsCommandHandler_;
    EuControlSwitchArray windowsAudioCommands_;
    EuControlSwitch monoAudio_;
    EuControlSwitch clearSolo_;
    NEuCon::uint32 monoAudioMemberId_ = 0U;
    NEuCon::uint32 clearSoloMemberId_ = 0U;
    std::vector<CommandCategory> commandCategories_;
    bool monoAudioEnabled_ = false;
    bool soloActive_ = false;
};
