using FaderBridge.Core.Midi;

namespace FaderBridge.Tests;

public sealed class MackieControlCodecTests
{
    [Theory]
    [InlineData(0, 0)]
    [InlineData(7, 8_192)]
    [InlineData(15, 16_383)]
    public void FaderPacketRoundTrips(int channel, int value)
    {
        var decoded = MackieControlCodec.Decode(MackieControlCodec.EncodeFader(channel, value));

        Assert.Equal(MackieMessageKind.Fader, decoded.Kind);
        Assert.Equal(channel, decoded.Channel);
        Assert.Equal(value, decoded.Value);
    }

    [Fact]
    public void StandardMeterPacksStripAndLevelIntoPressureValue()
    {
        var raw = MackieControlCodec.EncodeStandardMeter(strip: 6, level: 11);
        var decoded = MackieControlCodec.Decode(raw);

        Assert.Equal(MackieMessageKind.ChannelPressure, decoded.Kind);
        Assert.Equal(0x6B, decoded.Value);
    }

    [Theory]
    [InlineData(0f, 0)]
    [InlineData(1f, 16_383)]
    [InlineData(-1f, 0)]
    [InlineData(2f, 16_383)]
    public void NormalizedFaderValueIsClamped(float normalized, int expected)
    {
        Assert.Equal(expected, MackieControlCodec.NormalizedToFader(normalized));
    }

    [Fact]
    public void LegacyEuMidiMeterUsesPressureChannelAsStrip()
    {
        var raw = MeterPacketEncoder.Encode(
            strip: 5,
            normalizedPeak: 1f,
            MeterPacketEncoding.PressureChannelPerStrip,
            segmentCount: 10);
        var decoded = MackieControlCodec.Decode(raw);

        Assert.Equal(MackieMessageKind.ChannelPressure, decoded.Kind);
        Assert.Equal(5, decoded.Channel);
        Assert.Equal(9, decoded.Value);
    }
}
