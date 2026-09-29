using Orkestr.Codec.Entities;

namespace Orkestr.Codec.Interfaces;

/// <summary>
///	Writes a VER request into the five-byte buffer the client receives.
///	Does not choose the attempt id, the destination, or the reply deadline.
///	The engine supplies the id and the transport sends the bytes.
/// </summary>
public interface IVerRequestEncoder
{
    /// <summary>
    ///	Writes the attempt id and command 0x01. The returned buffer is always 5 bytes.
    ///	Success is not logged here.
    /// </summary>
    /// <param name="request">Attempt id. Null is rejected.</param>
    /// <returns>A new 5-byte buffer. The caller owns it.</returns>
    /// <exception cref="ArgumentNullException">The request is null.</exception>
    byte[] Encode(VerRequest request);
}
