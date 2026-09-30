namespace Orkestr.Core.Entities;

/// <summary>
///	Result of checking one VER response.
///	None means the application channel can be used. IncompatibleVersion stops this command.
///	The engine logs the result. A firmware update is not started from either value.
/// </summary>
public enum VerStatus
{
    /// <summary>
    ///	The version bytes equal the constant. The log line is Application channel is ready.
    /// </summary>
    None,

    /// <summary>
    ///	The version bytes differ from the constant. The log token is INCOMPATIBLE_VERSION.
    /// </summary>
    IncompatibleVersion
}
