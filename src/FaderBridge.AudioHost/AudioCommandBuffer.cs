namespace FaderBridge.AudioHost;

internal sealed class AudioCommandBuffer
{
    private readonly object sync = new();
    private readonly float?[] volumes = new float?[16];
    private readonly bool?[] mutes = new bool?[16];

    public void SetVolume(int slot, float volume)
    {
        if (slot is < 0 or >= 16)
        {
            return;
        }
        lock (sync)
        {
            volumes[slot] = volume;
        }
    }

    public void SetMute(int slot, bool muted)
    {
        if (slot is < 0 or >= 16)
        {
            return;
        }
        lock (sync)
        {
            mutes[slot] = muted;
        }
    }

    public IReadOnlyList<AudioCommand> Drain()
    {
        lock (sync)
        {
            var result = new List<AudioCommand>();
            for (var slot = 0; slot < 16; slot++)
            {
                if (volumes[slot] is { } volume)
                {
                    result.Add(new AudioCommand(slot, volume, null));
                    volumes[slot] = null;
                }
                if (mutes[slot] is { } mute)
                {
                    result.Add(new AudioCommand(slot, null, mute));
                    mutes[slot] = null;
                }
            }
            return result;
        }
    }
}

internal sealed record AudioCommand(int Slot, float? Volume, bool? Muted);
