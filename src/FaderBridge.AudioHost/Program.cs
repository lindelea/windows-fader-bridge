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
var commandBuffer = new AudioCommandBuffer();

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
        var commandTask = PipeProtocol.ReadCommandsAsync(pipe, commandBuffer, cancellation.Token);

        while (pipe.IsConnected && !commandTask.IsCompleted && !cancellation.IsCancellationRequested)
        {
            foreach (var command in commandBuffer.Drain())
            {
                if (command.Volume is { } volume)
                {
                    controller.SetVolume(command.Slot, volume);
                }
                if (command.Muted is { } muted)
                {
                    controller.SetMute(command.Slot, muted);
                }
            }
            var strips = controller.ReadStrips();
            await PipeProtocol.WriteSnapshotAsync(pipe, strips, cancellation.Token);
            await Task.Delay(30, cancellation.Token);
        }

        await commandTask;
    }
    catch (OperationCanceledException) when (cancellation.IsCancellationRequested)
    {
        break;
    }
    catch (Exception exception) when (exception is IOException or UnauthorizedAccessException or InvalidOperationException)
    {
        await Task.Delay(500, cancellation.Token);
    }
}
