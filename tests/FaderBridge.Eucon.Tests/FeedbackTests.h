#pragma once

#include "EuconChannel.h"
#include "EuconHost.h"
#include "WindowsSystemProcessor.h"
#include "WindowsCommandProcessor.h"
#include "EuConManager.h"
#include "EuPrimitiveControl.h"
#include <stdexcept>

// Unregistered SDK models only: no audio engine, commands, or surface assignment.
inline int RunFeedbackTests()
{
    if (EuConManager::Initialize() != kERR_OK) return 2;
    int result = 0;
    try
    {
        auto require = [](bool value) { if (!value) throw std::runtime_error("feedback regression"); };
        FaderBridgeNode node(nullptr);
        require(node.ConsumeRefreshRequest());
        require(!node.ConsumeRefreshRequest());
        node.RequestRefresh();
        node.RequestRefresh();
        require(node.ConsumeRefreshRequest());
        require(!node.ConsumeRefreshRequest());

        int changes = 0;
        auto changed = [&](float, NEuCon::uint16, float) { ++changes; };
        EuconChannel channel(0, 0, L"FeedbackTest", L"Test",
            changed, changed, changed, changed, changed);
        AttributeChangeData visibility;
        visibility.mIntAttributeKey = kATRIBID_SurfaceIsVisible;
        visibility.mIntAttributeValue = 1;
        channel.OnProcessorCallback(kEVT_AttributeChange, 0, &visibility);
        require(channel.ConsumeFeedbackRefresh());
        channel.OnProcessorCallback(kEVT_AttributeChange, 0, &visibility);
        require(!channel.ConsumeFeedbackRefresh());
        visibility.mIntAttributeValue = 0;
        channel.OnProcessorCallback(kEVT_AttributeChange, 0, &visibility);
        require(!channel.ConsumeFeedbackRefresh());
        visibility.mIntAttributeValue = 1;
        channel.OnProcessorCallback(kEVT_AttributeChange, 0, &visibility);
        require(channel.ConsumeFeedbackRefresh());

        std::vector<EuControl*> controls;
        require(channel.GetContainedControls(controls) == kERR_OK);
        EuControlFader* fader = nullptr;
        for (auto* control : controls)
            if (auto* candidate = dynamic_cast<EuControlFader*>(control)) fader = candidate;
        require(fader != nullptr);
        EuPrimitiveControl* mute = nullptr;
        require(fader->GetPrimitive(EuControlFader::kID_Mute, &mute) == kERR_OK && mute);
        channel.OnPrimitiveCallback(kEVT_PRIM_StateChange, kPRIMITIVE_FORCE_UPDATE,
            1, 0, EuControlFader::kID_Mute, mute, 1);
        require(changes == 0);
        channel.OnPrimitiveCallback(kEVT_PRIM_StateChange, 0,
            1, 0, EuControlFader::kID_Mute, mute, 1);
        require(changes == 1);

        int commands = 0;
        WindowsSystemProcessor system([&] { ++commands; });
        system.OnPrimitiveCallback(kEVT_PRIM_StateChange, kPRIMITIVE_FORCE_UPDATE, 1, 0, 0, nullptr, 1);
        require(commands == 0);
        system.OnPrimitiveCallback(kEVT_PRIM_StateChange, 0, 1, 0, 0, nullptr, 1);
        require(commands == 1);
        WindowsCommandProcessor command([&] { ++commands; }, [&] { ++commands; },
            [&](WindowsCommand) { ++commands; });
        for (unsigned container = 1; container < 32; ++container)
            for (unsigned member = 0; member < 256; ++member)
                command.OnPrimitiveCallback(kEVT_PRIM_StateChange, kPRIMITIVE_FORCE_UPDATE,
                    container, member, 0, nullptr, 1);
        require(commands == 1);
    }
    catch (...) { result = 1; }
    EuConManager::Destroy();
    return result;
}
