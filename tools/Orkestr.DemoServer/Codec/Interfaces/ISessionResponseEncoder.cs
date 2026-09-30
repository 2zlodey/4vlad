using Orkestr.Codec.Entities;

namespace Orkestr.Codec.Interfaces;

/// <summary>
///	Writes a session reply into the 52-byte buffer the device receives.
///	Does not choose the destination address and does not generate the key.
///	The engine supplies the fields and the transport sends the bytes.
/// </summary>
public interface ISessionResponseEncoder
{
    /// <summary>
    ///	Copies the key and nonce and writes the timestamp little-endian.
    ///	The returned buffer is always 52 bytes. Success is not logged here.
    /// </summary>
    /// <param name="response">Key, nonce, and timestamp already checked for length.</param>
    /// <returns>A new 52-byte buffer. The caller owns it.</returns>
    /// <exception cref="ArgumentNullException">The response is null.</exception>
    byte[] Encode(SessionResponse response);
}
