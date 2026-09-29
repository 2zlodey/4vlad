using System.Buffers.Binary;
using Orkestr.Codec.Entities;
using Orkestr.Codec.Interfaces;

namespace Orkestr.Codec.Implementation;

/// <summary>
///	Builds the 5-byte VER request: attempt id, then command 0x01.
///	Allocates a new buffer on every call and does not log the bytes.
///	Does not allocate the id or choose the UDP destination. The engine does both.
/// </summary>
public sealed class VerRequestEncoder : IVerRequestEncoder
{
    /// <summary>
    ///	Writes the attempt id little-endian and the fixed command byte.
    /// </summary>
    /// <param name="request">Attempt id already chosen by the engine.</param>
    /// <returns>A new 5-byte buffer.</returns>
    /// <exception cref="ArgumentNullException">The request is null.</exception>
    public byte[] Encode(VerRequest request)
    {
        ArgumentNullException.ThrowIfNull(request);

        var buffer = new byte[VerLayout.RequestSize];
        BinaryPrimitives.WriteUInt32LittleEndian(buffer.AsSpan(VerLayout.RequestIdOffset, sizeof(uint)), request.RequestId);
        buffer[VerLayout.CommandOffset] = VerLayout.CommandCode;
        return buffer;
    }
}
