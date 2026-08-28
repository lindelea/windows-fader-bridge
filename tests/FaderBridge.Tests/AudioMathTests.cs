using FaderBridge.Core.Audio;

namespace FaderBridge.Tests;

public sealed class AudioMathTests
{
    [Theory]
    [InlineData(1f, 0f)]
    [InlineData(0.5f, -6.0206f)]
    [InlineData(0.1f, -20f)]
    [InlineData(0f, AudioMath.MinimumDb)]
    public void ScalarToDbUsesAmplitudeLaw(float scalar, float expected)
    {
        Assert.InRange(MathF.Abs(expected - AudioMath.ScalarToDb(scalar)), 0f, 0.001f);
    }

    [Theory]
    [InlineData(0f)]
    [InlineData(-6f)]
    [InlineData(-20f)]
    [InlineData(-60f)]
    public void VolumeConversionRoundTrips(float decibels)
    {
        var result = AudioMath.ScalarToDb(AudioMath.DbToScalar(decibels));
        Assert.InRange(MathF.Abs(decibels - result), 0f, 0.001f);
    }
}
