namespace FaderBridge.Core.Audio;

public static class AudioMath
{
    public const float MinimumDb = -96f;

    public static float ScalarToDb(float scalar)
    {
        scalar = Math.Clamp(scalar, 0f, 1f);
        return scalar <= 0.0000158489f
            ? MinimumDb
            : Math.Clamp(20f * MathF.Log10(scalar), MinimumDb, 0f);
    }

    public static float DbToScalar(float decibels)
    {
        decibels = Math.Clamp(decibels, MinimumDb, 0f);
        return decibels <= MinimumDb
            ? 0f
            : Math.Clamp(MathF.Pow(10f, decibels / 20f), 0f, 1f);
    }

    public static float PeakToDb(float peak) =>
        peak <= 0.000001f ? -120f : Math.Clamp(20f * MathF.Log10(peak), -120f, 0f);
}
