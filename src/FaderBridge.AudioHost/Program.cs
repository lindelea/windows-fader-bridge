using System.IO.Pipes;
using FaderBridge.AudioHost;
using FaderBridge.Core.Audio;

using var cancellation = new CancellationTokenSource();
Console.CancelKeyPress += (_, eventArgs) =>
{
    eventArgs.Cancel = true;
    cancellation.Cancel();
};

var controller = new CoreAudioSessionController();

while (!cancellation.IsCancellationRequested)
{
    try
    {
        await using var pipe = new NamedPipeServerStream(
            PipeProtocol.PipeName,
            PipeDirection.InOut,
            1,
            PipeTransmissionMode.Byte,
            PipeOptions.Asynchronous);

        await pipe.WaitForConnectionAsync(cancellation.Token);
        // Commands are applied directly by the pipe reader. The 30 ms cadence
        // below is only for metering/snapshots and never delays a fader write.
        var commandTask = PipeProtocol.ReadCommandsAsync(pipe, controller, cancellation.Token);

        while (pipe.IsConnected && !commandTask.IsCompleted && !cancellation.IsCancellationRequested)
        {
            await PipeProtocol.WriteSnapshotAsync(pipe, controller.ReadStrips(), cancellation.Token);
            await Task.Delay(30, cancellation.Token);
        }

        await commandTask;
    }
    catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
    {
        break;
    }
    catch (Exception exception) when (
        exception is IOException or UnauthorizedAccessException or InvalidOperationException)
    {
        await Task.Delay(500, cancellation.Token);
    }
}
