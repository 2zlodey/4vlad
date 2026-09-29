using System.Buffers.Binary;
using Orkestr.Codec.Entities;
using Orkestr.Codec.Interfaces;

namespace Orkestr.Codec.Implementation;

/// <summary>
///	Builds the 52-byte session reply: key, nonce, then a little-endian timestamp.
///	Allocates a new buffer on every call and does not log the bytes.
///	Does not generate the key or choose the UDP destination. The engine does both.
/// </summary>
public sealed class SessionResponseEncoder : ISessionResponseEncoder
{
    /// <summary>
    ///	Copies the key and nonce and writes the timestamp with a fixed little-endian layout.
    /// </summary>
    /// <param name="response">Fields already checked for key and nonce length.</param>
    /// <returns>A new 52-byte buffer.</returns>
    /// <exception cref="ArgumentNullException">The response is null.</exception>
    public byte[] Encode(SessionResponse response)
    {
        ArgumentNullException.ThrowIfNull(response);

        var buffer = new byte[WireLayout.ResponseSize];
        response.Key.CopyTo(buffer.AsSpan(WireLayout.KeyOffset, WireLayout.KeySize));
        response.Nonce.CopyTo(buffer.AsSpan(WireLayout.NonceOffset, WireLayout.NonceSize));
        BinaryPrimitives.WriteInt64LittleEndian(
            buffer.AsSpan(WireLayout.TimestampOffset, WireLayout.TimestampSize),
            response.Timestamp);
        return buffer;
    }
}
