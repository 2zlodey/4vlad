using Orkestr.Codec.Entities;
using Orkestr.Codec.Interfaces;

namespace Orkestr.Codec.Implementation;

/// <summary>
///	Builds the one-byte VER request containing command 0x01.
///	Allocates a new buffer on every call and does not log the bytes.
///	Does not choose the UDP destination. The engine does that.
/// </summary>
public sealed class VerRequestEncoder : IVerRequestEncoder
{
    /// <summary>
    ///	Writes the fixed command byte.
    /// </summary>
    /// <param name="request">VER request marker.</param>
    /// <returns>A new one-byte buffer.</returns>
    /// <exception cref="ArgumentNullException">The request is null.</exception>
    public byte[] Encode(VerRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);
        var buffer = new byte[VerLayout.RequestSize];
        buffer[VerLayout.CommandOffset] = VerLayout.CommandCode;
        return buffer;
    }
}
