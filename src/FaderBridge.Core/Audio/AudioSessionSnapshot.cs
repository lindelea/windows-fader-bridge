namespace FaderBridge.Core.Audio;

public sealed record AudioSessionSnapshot(
    string EndpointId,
    string EndpointName,
    string SessionInstanceId,
    uint ProcessId,
    string DisplayName,
    string? ProcessPath,
    float Volume,
    float Peak,
    bool IsMuted,
    string State,
    bool IsSystemSounds);
