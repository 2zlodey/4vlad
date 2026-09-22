namespace Orkestr.Logging.Entities;

/// <summary>
///	Importance of one log record, ordered from least to most important.
///	The logger keeps a record when its numeric value is greater than or equal to the
///	process threshold. Zero admits every level. Five admits only a device conflict.
///	A channel does not apply a second threshold of its own.
/// </summary>
public enum LogLevel
{
    /// <summary>
    ///	Finest diagnostic step, numeric value 0. The handshake loop does not emit it.
    /// </summary>
    Trace = 0,

    /// <summary>
    ///	Diagnostic detail for one packet or one lookup, numeric value 1.
    ///	The handshake loop does not emit it. Key and nonce stay at Information.
    /// </summary>
    Debug = 1,

    /// <summary>
    ///	Normal operator path, numeric value 2: listen, receive, registry, send, and stop.
    /// </summary>
    Information = 2,

    /// <summary>
    ///	Identity change that the operator must see, numeric value 3.
    ///	Used when a device id moves address, or an address presents a new device id.
    /// </summary>
    Warning = 3,

    /// <summary>
    ///	A failed operation, numeric value 4. Handshake and socket failures use this level.
    ///	An address change and a device conflict are not errors.
    /// </summary>
    Error = 4,

    /// <summary>
    ///	Both registry keys hit different records, numeric value 5.
    ///	This is the only critical event in the working cycle.
    /// </summary>
    Critical = 5
}
