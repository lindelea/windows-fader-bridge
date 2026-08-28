using System.IO.Pipes;
using System.Text;
using FaderBridge.Core.Audio;

namespace FaderBridge.AudioHost;

internal static class PipeProtocol
{
    public const string PipeName = "FaderBridge.Eucon.2026";
    private const byte SnapshotMessage = 1;
    private const byte SetVolumeMessage = 2;
    private const byte SetMuteMessage = 3;

    public static async Task WriteSnapshotAsync(
        NamedPipeServerStream pipe,
        IReadOnlyList<AudioStripSnapshot> strips,
        CancellationToken cancellationToken)
    {
        using var payloadStream = new MemoryStream();
        using (var writer = new BinaryWriter(payloadStream, Encoding.UTF8, leaveOpen: true))
        {
            writer.Write(SnapshotMessage);
            writer.Write(checked((byte)strips.Count));
            foreach (var strip in strips)
            {
                writer.Write(checked((byte)strip.Slot));
                writer.Write((byte)((strip.IsActive ? 1 : 0) | (strip.IsMuted ? 2 : 0)));
                writer.Write(strip.Volume);
                writer.Write(strip.PeakDb);
                var nameBytes = Encoding.UTF8.GetBytes(strip.DisplayName);
                if (nameBytes.Length > 512)
                {
                    nameBytes = nameBytes[..512];
                }
                writer.Write(checked((ushort)nameBytes.Length));
                writer.Write(nameBytes);
            }
        }

        var payload = payloadStream.ToArray();
        var header = BitConverter.GetBytes(payload.Length);
        await pipe.WriteAsync(header, cancellationToken);
        await pipe.WriteAsync(payload, cancellationToken);
        await pipe.FlushAsync(cancellationToken);
    }

    public static async Task ReadCommandsAsync(
        NamedPipeServerStream pipe,
        AudioCommandBuffer commandBuffer,
        CancellationToken cancellationToken)
    {
        var header = new byte[sizeof(int)];
        while (!cancellationToken.IsCancellationRequested && pipe.IsConnected)
        {
            if (!await ReadExactlyAsync(pipe, header, cancellationToken))
            {
                return;
            }

            var length = BitConverter.ToInt32(header);
            if (length is < 2 or > 64)
            {
                throw new InvalidDataException($"Invalid command length: {length}");
            }

            var payload = new byte[length];
            if (!await ReadExactlyAsync(pipe, payload, cancellationToken))
            {
                return;
            }

            using var reader = new BinaryReader(new MemoryStream(payload), Encoding.UTF8);
            var messageType = reader.ReadByte();
            var slot = reader.ReadByte();
            switch (messageType)
            {
                case SetVolumeMessage when length == 6:
                    commandBuffer.SetVolume(slot, reader.ReadSingle());
                    break;
                case SetMuteMessage when length == 3:
                    commandBuffer.SetMute(slot, reader.ReadByte() != 0);
                    break;
                default:
                    throw new InvalidDataException($"Unknown command type: {messageType}");
            }
        }
    }

    private static async Task<bool> ReadExactlyAsync(
        Stream stream,
        Memory<byte> buffer,
        CancellationToken cancellationToken)
    {
        var offset = 0;
        while (offset < buffer.Length)
        {
            var read = await stream.ReadAsync(buffer[offset..], cancellationToken);
            if (read == 0)
            {
                return false;
            }
            offset += read;
        }
        return true;
    }
}
