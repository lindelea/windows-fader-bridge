namespace FaderBridge.Core.Audio;

public sealed class StableSlotAllocator
{
    private sealed record Entry(string Key, DateTimeOffset LastSeen);

    private readonly Entry?[] slots;
    private readonly TimeSpan retention;

    public StableSlotAllocator(int slotCount, TimeSpan? retention = null)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(slotCount);
        slots = new Entry?[slotCount];
        this.retention = retention ?? TimeSpan.FromSeconds(5);
    }

    public IReadOnlyDictionary<string, int> Update(IEnumerable<string> activeKeys, DateTimeOffset now)
    {
        var keys = activeKeys.Distinct(StringComparer.OrdinalIgnoreCase).ToArray();
        var active = keys.ToHashSet(StringComparer.OrdinalIgnoreCase);

        for (var index = 0; index < slots.Length; index++)
        {
            var entry = slots[index];
            if (entry is null)
            {
                continue;
            }

            if (active.Contains(entry.Key))
            {
                slots[index] = entry with { LastSeen = now };
            }
            else if (now - entry.LastSeen >= retention)
            {
                slots[index] = null;
            }
        }

        foreach (var key in keys)
        {
            if (FindSlot(key) >= 0)
            {
                continue;
            }

            var freeSlot = Array.FindIndex(slots, entry => entry is null);
            if (freeSlot < 0)
            {
                break;
            }

            slots[freeSlot] = new Entry(key, now);
        }

        return slots
            .Select((entry, index) => (entry, index))
            .Where(item => item.entry is not null && active.Contains(item.entry.Key))
            .ToDictionary(item => item.entry!.Key, item => item.index, StringComparer.OrdinalIgnoreCase);
    }

    private int FindSlot(string key) =>
        Array.FindIndex(slots, entry =>
            entry is not null && string.Equals(entry.Key, key, StringComparison.OrdinalIgnoreCase));
}
