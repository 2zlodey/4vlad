using System.Security.Cryptography;
using Orkestr.Sessions.Entities;
using Orkestr.Sessions.Interfaces;

namespace Orkestr.Sessions.Implementation;

/// <summary>
///	Fills a new session from the cryptographic random generator and the UTC clock.
///	Owns the only place that writes a key and a nonce. Lengths are constructor arguments,
///	so this type does not reference the codec layout.
///	Does not remember the session. The provider stores the object it returns.
/// </summary>
public sealed class CryptographicSessionFactory : ISessionFactory
{
    #region Fields

    private readonly int _keySize;
    private readonly int _nonceSize;

    #endregion

    /// <summary>
    ///	Remembers the byte counts the engine copied from the wire layout.
    /// </summary>
    /// <param name="keySize">Key length in bytes. Zero and negative values are rejected.</param>
    /// <param name="nonceSize">Nonce length in bytes. Zero and negative values are rejected.</param>
    /// <exception cref="ArgumentOutOfRangeException">A length is not positive.</exception>
    public CryptographicSessionFactory(int keySize, int nonceSize)
    {
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(keySize);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(nonceSize);

        _keySize = keySize;
        _nonceSize = nonceSize;
    }

    /// <summary>
    ///	Allocates both buffers, fills them, and stamps <see cref="DateTimeOffset.UtcNow"/> in milliseconds.
    ///	The buffers are not encrypted payload. They are the material the reply will carry.
    /// </summary>
    /// <returns>A new session. Each call returns a different key and nonce.</returns>
    public DeviceSession Create()
    {
        var key = new byte[_keySize];
        var nonce = new byte[_nonceSize];
        RandomNumberGenerator.Fill(key);
        RandomNumberGenerator.Fill(nonce);
        return new DeviceSession(key, nonce, DateTimeOffset.UtcNow.ToUnixTimeMilliseconds());
    }
}
