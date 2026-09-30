namespace Orkestr.Sessions.Entities;

/// <summary>
///	What one registry update did, and which record should receive the 52-byte reply.
///	The device is already stored in the provider's collection when this object is returned.
///	The engine does not search again. It encodes the session on this record.
/// </summary>
public sealed class RegistryResult
{
    /// <summary>
    ///	Pairs an outcome with the record that remains in the collection.
    /// </summary>
    /// <param name="outcome">Rule that was applied.</param>
    /// <param name="device">Record now stored in the collection. Null is rejected.</param>
    /// <exception cref="ArgumentNullException">The device is null.</exception>
    public RegistryResult(RegistryOutcome outcome, ConnectedDevice device)
    {
        ArgumentNullException.ThrowIfNull(device);

        Outcome = outcome;
        Device = device;
    }

    #region Properties

    /// <summary>
    ///	Rule that accepted the handshake. Every value still receives a reply.
    /// </summary>
    public RegistryOutcome Outcome { get; }

    /// <summary>
    ///	Record that should receive the reply. Its session is the one the engine encodes.
    /// </summary>
    public ConnectedDevice Device { get; }

    #endregion
}
