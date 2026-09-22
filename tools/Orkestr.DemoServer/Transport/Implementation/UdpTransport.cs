using System.Net;
using System.Net.Sockets;
using Orkestr.Logging.Implementation;
using Orkestr.Logging.Interfaces;
using Orkestr.Transport.Entities;
using Orkestr.Transport.Interfaces;

namespace Orkestr.Transport.Implementation;

/// <summary>
///	UDP socket for one process lifetime. Binds on start, then receives and sends datagrams.
///	Logs the listen line after a successful bind and the send line after a successful send.
///	Does not catch socket failures and does not decode the payload. The engine applies that policy.
/// </summary>
public sealed class UdpTransport : ITransport
{
    #region Fields

    private readonly TransportOptions _options;
    private readonly ILogger _logger;
    private UdpClient? _client;

    #endregion

    /// <summary>
    ///	Stores the endpoint and the logger. The socket stays closed until <see cref="StartAsync"/>.
    /// </summary>
    /// <param name="options">Bind address and port.</param>
    /// <param name="logger">Logger used for the listen line and the send line.</param>
    /// <exception cref="ArgumentNullException">The options or the logger is null.</exception>
    public UdpTransport(TransportOptions options, ILogger logger)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(logger);

        _options = options;
        _logger = logger;
    }

    #region Public methods

    /// <summary>
    ///	Binds the UDP socket and writes the listen line at Information.
    ///	A busy port throws and leaves the process before the receive loop.
    /// </summary>
    /// <param name="cancellationToken">Observed before the bind. A cancelled token does not open the socket.</param>
    /// <returns>A completed task after the listen line is written.</returns>
    /// <exception cref="OperationCanceledException">The token was already cancelled.</exception>
    /// <exception cref="SocketException">The bind failed.</exception>
    /// <exception cref="InvalidOperationException">The socket is already open.</exception>
    public Task StartAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        if (_client is not null)
            throw new InvalidOperationException("UDP socket is already open.");

        _client = new UdpClient(new IPEndPoint(_options.ListenAddress, _options.ListenPort));
        _logger.Information(
            LogSource.From(this),
            $"UDP server listening on {_options.ListenAddress}:{_options.ListenPort}");
        return Task.CompletedTask;
    }

    /// <summary>
    ///	Waits for one datagram and copies its payload. Writes no log line of its own.
    /// </summary>
    /// <param name="cancellationToken">Cancels the wait. The engine then stops without a UDP error line.</param>
    /// <returns>A datagram whose payload is a copy of the socket buffer.</returns>
    /// <exception cref="InvalidOperationException">The socket has not been opened.</exception>
    /// <exception cref="OperationCanceledException">The token was cancelled while waiting.</exception>
    /// <exception cref="SocketException">The socket failed while waiting.</exception>
    public async Task<Datagram> ReceiveAsync(CancellationToken cancellationToken)
    {
        var client = OpenClient();
        var result = await client.ReceiveAsync(cancellationToken);
        var payload = result.Buffer.ToArray();
        return new Datagram(payload, result.RemoteEndPoint.Address, result.RemoteEndPoint.Port);
    }

    /// <summary>
    ///	Sends the buffer, then writes the send line at Information. A socket error is not logged here.
    /// </summary>
    /// <param name="address">Destination IP.</param>
    /// <param name="port">Destination UDP port chosen by the caller.</param>
    /// <param name="payload">Bytes to send.</param>
    /// <param name="cancellationToken">Observed by the send.</param>
    /// <returns>A task that completes after the send line is written.</returns>
    /// <exception cref="ArgumentNullException">The address is null.</exception>
    /// <exception cref="InvalidOperationException">The socket has not been opened.</exception>
    /// <exception cref="OperationCanceledException">The token was cancelled.</exception>
    /// <exception cref="SocketException">The socket rejected the send.</exception>
    public async Task SendAsync(
        IPAddress address,
        int port,
        ReadOnlyMemory<byte> payload,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(address);

        var client = OpenClient();
        await client.SendAsync(payload, new IPEndPoint(address, port), cancellationToken);
        _logger.Information(
            LogSource.From(this),
            $"Sent {payload.Length} bytes to {address}:{port}");
    }

    /// <summary>
    ///	Closes the socket if <see cref="StartAsync"/> opened one. A second close does nothing.
    /// </summary>
    /// <returns>A completed task after the socket is released.</returns>
    public ValueTask DisposeAsync()
    {
        _client?.Dispose();
        _client = null;
        return ValueTask.CompletedTask;
    }

    #endregion

    /// <summary>
    ///	Returns the open socket or rejects the call that arrived before bind.
    /// </summary>
    /// <returns>The socket created by <see cref="StartAsync"/>.</returns>
    /// <exception cref="InvalidOperationException">The socket has not been opened.</exception>
    private UdpClient OpenClient()
    {
        if (_client is null)
            throw new InvalidOperationException("UDP socket is not open.");

        return _client;
    }
}
