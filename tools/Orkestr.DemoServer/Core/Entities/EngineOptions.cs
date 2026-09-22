using System.Net;
using Orkestr.Logging.Entities;

namespace Orkestr.Core.Entities;

/// <summary>
///	Process parameters read once when the engine is constructed.
///	Holds the UDP listen endpoint, the log threshold, and the optional log file path.
///	Does not open a socket, create a logger, or read the command line. The command provider
///	fills this object, and the engine copies the values into the transport and the logger.
/// </summary>
public sealed class EngineOptions
{
    /// <summary>
    ///	UDP port used when the command line does not pass one. It is above 1024, so Debian
    ///	can bind it without root.
    /// </summary>
    public const int DefaultListenPort = 2653;

    /// <summary>
    ///	IPv4 address used when the command line does not pass one. This is 0.0.0.0, every interface.
    /// </summary>
    public static readonly IPAddress DefaultListenAddress = IPAddress.Any;

    /// <summary>
    ///	Log threshold used when the command line does not pass one. Information, numeric value 2.
    /// </summary>
    public const LogLevel DefaultMinimumLogLevel = LogLevel.Information;

    /// <summary>
    ///	Checks the process invariants and stores them. A null file path means no file channel.
    /// </summary>
    /// <param name="listenAddress">IPv4 or other address the socket will bind. Null is rejected.</param>
    /// <param name="listenPort">UDP port from 1 to 65535. Zero is rejected so the socket cannot pick an ephemeral port.</param>
    /// <param name="minimumLogLevel">Defined <see cref="LogLevel"/> value. The numeric argument is converted before this call.</param>
    /// <param name="logFilePath">File to append, or null when only the console is used. Whitespace is rejected.</param>
    /// <exception cref="ArgumentNullException">The listen address is null.</exception>
    /// <exception cref="ArgumentOutOfRangeException">The port is outside 1..65535, or the level is not defined.</exception>
    /// <exception cref="ArgumentException">Log file path is empty.</exception>
    public EngineOptions(
        IPAddress listenAddress,
        int listenPort,
        LogLevel minimumLogLevel,
        string? logFilePath)
    {
        ArgumentNullException.ThrowIfNull(listenAddress);
        if (listenPort is < 1 or > 65535)
            throw new ArgumentOutOfRangeException(nameof(listenPort));
        if (!Enum.IsDefined(minimumLogLevel))
            throw new ArgumentOutOfRangeException(nameof(minimumLogLevel));
        if (logFilePath is not null && string.IsNullOrWhiteSpace(logFilePath))
            throw new ArgumentException("Log file path is empty.", nameof(logFilePath));

        ListenAddress = listenAddress;
        ListenPort = listenPort;
        MinimumLogLevel = minimumLogLevel;
        LogFilePath = logFilePath;
    }

    #region Properties

    /// <summary>
    ///	Address passed to the UDP bind. The default value prints as 0.0.0.0.
    /// </summary>
    public IPAddress ListenAddress { get; }

    /// <summary>
    ///	UDP port passed to the UDP bind, from 1 to 65535 inclusive.
    /// </summary>
    public int ListenPort { get; }

    /// <summary>
    ///	Threshold copied into the logger. It does not change while the process is running.
    /// </summary>
    public LogLevel MinimumLogLevel { get; }

    /// <summary>
    ///	Path of the file channel, or null when the process writes only to the console.
    /// </summary>
    public string? LogFilePath { get; }

    #endregion
}
