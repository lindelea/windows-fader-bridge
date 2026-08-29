namespace FaderBridge.Core.Audio;

public static class AudioMath
{
    public static float PeakToDb(float peak) =>
        peak <= 0.000001f ? -120f : Math.Clamp(20f * MathF.Log10(peak), -120f, 0f);
}
