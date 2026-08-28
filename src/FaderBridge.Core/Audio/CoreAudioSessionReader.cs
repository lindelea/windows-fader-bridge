using System.Diagnostics;
using NAudio.CoreAudioApi;

namespace FaderBridge.Core.Audio;

/// <summary>
/// Takes short-lived snapshots of Windows Core Audio render sessions.
/// COM wrappers are deliberately not cached: endpoint and session identities can
/// become stale whenever an application or audio device restarts.
/// </summary>
public sealed class CoreAudioSessionReader
{
    public IReadOnlyList<AudioSessionSnapshot> ReadActiveRenderSessions()
    {
        var result = new List<AudioSessionSnapshot>();
        using var enumerator = new MMDeviceEnumerator();
        var endpoints = enumerator.EnumerateAudioEndPoints(DataFlow.Render, DeviceState.Active);

        foreach (var endpoint in endpoints)
        {
            using (endpoint)
            try
            {
                var sessions = endpoint.AudioSessionManager.Sessions;
                for (var index = 0; index < sessions.Count; index++)
                {
                    using var session = sessions[index];
                    result.Add(ReadSession(endpoint, session));
                }
            }
            catch (Exception exception) when (exception is UnauthorizedAccessException or InvalidOperationException)
            {
                // One broken or transient endpoint should not hide every other session.
            }
        }

        return result;
    }

    private static AudioSessionSnapshot ReadSession(MMDevice endpoint, AudioSessionControl session)
    {
        var processId = session.GetProcessID;
        var process = TryReadProcess(processId);
        var isSystemSounds = session.IsSystemSoundsSession;
        var displayName = FirstNonBlank(
            isSystemSounds ? "System Sounds" : null,
            NormalizeDisplayName(session.DisplayName),
            process.Name,
            $"Session {processId}");

        return new AudioSessionSnapshot(
            endpoint.ID,
            endpoint.FriendlyName,
            session.GetSessionInstanceIdentifier,
            processId,
            displayName,
            process.Path,
            session.SimpleAudioVolume.Volume,
            session.AudioMeterInformation.MasterPeakValue,
            session.SimpleAudioVolume.Mute,
            session.State.ToString(),
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
            string? path;
            try
            {
                path = process.MainModule?.FileName;
            }
            catch
            {
                path = null;
            }

            return (process.ProcessName, path);
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
}
