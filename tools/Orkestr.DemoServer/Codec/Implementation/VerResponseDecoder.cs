using Orkestr.Codec.Entities;
using Orkestr.Codec.Interfaces;

namespace Orkestr.Codec.Implementation;

/// <summary>
///	Decodes an 11-byte VER response into ten version bytes.
///	Rejects every other length and does not log success or failure.
///	Does not score the version.
/// </summary>
public sealed class VerResponseDecoder : IVerResponseDecoder
{
    /// <summary>
    ///	Reads a buffer that is exactly 11 bytes and starts with opcode 0x01.
    /// </summary>
    /// <param name="payload">Datagram body.</param>
    /// <returns>The ten version bytes.</returns>
    /// <exception cref="ArgumentNullException">The payload is null.</exception>
    /// <exception cref="ArgumentException">Invalid VER response length or opcode.</exception>
    public VerResponse Decode(byte[] payload)
    {
        ArgumentNullException.ThrowIfNull(payload);
        if (payload.Length != VerLayout.ResponseSize || payload[VerLayout.CommandOffset] != VerLayout.CommandCode)
        {
            throw new ArgumentException(
                $"Invalid VER response: length {payload.Length}, expected {VerLayout.ResponseSize}, " +
                $"opcode 0x{VerLayout.CommandCode:X2}");
        }
        var version = payload.AsSpan(VerLayout.VersionOffset, VerLayout.VersionSize).ToArray();
        return new VerResponse(version);
    }
}
