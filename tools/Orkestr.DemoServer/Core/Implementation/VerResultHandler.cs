using Orkestr.Codec.Entities;
using Orkestr.Core.Entities;
using Orkestr.Core.Interfaces;

namespace Orkestr.Core.Implementation;

/// <summary>
///	Compares a VER response with <see cref="SoftwareVersion"/>.
///	Returns a status and does not log, send, or change the registry.
///	A mismatch does not select another firmware image.
/// </summary>
public sealed class VerResultHandler : IVerResultHandler
{
    /// <summary>
    ///	Compares all ten version bytes, including the trailing zeros.
    /// </summary>
    /// <param name="response">Response whose attempt id was already accepted by the engine.</param>
    /// <returns><see cref="VerStatus.None"/> when the bytes match the constant.</returns>
    /// <exception cref="ArgumentNullException">The response is null.</exception>
    public VerStatus Handle(VerResponse response)
    {
        ArgumentNullException.ThrowIfNull(response);
        if (SoftwareVersion.Matches(response.Version))
            return VerStatus.None;

        return VerStatus.IncompatibleVersion;
    }
}
