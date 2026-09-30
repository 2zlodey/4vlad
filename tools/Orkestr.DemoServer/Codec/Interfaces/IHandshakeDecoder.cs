using Orkestr.Codec.Entities;

namespace Orkestr.Codec.Interfaces;

/// <summary>
///	Reads the first 626 bytes of a datagram into a handshake tree.
///	Does not open a socket, update a registry, or write a log line.
///	A short buffer fails here, before the engine asks the session provider to change anything.
/// </summary>
public interface IHandshakeDecoder
{
    /// <summary>
    ///	Decodes one datagram. Bytes past the handshake length are ignored.
    ///	Coordinates, version, counter, and timestamps are not validated.
    /// </summary>
    /// <param name="payload">Datagram body. Null is rejected. Shorter than 626 bytes is rejected.</param>
    /// <returns>The handshake stored in the first 626 bytes.</returns>
    /// <exception cref="ArgumentNullException">The payload is null.</exception>
    /// <exception cref="ArgumentException">Packet too short: {length} bytes, expected at least 626</exception>
    HandShake Decode(byte[] payload);
}
