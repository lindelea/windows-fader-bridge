namespace FaderBridge.Core.Audio;

public sealed record AppAudioSessionSnapshot(
    string Key,
    string DisplayName,
    float Volume,
    float Peak,
    bool IsMuted);

public sealed record AudioStripSnapshot(
    int Slot,
    bool IsActive,
    string DisplayName,
    float Volume,
    float PeakDb,
    bool IsMuted);
