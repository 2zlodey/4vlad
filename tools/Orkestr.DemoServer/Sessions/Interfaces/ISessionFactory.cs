using Orkestr.Sessions.Entities;

namespace Orkestr.Sessions.Interfaces;

/// <summary>
///	Creates one new session: key, nonce, and the current UTC time.
///	Does not store the session and does not know which device will receive it.
///	The provider calls this only when a rule needs a new session.
/// </summary>
public interface ISessionFactory
{
    /// <summary>
    ///	Fills a new key and nonce and stamps the current UTC time in milliseconds.
    /// </summary>
    /// <returns>A session that has not been stored yet.</returns>
    DeviceSession Create();
}
