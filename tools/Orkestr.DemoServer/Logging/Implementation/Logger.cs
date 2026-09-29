using Orkestr.Logging.Entities;
using Orkestr.Logging.Interfaces;

namespace Orkestr.Logging.Implementation;

/// <summary>
///	Keeps or drops each log call by comparing it with one threshold, then hands the
///	record to every channel in list order.
///	Owns the only threshold in the process. Channels receive records that already qualify.
///	A channel that throws is skipped. The failure is not written again and does not leave this class.
/// </summary>
public sealed class Logger : ILogger
{
    #region Fields

    private readonly ILogChannel[] _channels;

    #endregion

    /// <summary>
    ///	Snapshots the channel list and the threshold. Later edits to the caller's list have no effect.
    /// </summary>
    /// <param name="channels">Destinations in write order. The list itself may be empty. A null entry is rejected.</param>
    /// <param name="minimumLevel">Lowest level that is written. Must be a defined <see cref="LogLevel"/> value.</param>
    /// <exception cref="ArgumentNullException">The channel list is null.</exception>
    /// <exception cref="ArgumentException">The channel list contains a null entry.</exception>
    /// <exception cref="ArgumentOutOfRangeException">The level is not a defined <see cref="LogLevel"/> value.</exception>
    public Logger(IReadOnlyList<ILogChannel> channels, LogLevel minimumLevel)
    {
        ArgumentNullException.ThrowIfNull(channels);
        if (!Enum.IsDefined(minimumLevel))
            throw new ArgumentOutOfRangeException(nameof(minimumLevel));
        if (channels.Any(channel => channel is null))
            throw new ArgumentException("A log channel is missing.", nameof(channels));

        _channels = channels.ToArray();
        MinimumLevel = minimumLevel;
    }

    /// <summary>
    ///	Lowest level that still reaches the channels. Fixed for the lifetime of this logger.
    /// </summary>
    public LogLevel MinimumLevel { get; }

    #region Public methods

    /// <summary>
    ///	Writes a trace record when trace is at or above <see cref="MinimumLevel"/>.
    ///	The handshake loop does not call this method.
    /// </summary>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English diagnostic text.</param>
    public void Trace(string source, string message) => Write(LogLevel.Trace, source, message);

    /// <summary>
    ///	Writes a debug record when debug is at or above <see cref="MinimumLevel"/>.
    ///	The handshake loop does not call this method.
    /// </summary>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English diagnostic text.</param>
    public void Debug(string source, string message) => Write(LogLevel.Debug, source, message);

    /// <summary>
    ///	Writes an operational record when information is at or above <see cref="MinimumLevel"/>.
    /// </summary>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English operational text.</param>
    public void Information(string source, string message) => Write(LogLevel.Information, source, message);

    /// <summary>
    ///	Writes an identity-change record when warning is at or above <see cref="MinimumLevel"/>.
    /// </summary>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English text describing the previous and the new identity.</param>
    public void Warning(string source, string message) => Write(LogLevel.Warning, source, message);

    /// <summary>
    ///	Writes a failure record when error is at or above <see cref="MinimumLevel"/>.
    /// </summary>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English failure text.</param>
    public void Error(string source, string message) => Write(LogLevel.Error, source, message);

    /// <summary>
    ///	Writes a device-conflict record. Critical passes every legal threshold.
    /// </summary>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English text naming both existing records and the new identity.</param>
    public void Critical(string source, string message) => Write(LogLevel.Critical, source, message);

    #endregion

    /// <summary>
    ///	Drops the call when the level is below the threshold. Otherwise builds one UTC record
    ///	and asks each channel to write it. A channel exception is swallowed so the remaining
    ///	channels still receive the line and the handshake loop does not see the failure.
    /// </summary>
    /// <param name="level">Importance of this call.</param>
    /// <param name="source">Namespace and class of the caller.</param>
    /// <param name="message">English text. Empty is allowed and becomes a blank separator.</param>
    /// <exception cref="ArgumentNullException">The source or the message is null.</exception>
    private void Write(LogLevel level, string source, string message)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(message);

        if (level < MinimumLevel)
            return;

        var entry = new LogEntry(DateTimeOffset.UtcNow, level, source, message);
        foreach (var channel in _channels)
        {
            try
            {
                channel.Write(entry);
            }
            catch (Exception)
            {
                // This channel failed. Do not retry through the logger: it may be the only one.
            }
        }
    }
}
