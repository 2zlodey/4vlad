using System.Net.Sockets;
using Orkestr.Codec.Entities;
using Orkestr.Codec.Implementation;
using Orkestr.Codec.Interfaces;
using Orkestr.Core.Entities;
using Orkestr.Core.Interfaces;
using Orkestr.Common.Logging.Implementation;
using Orkestr.Common.Logging.Interfaces;
using Orkestr.Sessions.Entities;
using Orkestr.Sessions.Implementation;
using Orkestr.Sessions.Interfaces;
using Orkestr.Transport.Entities;
using Orkestr.Transport.Implementation;
using Orkestr.Transport.Interfaces;

namespace Orkestr.Core.Implementation;

/// <summary>
///	Process composition root and the per-datagram processing loop.
///	Creates the logger, transport, codec, and session provider, then for each
///	UDP packet receives the datagram, decodes it, updates the registry, and sends
///	52 bytes to the source IP and the port in <see cref="HandShake.Port"/>.
///	After that reply it sends one VER request to the same address and waits for the version.
///	Device lookup by id and by address is performed by <see cref="ISessionProvider"/>.
/// </summary>
public sealed class OrkestrEngine : IOrkestrEngine
{
    #region Fields

    private readonly EngineOptions? _options;
    private readonly ILogger _logger;
    private readonly ITransport _transport;
    private readonly IHandshakeDecoder _decoder;
    private readonly ISessionResponseEncoder _encoder;
    private readonly ISessionProvider _sessions;
    private readonly IVerRequestEncoder _verEncoder;
    private readonly IVerResponseDecoder _verDecoder;
    private readonly IVerResultHandler _verResults;
    private readonly LogSetup? _log;
    private uint _nextRequestId = 1;

    #endregion

    #region Constructors

    /// <summary>
    ///	Builds an engine with the default listen address, port 2653, Information logs, and no file.
    ///	Does not read the command line. The process path uses the options constructor instead.
    /// </summary>
    public OrkestrEngine()
        : this(new EngineOptions(
            EngineOptions.DefaultListenAddress,
            EngineOptions.DefaultListenPort,
            EngineOptions.DefaultMinimumLogLevel,
            null))
    {
    }

    /// <summary>
    ///	Builds the logger, transport, codec, and session provider from the given process options.
    ///	A log file that cannot be opened throws before the method returns, so the receive loop never starts.
    /// </summary>
    /// <param name="options">Listen endpoint, log threshold, and optional log file path.</param>
    /// <exception cref="ArgumentNullException">The options are null.</exception>
    /// <exception cref="IOException">The log file cannot be opened for append.</exception>
    /// <exception cref="DirectoryNotFoundException">The log file directory does not exist.</exception>
    /// <exception cref="UnauthorizedAccessException">The log file cannot be written.</exception>
    public OrkestrEngine(EngineOptions options)
    {
        ArgumentNullException.ThrowIfNull(options);
        _options = options;
        (_logger, _transport, _decoder, _encoder, _sessions, _verEncoder, _verDecoder, _verResults, _log) =
            Initialize(_options ?? throw new ArgumentNullException(nameof(options)));
    }

    /// <summary>
    ///	Stores services that were built elsewhere. Does not create channels, a socket, or a registry.
    ///	The receive loop is the same one used by the other constructors.
    /// </summary>
    /// <param name="logger">Logger the loop and the injected services already share.</param>
    /// <param name="transport">Socket the loop will start, receive from, and close.</param>
    /// <param name="decoder">Handshake decoder.</param>
    /// <param name="encoder">Reply encoder.</param>
    /// <param name="sessions">Registry the loop updates through <see cref="ISessionProvider.Apply"/>.</param>
    /// <param name="verEncoder">VER request encoder.</param>
    /// <param name="verDecoder">VER response decoder.</param>
    /// <param name="verResults">Handler that compares the version bytes.</param>
    /// <exception cref="ArgumentNullException">Any service is null.</exception>
    public OrkestrEngine(
        ILogger logger,
        ITransport transport,
        IHandshakeDecoder decoder,
        ISessionResponseEncoder encoder,
        ISessionProvider sessions,
        IVerRequestEncoder verEncoder,
        IVerResponseDecoder verDecoder,
        IVerResultHandler verResults)
    {
        ArgumentNullException.ThrowIfNull(logger);
        ArgumentNullException.ThrowIfNull(transport);
        ArgumentNullException.ThrowIfNull(decoder);
        ArgumentNullException.ThrowIfNull(encoder);
        ArgumentNullException.ThrowIfNull(sessions);
        ArgumentNullException.ThrowIfNull(verEncoder);
        ArgumentNullException.ThrowIfNull(verDecoder);
        ArgumentNullException.ThrowIfNull(verResults);

        _options = null;
        _logger = logger;
        _transport = transport;
        _decoder = decoder;
        _encoder = encoder;
        _sessions = sessions;
        _verEncoder = verEncoder;
        _verDecoder = verDecoder;
        _verResults = verResults;
        _log = null;
    }

