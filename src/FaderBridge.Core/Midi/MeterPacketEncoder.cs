namespace FaderBridge.Core.Midi;

public enum MeterPacketEncoding
{
    /// <summary>Mackie Control: D0 followed by (strip &lt;&lt; 4) | level.</summary>
    StandardPacked,

    /// <summary>
    /// Legacy EuControl profiles seen on this machine: D0..D7 selects the strip
    /// and the pressure value is the level.
    /// </summary>
    PressureChannelPerStrip
}

public static class MeterPacketEncoder
{
    public static int Encode(
        int strip,
        float normalizedPeak,
        MeterPacketEncoding encoding,
        int segmentCount = 13)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(strip);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(strip, 7);
        ArgumentOutOfRangeException.ThrowIfLessThan(segmentCount, 2);
        ArgumentOutOfRangeException.ThrowIfGreaterThan(segmentCount, 16);

        var level = (int)MathF.Round(Math.Clamp(normalizedPeak, 0f, 1f) * (segmentCount - 1));
        return encoding switch
        {
            MeterPacketEncoding.StandardPacked => MackieControlCodec.EncodeStandardMeter(strip, level),
            MeterPacketEncoding.PressureChannelPerStrip => (0xD0 | strip) | (level << 8),
            _ => throw new ArgumentOutOfRangeException(nameof(encoding), encoding, null)
        };
    }
}
