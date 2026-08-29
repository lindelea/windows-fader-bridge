namespace FaderBridge.Core.Audio;

public sealed record AudioSessionSnapshot(
    string EndpointId,
    string EndpointName,
    Guid GroupingParam,
    string SessionId,
    string SessionInstanceId,
    uint ProcessId,
    string RawDisplayName,
    string DisplayName,
    string? ProcessPath,
    float Volume,
    float Peak,
    bool IsMuted,
    string State,
    bool IsSystemSounds);
