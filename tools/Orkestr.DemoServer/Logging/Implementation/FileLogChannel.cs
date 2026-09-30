using System.Globalization;
using System.Text;
using Orkestr.Logging.Entities;
using Orkestr.Logging.Interfaces;

namespace Orkestr.Logging.Implementation;

/// <summary>
///	Appends each accepted log record to a UTF-8 file and flushes it to disk.
///	Applies no level threshold. The line layout matches the console channel.
///	Does not create missing directories. The engine closes this channel when the loop ends.
/// </summary>
public sealed class FileLogChannel : ILogChannel, IDisposable
{
    #region Fields

    private readonly FileStream _stream;
    private readonly StreamWriter _writer;
    private bool _disposed;

    #endregion

    /// <summary>
    ///	Opens the file for append. The directory must already exist.
    /// </summary>
    /// <param name="path">File path, absolute or relative to the process working directory.</param>
    /// <exception cref="ArgumentException">The path is null, empty, or whitespace.</exception>
    /// <exception cref="DirectoryNotFoundException">The directory does not exist.</exception>
    /// <exception cref="IOException">The file cannot be opened for append.</exception>
    /// <exception cref="UnauthorizedAccessException">The process cannot write the file.</exception>
    public FileLogChannel(string path)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(path);

        _stream = new FileStream(path, FileMode.Append, FileAccess.Write, FileShare.Read);
        _writer = new StreamWriter(_stream, new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    }

    /// <summary>
    ///	Destination name used to tell this channel from the console.
    /// </summary>
    public string Name => "File";

    #region Public methods

    /// <summary>
    ///	Appends one line and flushes it through to disk so a crash keeps the record.
    /// </summary>
    /// <param name="entry">Record already accepted by the logger.</param>
    /// <exception cref="ArgumentNullException">The record is null.</exception>
    /// <exception cref="ObjectDisposedException">The channel has been closed.</exception>
    /// <exception cref="IOException">The file cannot be extended.</exception>
    public void Write(LogEntry entry)
    {
        ArgumentNullException.ThrowIfNull(entry);
        ObjectDisposedException.ThrowIf(_disposed, this);

        _writer.WriteLine(Format(entry));
        _writer.Flush();
        _stream.Flush(flushToDisk: true);
    }

    /// <summary>
    ///	Closes the writer and the underlying file. A second call does nothing.
    /// </summary>
    public void Dispose()
    {
        if (_disposed)
            return;

        _disposed = true;
        _writer.Dispose();
    }

    #endregion

    /// <summary>
    ///	Formats one record the same way the console channel does. The UTC time uses the round-trip
    ///	form that ends in Z, matching the operator log example.
    /// </summary>
    /// <param name="entry">Record to format. The caller has already rejected null.</param>
    /// <returns>A single line without a trailing newline. The writer adds the newline.</returns>
    private static string Format(LogEntry entry)
    {
        return string.Create(
            CultureInfo.InvariantCulture,
            $"{entry.Timestamp.UtcDateTime:O} {entry.Level} {entry.Source} {entry.Message}");
    }
}
