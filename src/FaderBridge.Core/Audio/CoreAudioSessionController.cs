using System.Diagnostics;
using NAudio.CoreAudioApi;

namespace FaderBridge.Core.Audio;

public sealed class CoreAudioSessionController
{
    public const int StripCount = 16;

    private readonly object sync = new();
    private readonly StableSlotAllocator allocator = new(StripCount, TimeSpan.Zero);
    private readonly string?[] keysBySlot = new string?[StripCount];

    public IReadOnlyList<AudioStripSnapshot> ReadStrips()
    {
        lock (sync)
        {
            var applications = ReadApplications();
            var slots = allocator.Update(applications.Select(application => application.Key), DateTimeOffset.UtcNow);
            Array.Clear(keysBySlot);

            var result = Enumerable.Range(0, StripCount)
                .Select(slot => new AudioStripSnapshot(slot, false, string.Empty, 0f, -120f, false))
                .ToArray();

            foreach (var application in applications)
            {
                if (!slots.TryGetValue(application.Key, out var slot))
                {
                    continue;
                }

                keysBySlot[slot] = application.Key;
                result[slot] = new AudioStripSnapshot(
                    slot,
                    true,
                    application.DisplayName,
                    application.Volume,
                    AudioMath.PeakToDb(application.Peak),
                    application.IsMuted);
            }

            return result;
        }
    }

    public bool SetVolume(int slot, float volume)
    {
        lock (sync)
        {
            var key = GetSlotKey(slot);
            return key is not null && Apply(key, session =>
                session.SimpleAudioVolume.Volume = Math.Clamp(volume, 0f, 1f));
        }
    }

    public bool SetMute(int slot, bool isMuted)
    {
        lock (sync)
        {
            var key = GetSlotKey(slot);
            return key is not null && Apply(key, session => session.SimpleAudioVolume.Mute = isMuted);
        }
    }

    private string? GetSlotKey(int slot) =>
        slot >= 0 && slot < keysBySlot.Length ? keysBySlot[slot] : null;

    private static IReadOnlyList<AppAudioSessionSnapshot> ReadApplications()
    {
        using var enumerator = new MMDeviceEnumerator();
        using var endpoint = enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
        var sessions = endpoint.AudioSessionManager.Sessions;
        var entries = new List<SessionEntry>();

        for (var index = 0; index < sessions.Count; index++)
        {
            using var session = sessions[index];
            if (session.State.ToString().Contains("Expired", StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            entries.Add(ReadEntry(session));
        }

        return entries
            .Where(entry => !entry.IsSystemSounds)
            .GroupBy(entry => entry.Key, StringComparer.OrdinalIgnoreCase)
            .Select(group => new AppAudioSessionSnapshot(
                group.Key,
                group.Select(entry => entry.DisplayName).First(name => !string.IsNullOrWhiteSpace(name)),
                group.Average(entry => entry.Volume),
                group.Max(entry => entry.Peak),
                group.All(entry => entry.IsMuted)))
            .OrderBy(application => application.DisplayName, StringComparer.OrdinalIgnoreCase)
            .ToArray();
    }

    private static bool Apply(string key, Action<AudioSessionControl> action)
    {
        using var enumerator = new MMDeviceEnumerator();
        using var endpoint = enumerator.GetDefaultAudioEndpoint(DataFlow.Render, Role.Multimedia);
        var sessions = endpoint.AudioSessionManager.Sessions;
        var changed = false;

        for (var index = 0; index < sessions.Count; index++)
        {
            using var session = sessions[index];
            if (!string.Equals(ReadEntry(session).Key, key, StringComparison.OrdinalIgnoreCase))
            {
                continue;
            }

            action(session);
            changed = true;
        }

        return changed;
    }

    private static SessionEntry ReadEntry(AudioSessionControl session)
    {
        var processId = session.GetProcessID;
        var process = TryReadProcess(processId);
        var isSystemSounds = session.IsSystemSoundsSession;
        var displayName = FirstNonBlank(
            isSystemSounds ? "System Sounds" : null,
            NormalizeDisplayName(session.DisplayName),
            process.Name,
            $"Session {processId}");
        displayName = NormalizeApplicationName(displayName, process.Name, process.Path);
        var key = isSystemSounds
            ? "system-sounds"
            : "process:" + FirstNonBlank(process.Name, displayName).ToUpperInvariant();

        return new SessionEntry(
            key,
            displayName,
            session.SimpleAudioVolume.Volume,
            session.AudioMeterInformation.MasterPeakValue,
            session.SimpleAudioVolume.Mute,
            isSystemSounds);
    }

    private static (string? Name, string? Path) TryReadProcess(uint processId)
    {
        if (processId == 0)
        {
            return (null, null);
        }

        try
        {
            using var process = Process.GetProcessById(checked((int)processId));
            try
            {
                return (process.ProcessName, process.MainModule?.FileName);
            }
            catch
            {
                return (process.ProcessName, null);
            }
        }
        catch
        {
            return (null, null);
        }
    }

    private static string FirstNonBlank(params string?[] values) =>
        values.First(value => !string.IsNullOrWhiteSpace(value))!;

    private static string? NormalizeDisplayName(string? value) =>
        value?.StartsWith("@%", StringComparison.Ordinal) == true ? null : value;

    private static string NormalizeApplicationName(string current, string? processName, string? processPath)
    {
        if (string.Equals(processName, "AMPLibraryAgent", StringComparison.OrdinalIgnoreCase) ||
            processPath?.Contains("AppleInc.AppleMusicWin_", StringComparison.OrdinalIgnoreCase) == true)
        {
            return "Apple Music";
        }
        return current;
    }

    private sealed record SessionEntry(
        string Key,
        string DisplayName,
        float Volume,
        float Peak,
        bool IsMuted,
        bool IsSystemSounds);
}
