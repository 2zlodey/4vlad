using System.Net;
using Orkestr.Transport.Entities;

namespace Orkestr.Transport.Interfaces;

/// <summary>
///	UDP socket used for the whole process: bind, receive one datagram, send raw bytes.
///	Does not know handshake fields, registry keys, or the session key.
///	The caller chooses the reply address and port. Closing the socket is part of this contract.
/// </summary>
public interface ITransport : IAsyncDisposable
{
    /// <summary>
    ///	Binds the listen socket. A failure, including a busy port, escapes to the caller.
    ///	The working process treats that failure as fatal and does not enter the receive loop.
    /// </summary>
    /// <param name="cancellationToken">Token observed before the bind. The bind itself is not cancellable.</param>
    /// <returns>A task that completes when the socket is listening.</returns>
    Task StartAsync(CancellationToken cancellationToken);

    /// <summary>
    ///	Waits for one datagram. Cancellation throws <see cref="OperationCanceledException"/>
    ///	and does not report a UDP error. The payload returned is a copy.
    /// </summary>
    /// <param name="cancellationToken">Token cancelled by SIGINT or SIGTERM.</param>
    /// <returns>The next datagram, including the source UDP port.</returns>
    Task<Datagram> ReceiveAsync(CancellationToken cancellationToken);

    /// <summary>
    ///	Sends the given bytes to the given IP and port. The port is chosen by the caller.
    ///	A socket failure escapes so the engine can treat a failed reply as a handshake error.
    /// </summary>
    /// <param name="address">Destination IP. For a handshake reply this is the record's source IP.</param>
    /// <param name="port">Destination UDP port. For a handshake reply this is HandShake.Port.</param>
    /// <param name="payload">Bytes to send. The handshake reply is 52 bytes.</param>
    /// <param name="cancellationToken">Token observed by the send.</param>
    /// <returns>A task that completes after the socket accepts the datagram.</returns>
    Task SendAsync(
        IPAddress address,
        int port,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken);
}
