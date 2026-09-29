using Orkestr.Codec.Entities;
using Orkestr.Core.Entities;

namespace Orkestr.Core.Interfaces;

/// <summary>
///	Decides whether a decoded VER response matches the client software constant.
///	Does not read offsets, send a datagram, or remove the device from the registry.
///	The engine calls this after the attempt id has already matched.
/// </summary>
public interface IVerResultHandler
{
    /// <summary>
    ///	Compares the version bytes with the constant.
    ///	Does not write the log line. The engine does that from the returned status.
    /// </summary>
    /// <param name="response">Decoded 14-byte response. Null is rejected.</param>
    /// <returns><see cref="VerStatus.None"/> when the bytes match, otherwise incompatible.</returns>
    /// <exception cref="ArgumentNullException">The response is null.</exception>
    VerStatus Handle(VerResponse response);
}
