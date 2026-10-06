#pragma once

#include "EuconChannel.h"
#include "EuconHost.h"
#include "WindowsSystemProcessor.h"
#include "WindowsCommandProcessor.h"
#include "EuConManager.h"
#include "EuPrimitiveControl.h"
#include "AudioVolumeReadback.h"
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
        EuPrimitiveControl* slider = nullptr;
        require(fader->GetPrimitive(EuControlFader::kID_Slider, &slider) == kERR_OK && slider);
        require(channel.InitializeAudioState(1.0F, 0.0F, false));
        require(channel.FaderIndex() == 728); // Ready before any node registration.
        require(channel.InitializeAudioState(0.5F, 0.0F, true));
        require(channel.FaderIndex() == 364);
        require(slider->SetCurrentIndex(0) == kERR_OK);
        require(channel.FaderIndex() == 0); // Detects divergence despite a last-sent target.
        require(channel.SetFaderNormalized(0.5F) && channel.FaderIndex() == 364);
        AudioVolumeReadback readback;
        require(!readback.Observe(false, 0) && !readback.known);
        require(readback.Observe(true, 1));
        require(!readback.Observe(false, 0) && readback.value == 1);
        require(readback.Observe(true, 0) && readback.value == 0);
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
