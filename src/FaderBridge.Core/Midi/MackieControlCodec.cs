namespace FaderBridge.Core.Midi;

public enum MackieMessageKind
{
    Unknown,
    Fader,
    Note,
    ControlChange,
    ChannelPressure
}

public readonly record struct MackieMessage(
    MackieMessageKind Kind,
    int Channel,
    int Control,
    int Value);

public static class MackieControlCodec
{
    public const int MaximumFaderValue = 16_383;

    public static MackieMessage Decode(int rawMessage)
    {
        var status = rawMessage & 0xFF;
        var command = status & 0xF0;
        var channel = status & 0x0F;
        var data1 = (rawMessage >> 8) & 0x7F;
        var data2 = (rawMessage >> 16) & 0x7F;

        return command switch
        {
            0xE0 => new MackieMessage(MackieMessageKind.Fader, channel, channel, (data2 << 7) | data1),
            0x90 or 0x80 => new MackieMessage(MackieMessageKind.Note, channel, data1, command == 0x80 ? 0 : data2),
            0xB0 => new MackieMessage(MackieMessageKind.ControlChange, channel, data1, data2),
            0xD0 => new MackieMessage(MackieMessageKind.ChannelPressure, channel, 0, data1),
            _ => new MackieMessage(MackieMessageKind.Unknown, channel, data1, data2)
        };
    }

    public static int EncodeFader(int channel, int value)
    {
        ValidateChannel(channel);
        value = Math.Clamp(value, 0, MaximumFaderValue);
        return (0xE0 | channel) | ((value & 0x7F) << 8) | (((value >> 7) & 0x7F) << 16);
    }

    public static int EncodeNote(int note, bool enabled, int channel = 0)
    {
        ValidateChannel(channel);
        ArgumentOutOfRangeException.ThrowIfNegative(note);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(note, 127);
        return (0x90 | channel) | (note << 8) | ((enabled ? 127 : 0) << 16);
    }

    /// <summary>
    /// Standard Mackie Control meter packet: one channel-pressure status byte,
    /// with strip in the high nibble and meter level in the low nibble.
    /// </summary>
    public static int EncodeStandardMeter(int strip, int level)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(strip);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(strip, 7);
        level = Math.Clamp(level, 0, 15);
        return 0xD0 | (((strip << 4) | level) << 8);
    }

    public static int NormalizedToFader(float normalized) =>
        (int)MathF.Round(Math.Clamp(normalized, 0f, 1f) * MaximumFaderValue);

    public static float FaderToNormalized(int value) =>
        Math.Clamp(value, 0, MaximumFaderValue) / (float)MaximumFaderValue;

    private static void ValidateChannel(int channel)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(channel);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(channel, 15);
    }
}
