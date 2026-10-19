using Orkestr.Codec.Entities;

namespace Orkestr.Codec.Interfaces;

/// <summary>
///	Reads an 11-byte VER response (opcode plus ten version bytes) into an object.
///	Does not compare the version with the constant.
///	A buffer of any other length fails here, before the result handler runs.
/// </summary>
public interface IVerResponseDecoder
{
    /// <summary>
    ///	Decodes one response. The length must be 11 bytes and the first byte must be 0x01.
    ///	The version bytes are copied and not scored.
    /// </summary>
    /// <param name="payload">Datagram body. Null is rejected. Any length other than 11 is rejected.</param>
    /// <returns>The ten version bytes.</returns>
    /// <exception cref="ArgumentNullException">The payload is null.</exception>
    /// <exception cref="ArgumentException">Invalid VER response length or opcode.</exception>
    VerResponse Decode(byte[] payload);
}
