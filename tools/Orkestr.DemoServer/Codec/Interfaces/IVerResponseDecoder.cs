using Orkestr.Codec.Entities;

namespace Orkestr.Codec.Interfaces;

/// <summary>
///	Reads a 14-byte VER response into an object.
///	Does not compare the version with the constant and does not match the attempt id.
///	A buffer of any other length fails here, before the result handler runs.
/// </summary>
public interface IVerResponseDecoder
{
    /// <summary>
    ///	Decodes one response. The length must be 14 bytes. Extra bytes are not ignored.
    ///	The version bytes are copied and not scored.
    /// </summary>
    /// <param name="payload">Datagram body. Null is rejected. Any length other than 14 is rejected.</param>
    /// <returns>The attempt id and the ten version bytes.</returns>
    /// <exception cref="ArgumentNullException">The payload is null.</exception>
    /// <exception cref="ArgumentException">Packet length {length} bytes, expected 14</exception>
    VerResponse Decode(byte[] payload);
}
