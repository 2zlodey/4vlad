using Orkestr.Codec.Entities;

namespace Orkestr.Codec.Interfaces;

/// <summary>
///	Writes the one-byte opcode-only VER request the client receives.
///	Does not choose the destination or the reply deadline.
/// </summary>
public interface IVerRequestEncoder
{
    /// <summary>
    ///	Writes command 0x01. The returned buffer is always one byte.
    ///	Success is not logged here.
    /// </summary>
    /// <param name="request">VER request marker. Null is rejected.</param>
    /// <returns>A new one-byte buffer. The caller owns it.</returns>
    /// <exception cref="ArgumentNullException">The request is null.</exception>
    byte[] Encode(VerRequest request);
}
