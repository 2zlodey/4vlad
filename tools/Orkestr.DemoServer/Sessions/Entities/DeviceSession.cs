namespace Orkestr.Sessions.Entities;

/// <summary>
///	Cryptographic material returned to one device in the 52-byte reply.
///	Holds the key, nonce, and the UTC Unix time in milliseconds taken when the session was created.
///	The bytes are not written again. Keeping a session means passing this same object into the next record.
/// </summary>
public sealed class DeviceSession
{
    /// <summary>
    ///	Stores the material the factory just filled. The arrays are not replaced later.
    /// </summary>
    /// <param name="key">32-byte key sent to the device. It does not encrypt a later packet.</param>
    /// <param name="nonce">12-byte nonce sent to the device beside the key.</param>
    /// <param name="timestamp">Unix time in milliseconds, UTC, taken when the session was created.</param>
    /// <exception cref="ArgumentNullException">The key or the nonce is null.</exception>
    public DeviceSession(byte[] key, byte[] nonce, long timestamp)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(nonce);

        Key = key;
        Nonce = nonce;
        Timestamp = timestamp;
    }

    #region Properties

    /// <summary>
    ///	Session key, 32 bytes. Filled by the factory and sent to the device in the response.
    ///	It is not written into this session again.
    /// </summary>
    public byte[] Key { get; }

    /// <summary>
    ///	Session nonce, 12 bytes. Filled once, beside the key, and sent in the same response.
    /// </summary>
    public byte[] Nonce { get; }

    /// <summary>
    ///	Unix time in milliseconds, UTC, captured when this session was created.
    /// </summary>
    public long Timestamp { get; }

    #endregion
}
