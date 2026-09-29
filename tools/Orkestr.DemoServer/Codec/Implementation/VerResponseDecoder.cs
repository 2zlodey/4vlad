using System.Buffers.Binary;
using Orkestr.Codec.Entities;
using Orkestr.Codec.Interfaces;

namespace Orkestr.Codec.Implementation;

/// <summary>
///	Decodes a 14-byte VER response into an attempt id and ten version bytes.
///	Rejects every other length and does not log success or failure.
///	Does not compare the id with the request in flight and does not score the version.
/// </summary>
public sealed class VerResponseDecoder : IVerResponseDecoder
{
    /// <summary>
    ///	Reads a buffer that is exactly 14 bytes.
    /// </summary>
    /// <param name="payload">Datagram body.</param>
    /// <returns>The response stored in the buffer.</returns>
    /// <exception cref="ArgumentNullException">The payload is null.</exception>
    /// <exception cref="ArgumentException">Packet length {length} bytes, expected 14</exception>
    public VerResponse Decode(byte[] payload)
    {
        ArgumentNullException.ThrowIfNull(payload);
        if (payload.Length != VerLayout.ResponseSize)
        {
            throw new ArgumentException(
                $"Packet length {payload.Length} bytes, expected {VerLayout.ResponseSize}");
        }

        var requestId = BinaryPrimitives.ReadUInt32LittleEndian(
            payload.AsSpan(VerLayout.RequestIdOffset, sizeof(uint)));
        var version = payload.AsSpan(VerLayout.VersionOffset, VerLayout.VersionSize).ToArray();
        return new VerResponse(requestId, version);
    }
}
