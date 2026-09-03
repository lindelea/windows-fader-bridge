#pragma once
#include "ChannelWriter.h"
#include <iostream>
#include <thread>

// Explicit opt-in bench tool. Not called by build scripts or CI. No EUCON node,
// audio session, solo/mute toggle, output, route or preamp writes are performed.
inline void LiveMutedChannelProbe(const std::string &name, bool readinessOnly = false)
{
    using namespace apollo;
    using Clock = std::chrono::steady_clock;
    Observer observer;
    observer.Start();
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    while (!observer.Latest().connected && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    const auto snapshot = observer.Latest();
    Channel original;
    int matches = 0;
    for (const auto &c : snapshot.channels)
        if (c.name == name)
        {
            original = c;
            ++matches;
        }
    if (!snapshot.connected || matches != 1 || !ControlEligible(original) || !original.stereo || !original.level ||
        original.level->value.Number() > -143.9 || !original.mute || !original.mute->value.Bool() || !original.solo ||
        original.solo->value.Bool() || !original.pan || !original.panRight)
        throw std::runtime_error("Probe requires one exact, already-muted stereo channel at minimum level, solo off");
    std::cout << "LIVE selected channel: " << original.name << "; key=" << original.key << "\n"
              << "Original gain=" << original.level->value.scalar << " panL=" << original.pan->value.scalar
              << " panR=" << original.panRight->value.scalar << " mute=true solo=false\n";
    std::atomic<bool> stop = false;
    if (readinessOnly)
    {
        ChannelController controller(observer);
        controller.Arm(original.key);
        const auto end = Clock::now() + std::chrono::seconds(14);
        while (Clock::now() < end)
        {
            controller.Validate();
            if (!controller.Epoch()) throw std::runtime_error(controller.Status().error);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        controller.Disarm();
        std::cout << "LIVE READINESS PASS: arm survived periodic discovery; no writes submitted.\n";
        return;
    }
    const auto guarded = [&](const Channel &fresh, ChannelField field, const Json &expected) {
        const auto &actual = FieldParameter(fresh, field);
        return observer.Latest().connected && observer.Latest().generation == snapshot.generation && fresh.mute &&
               fresh.mute->value.Bool() && fresh.solo && !fresh.solo->value.Bool() && actual &&
               SameControlValue(field, actual->value, expected);
    };
    for (auto field : {ChannelField::Level, ChannelField::PanLeft, ChannelField::PanRight})
    {
        const auto saved = FieldParameter(original, field)->value;
        const double n = saved.Number();
        const auto candidate = ControlNumber(field == ChannelField::Level ? -140 : n + (n > 0 ? -.01 : .01));
        bool attempted = false;
        std::exception_ptr failure;
        try
        {
            ChannelWriteClient client(stop);
            client.Connect();
            ChannelRequest request{original, field, candidate, 1, snapshot.generation, 1, Clock::now()};
            attempted = true;
            const auto result =
                client.Apply(request, [&](const Channel &fresh) { return guarded(fresh, field, saved); });
            if (!result)
                throw std::runtime_error("Probe precondition changed; write cancelled");
            std::cout << FieldName(field) << " readback=" << result->scalar << " confirmed\n";
        }
        catch (...)
        {
            failure = std::current_exception();
        }
        if (attempted)
        {
            // A failed acknowledgement is not a reason to retry the test write.
            // Inspect actual state and restore only our exact candidate value.
            ReadOnlyClient reader(stop);
            reader.Connect();
            NodeMap nodes;
            nodes[original.path] = reader.Get(original.path);
            const auto actual = ReadParameter(nodes, original.path, FieldName(field));
            if (!actual)
                throw std::runtime_error("RESTORATION NEEDS ATTENTION: value unavailable");
            if (!SameControlValue(field, actual->value, saved))
            {
                if (!SameControlValue(field, actual->value, candidate))
                    throw std::runtime_error("RESTORATION NEEDS ATTENTION: external value left unchanged");
                ChannelWriteClient restore(stop);
                restore.Connect();
                ChannelRequest request{original, field, saved, 1, snapshot.generation, 2, Clock::now()};
                if (!restore.Apply(request, [&](const Channel &fresh) { return guarded(fresh, field, candidate); }))
                    throw std::runtime_error("RESTORATION NEEDS ATTENTION: safety conditions changed");
            }
            std::cout << FieldName(field) << " restored=" << saved.scalar << "\n";
        }
        if (failure)
            std::rethrow_exception(failure);
    }
    std::cout << "LIVE PASS: three independent fields confirmed and restored; mute/solo/output/preamp untouched.\n";
}