    #endregion

    #region Startup

    /// <summary>
    ///	Binds the socket and handles one datagram at a time until cancellation or a receive failure.
    ///	A handshake error is logged and the loop continues. After a successful handshake the engine
    ///	sends one VER request and waits 3000 ms. A socket error while waiting stops the loop.
    ///	The socket, and the log file when this engine opened one, are closed before the method returns.
    /// </summary>
    /// <param name="cancellationToken">Token cancelled by SIGINT or SIGTERM.</param>
    /// <returns>A task that completes when reception stops. A bind failure faults the task.</returns>
    /// <exception cref="SocketException">The listen socket could not be bound.</exception>
    public async Task RunAsync(CancellationToken cancellationToken)
    {
        // Release the socket and the log file on every way out, including a failed bind.
        try
        {
            // Bind the configured UDP endpoint before the loop. A bind failure leaves the method and stops the process.
            await _transport.StartAsync(cancellationToken);

            // Receive datagrams until the process gets SIGTERM or SIGINT.
            while (!cancellationToken.IsCancellationRequested)
            {
                try
                {
                    // Wait for one packet. The source UDP port is used only in the receive log line.
                    var datagram = await _transport.ReceiveAsync(cancellationToken);

                    // Record the length and socket address before decoding fields.
                    _logger.Information(
                        LogSource.From(this),
                        $"Received {datagram.Payload.Length} bytes from " +
                        $"{datagram.SourceAddress}:{datagram.SourcePort}");

                    // A failed handshake does not start VER. A failed VER does not stop the receive loop.
                    ConnectedDevice? device = null;
                    try
                    {
                        // Decode, register, send 52 bytes, and log the session.
                        device = await AcceptHandshakeAsync(datagram, cancellationToken);
                    }
                    // Cancellation during decode or send stops the process. It is not a packet error.
                    catch (OperationCanceledException)
                    {
                        throw;
                    }
                    // This handshake error stays in the log. The loop waits for the next datagram.
                    catch (Exception ex)
                    {
                        _logger.Error(
                            LogSource.From(this),
                            $"Handshake processing error: {ex.Message}");
                    }

                    // VER runs only after the 52-byte reply was sent.
                    if (device is not null)
                    {
                        try
                        {
                            // Send 5 bytes and wait 3000 ms for the matching 14-byte response.
                            await ExchangeVerAsync(device, cancellationToken);
                        }
                        // Cancellation during the VER wait stops the process. It is not a command error.
                        catch (OperationCanceledException)
                        {
                            throw;
                        }
                        // A socket failure while waiting is a receive failure. The outer catch stops the loop.
                        catch (SocketException)
                        {
                            throw;
                        }
                        // A VER failure stays in the log. The registry record is kept.
                        catch (Exception ex)
                        {
                            _logger.Error(
                                LogSource.From(this),
                                $"VER failed: {ex.Message}");
                        }
                    }
                }
                // Orderly service stop. The UDP error message is not written.
                catch (OperationCanceledException)
                {
                    // Leave the receive loop. The stop line is written once after it.
                    break;
                }
                // A failure while waiting on the socket stops reception.
                catch (SocketException ex)
                {
                    _logger.Error(LogSource.From(this), $"UDP error: {ex.Message}");
                    // Do not wait for another datagram after the socket has failed.
                    break;
                }
                // Any other error outside packet handling stays in the log, and the loop continues.
                catch (Exception ex)
                {
                    _logger.Error(LogSource.From(this), $"Error: {ex.Message}");
                }
            }

            // Cancellation ends the loop without a UDP error, including a signal that arrived between packets.
            if (cancellationToken.IsCancellationRequested)
            {
                // Record the stop before leaving the method.
                _logger.Information(LogSource.From(this), "Stopping");
            }
        }
        finally
        {
            // Close the listening socket after the loop or after a bind failure.
            await _transport.DisposeAsync();

            // Close the log file when initialization opened one.
            _log?.Dispose();
        }
    }

