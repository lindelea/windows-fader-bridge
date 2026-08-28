using NAudio.Midi;

namespace FaderBridge.Core.Midi;

public sealed record MidiPortInfo(int Index, string Name);

/// <summary>
/// Uses WinMM instead of Windows.Devices.Midi. The old MIDI Mixer core used the
/// latter and currently logs E_NOTIMPL when opening a device on this machine.
/// </summary>
public static class MidiPortCatalog
{
    public static IReadOnlyList<MidiPortInfo> GetInputs() =>
        Enumerable.Range(0, MidiIn.NumberOfDevices)
            .Select(index => new MidiPortInfo(index, MidiIn.DeviceInfo(index).ProductName))
            .ToArray();

    public static IReadOnlyList<MidiPortInfo> GetOutputs() =>
        Enumerable.Range(0, MidiOut.NumberOfDevices)
            .Select(index => new MidiPortInfo(index, MidiOut.DeviceInfo(index).ProductName))
            .ToArray();
}
