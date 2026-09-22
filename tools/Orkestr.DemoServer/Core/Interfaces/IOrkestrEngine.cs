namespace Orkestr.Core.Interfaces;

/// <summary>
///	Runs the process loop: bind, receive one datagram at a time, and stop on cancellation.
///	Does not describe how a device is found or how the 52-byte body is laid out.
///	Those steps belong to the session provider and the codec behind the engine.
/// </summary>
public interface IOrkestrEngine
{
    /// <summary>
    ///	Binds the listen socket and handles datagrams until the token is cancelled or the
    ///	socket fails while waiting. A bind failure escapes to the caller.
    ///	Cancellation closes the socket and returns without a UDP error line.
    /// </summary>
    /// <param name="cancellationToken">Token cancelled by SIGINT or SIGTERM.</param>
    /// <returns>A task that completes when reception stops or the bind fails.</returns>
    Task RunAsync(CancellationToken cancellationToken);
}
