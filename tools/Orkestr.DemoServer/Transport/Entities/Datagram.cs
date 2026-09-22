using System.Net;

namespace Orkestr.Transport.Entities;

/// <summary>
///	One UDP datagram after the socket has received it and before any field is decoded.
///	Holds the payload bytes, the source IP, and the source UDP port.
///	The source port is for the receive log only. It is not a registry key and not the reply port.
/// </summary>
public sealed class Datagram
{
    /// <summary>
    ///	Stores one received datagram. The payload array is kept as given.
    /// </summary>
    /// <param name="payload">Payload bytes. Null is rejected. An empty array is allowed and fails later in the decoder.</param>
    /// <param name="sourceAddress">IP of the socket that sent the datagram.</param>
    /// <param name="sourcePort">UDP port of that socket. It is not <c>HandShake.Port</c>.</param>
    /// <exception cref="ArgumentNullException">The payload or the source address is null.</exception>
    public Datagram(byte[] payload, IPAddress sourceAddress, int sourcePort)
    {
        ArgumentNullException.ThrowIfNull(payload);
        ArgumentNullException.ThrowIfNull(sourceAddress);

        Payload = payload;
        SourceAddress = sourceAddress;
        SourcePort = sourcePort;
    }

    #region Properties

    /// <summary>
    ///	Payload bytes owned by this datagram. The decoder reads the first 626 when they exist.
    /// </summary>
    public byte[] Payload { get; }

    /// <summary>
    ///	Source IP. Together with the handshake reply port it forms the device address.
    /// </summary>
    public IPAddress SourceAddress { get; }

    /// <summary>
    ///	Source UDP port. Logged on receive and then ignored by the registry and the reply.
    /// </summary>
    public int SourcePort { get; }

    #endregion
}
