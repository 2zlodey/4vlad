namespace Orkestr.Logging.Entities;

/// <summary>
///	One log record after the logger has accepted it for output.
///	Holds the UTC time, importance, caller type name, and English message.
///	Does not decide whether the line is kept: the logger has already applied the threshold.
/// </summary>
public sealed class LogEntry
{
    /// <summary>
    ///	Builds a record that channels write without further filtering.
    /// </summary>
    /// <param name="timestamp">UTC time at which the logger accepted the record.</param>
    /// <param name="level">Importance, already compared with the process threshold.</param>
    /// <param name="source">Namespace and class of the caller, without the assembly name.</param>
    /// <param name="message">English text. An empty string is a blank separator line.</param>
    /// <exception cref="ArgumentNullException">The source or the message is null.</exception>
    public LogEntry(DateTimeOffset timestamp, LogLevel level, string source, string message)
    {
        ArgumentNullException.ThrowIfNull(source);
        ArgumentNullException.ThrowIfNull(message);

        Timestamp = timestamp;
        Level = level;
        Source = source;
        Message = message;
    }

    #region Properties

    /// <summary>
    ///	UTC time captured when the logger built the record. Written with the round-trip format.
    /// </summary>
    public DateTimeOffset Timestamp { get; }

    /// <summary>
    ///	Importance of this record. Channels print the name and do not compare it again.
    /// </summary>
    public LogLevel Level { get; }

    /// <summary>
    ///	Caller identity in the form namespace.Class, taken from the live instance.
    /// </summary>
    public string Source { get; }

    /// <summary>
    ///	English message body. Empty when the engine wants a blank separator in the dump.
    /// </summary>
    public string Message { get; }

    #endregion
}
