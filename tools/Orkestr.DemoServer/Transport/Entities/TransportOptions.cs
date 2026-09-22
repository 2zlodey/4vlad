using System.Net;

namespace Orkestr.Transport.Entities;

/// <summary>
///	Listen endpoint copied from the process options into the transport.
///	Holds the address and port the socket binds. It does not contain a reply port.
///	The transport does not read engine types. The engine fills this object before start.
/// </summary>
public sealed class TransportOptions
{
    /// <summary>
    ///	Stores the bind endpoint. The socket is not opened here.
    /// </summary>
    /// <param name="listenAddress">Address passed to the bind call. Null is rejected.</param>
    /// <param name="listenPort">Port passed to the bind call.</param>
    /// <exception cref="ArgumentNullException">The listen address is null.</exception>
    public TransportOptions(IPAddress listenAddress, int listenPort)
    {
        ArgumentNullException.ThrowIfNull(listenAddress);

        ListenAddress = listenAddress;
        ListenPort = listenPort;
    }

    #region Properties

    /// <summary>
    ///	Address the socket binds. The default process value is 0.0.0.0.
    /// </summary>
    public IPAddress ListenAddress { get; }

    /// <summary>
    ///	UDP port the socket binds. The default process value is 2653.
    /// </summary>
    public int ListenPort { get; }

    #endregion
}
