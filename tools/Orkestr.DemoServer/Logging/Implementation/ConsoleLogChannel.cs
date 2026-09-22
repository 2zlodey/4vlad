using System.Globalization;
using Orkestr.Logging.Entities;
using Orkestr.Logging.Interfaces;

namespace Orkestr.Logging.Implementation;

/// <summary>
///	Default log destination. Writes each accepted record as one English line on standard output.
///	Applies no level threshold. The logger has already decided that the record is visible.
///	Does not open a file and does not parse the message.
/// </summary>
public sealed class ConsoleLogChannel : ILogChannel
{
    /// <summary>
    ///	Destination name printed nowhere by itself. Callers use it to tell this channel from a file.
    /// </summary>
    public string Name => "Console";

    /// <summary>
    ///	Writes one line to standard output: UTC timestamp, level, source, message.
    /// </summary>
    /// <param name="entry">Record already accepted by the logger.</param>
    /// <exception cref="ArgumentNullException">The record is null.</exception>
    public void Write(LogEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        Console.WriteLine(Format(entry));
    }

    /// <summary>
    ///	Formats one record the same way the file channel does. The UTC time uses the round-trip
    ///	form that ends in Z, matching the operator log example.
    /// </summary>
    /// <param name="entry">Record to format. The caller has already rejected null.</param>
    /// <returns>A single line without a trailing newline. The newline is added by the writer.</returns>
    private static string Format(LogEntry entry)
    {
        return string.Create(
            CultureInfo.InvariantCulture,
//comented by zlodey
//          $"{entry.Timestamp.UtcDateTime:O} {entry.Level} {entry.Source} {entry.Message}");
    $"{entry.Timestamp.UtcDateTime.ToLocalTime().ToString("yy.MM.dd HH:mm:ss.fff")} {entry.Message}");

    }
}
