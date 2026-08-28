using FaderBridge.Core.Audio;

namespace FaderBridge.Tests;

public sealed class StableSlotAllocatorTests
{
    [Fact]
    public void ExistingApplicationsKeepTheirSlotsWhenSortOrderChanges()
    {
        var allocator = new StableSlotAllocator(4, TimeSpan.FromSeconds(5));
        var now = DateTimeOffset.UtcNow;

        var first = allocator.Update(["Chrome", "Cubase"], now);
        var second = allocator.Update(["Cubase", "Chrome", "Discord"], now.AddSeconds(1));

        Assert.Equal(first["Chrome"], second["Chrome"]);
        Assert.Equal(first["Cubase"], second["Cubase"]);
        Assert.Equal(2, second["Discord"]);
    }

    [Fact]
    public void MissingApplicationSlotIsRetainedBrieflyThenReused()
    {
        var allocator = new StableSlotAllocator(2, TimeSpan.FromSeconds(5));
        var now = DateTimeOffset.UtcNow;

        allocator.Update(["Chrome", "Cubase"], now);
        var duringGrace = allocator.Update(["Cubase", "Discord"], now.AddSeconds(2));
        var afterGrace = allocator.Update(["Cubase", "Discord"], now.AddSeconds(6));

        Assert.DoesNotContain("Discord", duringGrace.Keys);
        Assert.Equal(0, afterGrace["Discord"]);
    }
}