    /// <summary>
    ///	Decodes one handshake, updates the registry, sends 52 bytes, and logs the session.
    ///	Does not send VER. The caller does that only after this method returns the record.
    /// </summary>
    /// <param name="datagram">Datagram whose first 626 bytes are a handshake.</param>
    /// <param name="cancellationToken">Token observed by the reply send.</param>
    /// <returns>The registry record the reply was sent to.</returns>
    private async Task<ConnectedDevice> AcceptHandshakeAsync(
        Datagram datagram,
        CancellationToken cancellationToken)
    {
        // Decode the first 626 bytes. A short packet throws, and no response is built.
        var handshake = _decoder.Decode(datagram.Payload);

        // Log the device id before the registry changes.
        _logger.Information(
            LogSource.From(this),
            $"Device ID: {handshake.Dev.Id}");

        // Device address is the source IP and the reply port from the packet.
        _logger.Information(
            LogSource.From(this),
            $"Device address: {datagram.SourceAddress}:{handshake.Port}");

        // Apply one registry rule. The lookup branches live in the provider.
        var result = _sessions.Apply(datagram.SourceAddress, handshake);
        var device = result.Device;

        // Build 52 bytes from the session returned by the provider.
        var response = _encoder.Encode(new SessionResponse(
            device.Session.Key,
            device.Session.Nonce,
            device.Session.Timestamp));

        // Reply to the record IP and HandShake.Port. The source UDP port is not the reply port.
        await _transport.SendAsync(
            device.IpAddress,
            device.HandShake.Port,
            response,
            cancellationToken);

        // Log the key, nonce, handshake fields, and device count.
        LogAccepted(device);

        // The caller sends VER to this same record.
        return device;
    }

    /// <summary>
    ///	Sends one VER request and waits for the matching 14-byte response.
    ///	A different attempt id does not finish the wait. A handshake that arrives during the wait
    ///	is registered and answered, and does not start a second VER.
    ///	On timeout or an incompatible version the registry record stays.
    /// </summary>
    /// <param name="device">Record whose IP and reply port receive the request.</param>
    /// <param name="cancellationToken">Token cancelled by SIGINT or SIGTERM. The VER deadline is separate.</param>
    /// <returns>A task that completes when the command finishes, times out, or the version does not match.</returns>
    private async Task ExchangeVerAsync(ConnectedDevice device, CancellationToken cancellationToken)
    {
        // The first command of the process uses id 1. Zero is not issued.
        var requestId = _nextRequestId;
        _nextRequestId = _nextRequestId == uint.MaxValue ? 1u : _nextRequestId + 1u;

        // Build 5 bytes. The encoder does not choose the address.
        var request = _verEncoder.Encode(new VerRequest(requestId));

        // Record the attempt before the datagram leaves.
        _logger.Information(
            LogSource.From(this),
            $"VER request {requestId} to {device.IpAddress}:{device.HandShake.Port}");

        // Send to the record IP and HandShake.Port. One attempt only.
        await _transport.SendAsync(
            device.IpAddress,
            device.HandShake.Port,
            request,
            cancellationToken);

        // The wait ends on the process signal or after 3000 ms.
        using var wait = CancellationTokenSource.CreateLinkedTokenSource(cancellationToken);
        wait.CancelAfter(EngineOptions.DefaultVerTimeoutMilliseconds);

        // Keep reading until the matching response, the deadline, or a process stop.
        while (true)
        {
            Datagram reply;
            try
            {
                // The next datagram may be the VER response or another handshake.
                reply = await _transport.ReceiveAsync(wait.Token);
            }
            // The deadline fired. The process token is still active, so this is not a stop.
            catch (OperationCanceledException) when (!cancellationToken.IsCancellationRequested)
            {
                _logger.Error(
                    LogSource.From(this),
                    $"VER timed out after {EngineOptions.DefaultVerTimeoutMilliseconds} ms");
                return;
            }

            // Record the length and socket address before deciding what the datagram is.
            _logger.Information(
                LogSource.From(this),
                $"Received {reply.Payload.Length} bytes from " +
                $"{reply.SourceAddress}:{reply.SourcePort}");

            // A handshake during the wait is answered with 52 bytes and does not start another VER.
            if (reply.Payload.Length >= WireLayout.HandshakeSize)
            {
                try
                {
                    // Register and reply. The returned record is not given a new VER.
                    await AcceptHandshakeAsync(reply, cancellationToken);
                }
                // Cancellation during that reply stops the process.
                catch (OperationCanceledException)
                {
                    throw;
                }
                // That handshake error stays in the log. The original VER wait continues.
                catch (Exception ex)
                {
                    _logger.Error(
                        LogSource.From(this),
                        $"Handshake processing error: {ex.Message}");
                }

                // This datagram did not close the VER wait.
                continue;
            }

            // Any length other than 14 leaves the attempt open.
            if (reply.Payload.Length != VerLayout.ResponseSize)
            {
                _logger.Information(
                    LogSource.From(this),
                    $"VER ignored {reply.Payload.Length} bytes");
                continue;
            }

            // Read the attempt id and the ten version bytes.
            var response = _verDecoder.Decode(reply.Payload);

            // A different id does not close this attempt.
            if (response.RequestId != requestId)
            {
                _logger.Information(
                    LogSource.From(this),
                    $"VER ignored response {response.RequestId}");
                continue;
            }

            // Log the trimmed version text before the handler result.
            _logger.Information(
                LogSource.From(this),
                $"VER response {requestId} version {SoftwareVersion.Format(response.Version)}");

            // Compare the raw bytes with the constant. The handler does not log.
            var status = _verResults.Handle(response);

            // Matching bytes mean the application channel can take the next command.
            if (status == VerStatus.None)
            {
                _logger.Information(LogSource.From(this), "Application channel is ready");
                return;
            }

            // A mismatch stops this command. Firmware is not switched, and the record stays.
            _logger.Error(LogSource.From(this), "INCOMPATIBLE_VERSION");
            return;
        }
    }

