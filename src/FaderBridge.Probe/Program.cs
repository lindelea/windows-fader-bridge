using FaderBridge.Core.Audio;
using FaderBridge.Core.Midi;

Console.OutputEncoding = System.Text.Encoding.UTF8;
Console.WriteLine("FaderBridge read-only hardware probe");
Console.WriteLine("No volume or MIDI output will be changed.\n");

Console.WriteLine("MIDI inputs:");
foreach (var port in MidiPortCatalog.GetInputs())
{
    Console.WriteLine($"  [{port.Index}] {port.Name}");
}

Console.WriteLine("\nMIDI outputs:");
foreach (var port in MidiPortCatalog.GetOutputs())
{
    Console.WriteLine($"  [{port.Index}] {port.Name}");
}

Console.WriteLine("\nActive render sessions:");
var sessions = new CoreAudioSessionReader().ReadActiveRenderSessions();
foreach (var endpointGroup in sessions.GroupBy(session => new { session.EndpointId, session.EndpointName }))
{
    Console.WriteLine($"\n  Endpoint: {endpointGroup.Key.EndpointName}");
    foreach (var session in endpointGroup.OrderBy(item => item.DisplayName, StringComparer.OrdinalIgnoreCase))
    {
        Console.WriteLine(
            $"    {session.DisplayName,-34} pid={session.ProcessId,-6} " +
            $"vol={session.Volume,6:P0} peak={session.Peak,6:P0} mute={session.IsMuted,-5} state={session.State}");
        Console.WriteLine($"      id={session.SessionInstanceId}");
        Console.WriteLine($"      path={session.ProcessPath ?? "<unavailable>"}");
    }
}

Console.WriteLine($"\nFound {sessions.Count} session(s) across {sessions.Select(x => x.EndpointId).Distinct().Count()} endpoint(s).");
