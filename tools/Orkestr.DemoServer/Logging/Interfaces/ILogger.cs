using Orkestr.Logging.Entities;

namespace Orkestr.Logging.Interfaces;

/// <summary>
///	Process log passed into the transport and the session provider.
///	Each level is its own method so the caller passes the source type and the English text explicitly.
///	Drops a record whose level is below the threshold before any channel sees it.
/// </summary>
public interface ILogger
{
    /// <summary>
    ///	Lowest level that still reaches the channels. A larger value keeps fewer lines.
    /// </summary>
    LogLevel MinimumLevel { get; }

    /// <summary>
    ///	Call for the finest diagnostic steps that are not needed to follow the process:
    ///	a field offset, an internal branch, a temporary buffer size.
    ///	The default minimum level is Information, so Trace stays off the console
    ///	unless the threshold is lowered. The working handshake loop does not call Trace.
    /// </summary>
    /// <param name="source">Namespace and class of the caller, usually LogSource.From(this).</param>
    /// <param name="message">English diagnostic text.</param>
    void Trace(string source, string message);

    /// <summary>
    ///	Call for diagnostic detail used while investigating one packet or one registry
    ///	lookup, when that detail is not part of the normal operator log.
    ///	The default threshold hides Debug. Do not use Debug for the key, nonce,
    ///	or handshake field dump: those lines are Information and stay visible.
    ///	The working handshake loop does not call Debug.
    /// </summary>
    /// <param name="source">Namespace and class of the caller, usually LogSource.From(this).</param>
    /// <param name="message">English diagnostic text.</param>
    void Debug(string source, string message);

    /// <summary>
    ///	Call for the normal path that the operator reads at the default threshold.
    ///	Use it when the socket starts listening, a datagram is received, the device id
    ///	and device address are known, a device is created or updated, a response is sent,
    ///	the session and handshake fields are dumped, the device count is reported,
    ///	or the process is stopping. A successful handshake is Information.
    /// </summary>
    /// <param name="source">Namespace and class of the caller, usually LogSource.From(this).</param>
    /// <param name="message">English operational text.</param>
    void Information(string source, string message);

    /// <summary>
    ///	Call when the registry accepts the packet and the operator must see that
    ///	identity changed. Use it for a device id that moved to a new address,
    ///	and for an address that presented a new device id.
    ///	The process continues and still sends the 52-byte response.
    ///	These two registry outcomes are the Warning cases. A device conflict is Critical.
    /// </summary>
    /// <param name="source">Namespace and class of the caller, usually LogSource.From(this).</param>
    /// <param name="message">English text describing the previous and the new identity.</param>
    void Warning(string source, string message);

    /// <summary>
    ///	Call when an operation failed and must be recorded as a failure.
    ///	Use it when one handshake cannot be processed: the packet is short, decoding
    ///	throws, or sending that response throws. The loop then waits for the next datagram.
    ///	Use it when the socket fails while waiting for a datagram: reception stops.
    ///	Use it for any other exception outside packet handling: the loop continues.
    ///	An address change, a device-id change, and a device conflict are not Error.
    /// </summary>
    /// <param name="source">Namespace and class of the caller, usually LogSource.From(this).</param>
    /// <param name="message">English failure text. Include the exception message when one was caught.</param>
    void Error(string source, string message);

    /// <summary>
    ///	Call when both registry keys hit different records: the incoming device id
    ///	already belongs to one address, and the incoming address already belongs
    ///	to another device id. Both previous records are removed and a new session
    ///	is created. This is the only Critical event in the working cycle.
    ///	The process still sends a response for the new record.
    /// </summary>
    /// <param name="source">Namespace and class of the caller, usually LogSource.From(this).</param>
    /// <param name="message">English text naming both existing records and the new identity.</param>
    void Critical(string source, string message);
}
