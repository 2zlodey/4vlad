using Orkestr.Logging.Entities;

namespace Orkestr.Logging.Interfaces;

/// <summary>
///	Destination for log records that have already passed the process threshold.
///	Writes each record in full and does not drop lines by level.
///	The logger owns the threshold. A new destination is added to the logger's list.
/// </summary>
public interface ILogChannel
{
    /// <summary>
    ///	Stable destination name used to tell channels apart. Console and File are the built-in names.
    /// </summary>
    string Name { get; }

    /// <summary>
    ///	Writes one accepted record. Does not filter by level and does not trim the message.
    /// </summary>
    /// <param name="entry">Record already accepted by the logger.</param>
    void Write(LogEntry entry);
}
