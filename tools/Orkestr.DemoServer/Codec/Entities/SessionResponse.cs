namespace Orkestr.Codec.Entities;

/// <summary>
///	Reply fields before they are written into the 52-byte buffer.
///	Holds the session key, nonce, and UTC Unix time in milliseconds.
///	Does not know the registry type that produced the session. The engine copies the fields across.
/// </summary>
public sealed class SessionResponse
{
    /// <summary>
    ///	Checks the key and nonce lengths required by the reply layout.
    /// </summary>
    /// <param name="key">32-byte session key.</param>
    /// <param name="nonce">12-byte session nonce.</param>
    /// <param name="timestamp">Unix time in milliseconds, UTC, signed 64-bit.</param>
    /// <exception cref="ArgumentNullException">The key or the nonce is null.</exception>
    /// <exception cref="ArgumentException">Invalid key or nonce length</exception>
    public SessionResponse(byte[] key, byte[] nonce, long timestamp)
    {
        ArgumentNullException.ThrowIfNull(key);
        ArgumentNullException.ThrowIfNull(nonce);
        if (key.Length != WireLayout.KeySize || nonce.Length != WireLayout.NonceSize)
            throw new ArgumentException("Invalid key or nonce length");

        Key = key;
        Nonce = nonce;
        Timestamp = timestamp;
    }

    #region Properties

    /// <summary>
    ///	Session key, 32 bytes. Copied into the reply and not used to encrypt a later packet.
    /// </summary>
    public byte[] Key { get; }

    /// <summary>
    ///	Session nonce, 12 bytes. Copied into the reply after the key.
    /// </summary>
    public byte[] Nonce { get; }

    /// <summary>
    ///	Unix time in milliseconds, UTC. Written little-endian at the end of the reply.
    /// </summary>
    public long Timestamp { get; }

    #endregion
}