    #endregion

    #region Initialization

    /// <summary>
    ///	Opens the log through <see cref="LogSetup"/>, then builds the transport, codec, and
    ///	session provider in dependency order. Concrete classes of those services are named only here.
    ///	If a later step throws, the log opened above is closed before the exception leaves.
    /// </summary>
    /// <param name="options">Process parameters. The listen endpoint is copied into transport options.</param>
    /// <returns>The services the loop calls, plus the log setup that owns the file.</returns>
    private static (
        ILogger Logger,
        ITransport Transport,
        IHandshakeDecoder Decoder,
        ISessionResponseEncoder Encoder,
        ISessionProvider Sessions,
        IVerRequestEncoder VerEncoder,
        IVerResponseDecoder VerDecoder,
        IVerResultHandler VerResults,
        LogSetup Log) Initialize(EngineOptions options)
    {
        LogSetup log = LogSetup.Open(options.MinimumLogLevel, options.LogFilePath);
        try
        {
            ILogger logger = log.Logger;
            ITransport transport = new UdpTransport(
                new TransportOptions(options.ListenAddress, options.ListenPort),
                logger);
            IHandshakeDecoder decoder = new HandshakeDecoder();
            ISessionResponseEncoder encoder = new SessionResponseEncoder();
            var factory = new CryptographicSessionFactory(WireLayout.KeySize, WireLayout.NonceSize);
            ISessionProvider sessions = new InMemorySessionProvider(factory, logger);
            IVerRequestEncoder verEncoder = new VerRequestEncoder();
            IVerResponseDecoder verDecoder = new VerResponseDecoder();
            IVerResultHandler verResults = new VerResultHandler();
            return (logger, transport, decoder, encoder, sessions, verEncoder, verDecoder, verResults, log);
        }
        catch
        {
            log.Dispose();
            throw;
        }
    }

    #endregion

    #region Packet log

    /// <summary>
    ///	Writes the session, the handshake fields, and the device count after a reply has been sent.
    ///	Blank separator lines are information records with an empty message.
    ///	The count is read after the registry update, so it includes the record just stored.
    /// </summary>
    /// <param name="device">Record whose session was just sent.</param>
    private void LogAccepted(ConnectedDevice device)
    {
        var source = LogSource.From(this);
        _logger.Information(source, $"Key: {Convert.ToHexString(device.Session.Key)}");
        _logger.Information(source, $"Nonce: {Convert.ToHexString(device.Session.Nonce)}");
        _logger.Information(source, $"Timestamp: {device.Session.Timestamp}");
        _logger.Information(source, string.Empty);
        _logger.Information(source, "HandShake:");
        _logger.Information(source, $"  Cnt:  {device.HandShake.Cnt}");
        _logger.Information(source, $"  TSS:  {device.HandShake.TSS}");
        _logger.Information(source, $"  TSN:  {device.HandShake.TSN}");
        _logger.Information(source, $"  Port: {device.HandShake.Port}");
        _logger.Information(source, string.Empty);
        _logger.Information(source, "Device:");
        _logger.Information(source, $"  Id:       {device.HandShake.Dev.Id}");
        _logger.Information(source, $"  Version:  {device.HandShake.Dev.Version}");
        _logger.Information(source, $"  Lon:      {device.HandShake.Dev.Coord.Lon}");
        _logger.Information(source, $"  Lat:      {device.HandShake.Dev.Coord.Lat}");
        _logger.Information(source, $"  Antennas: {device.HandShake.Dev.Rfin.Length}");
        _logger.Information(source, $"  IP:       {device.IpAddress}");
        _logger.Information(source, string.Empty);
        _logger.Information(source, $"Device count: {_sessions.Count}");
    }

    #endregion
}
