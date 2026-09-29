namespace Orkestr.Sessions.Entities;

/// <summary>
///	Which registry rule accepted one handshake.
///	The engine does not branch on these values. It always replies to the record in the result.
///	The provider logs the matching text while it applies the rule.
/// </summary>
public enum RegistryOutcome
{
    /// <summary>
    ///	No record had this device id or this device address. A new session was created.
    /// </summary>
    NewDevice,

    /// <summary>
    ///	The device id and the device address named the same record. The session was kept.
    /// </summary>
    Updated,

    /// <summary>
    ///	The device id moved to a new address. The old record was removed and the session was kept.
    /// </summary>
    AddressChanged,

    /// <summary>
    ///	An occupied address presented a new device id. The old record and its session were removed.
    /// </summary>
    DeviceIdChanged,

    /// <summary>
    ///	The device id and the device address named two different records. Both were removed.
    /// </summary>
    Conflict
}
