using System.Net;
using Orkestr.Codec.Entities;
using Orkestr.Sessions.Entities;

namespace Orkestr.Sessions.Interfaces;

/// <summary>
///	In-memory collection of connected devices and the only way to change it.
///	Applies one registry rule per handshake and reports how many records remain.
///	Does not open a socket and does not build the 52-byte reply.
/// </summary>
public interface ISessionProvider
{
    /// <summary>
    ///	Number of records currently stored. There is no upper bound and no idle expiry.
    /// </summary>
    int Count { get; }

    /// <summary>
    ///	Finds or creates a device record and applies one of the five registry rules.
    ///	Logs the outcome. Returns the record the engine sends the response to.
    /// </summary>
    /// <param name="sourceAddress">IP of the socket that sent the datagram.</param>
    /// <param name="handshake">Decoded packet. The reply port is read from <see cref="HandShake.Port"/>.</param>
    /// <returns>The outcome and the record already stored in the collection.</returns>
    RegistryResult Apply(IPAddress sourceAddress, HandShake handshake);
}
