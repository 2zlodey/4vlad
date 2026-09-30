namespace Orkestr.Codec.Entities;

/// <summary>
///	VER response before the handler compares the version.
///	Holds the attempt id and the ten version bytes from the client.
///	Does not decide compatibility. The result handler in the engine does that.
/// </summary>
public sealed class VerResponse
{
    /// <summary>
    ///	Checks the version length and keeps a private copy of the bytes.
    /// </summary>
    /// <param name="requestId">Attempt id copied from the request.</param>
    /// <param name="version">Ten version bytes. Null and any other length are rejected.</param>
    /// <exception cref="ArgumentNullException">The version is null.</exception>
    /// <exception cref="ArgumentException">Invalid version length</exception>
    public VerResponse(uint requestId, byte[] version)
    {
        ArgumentNullException.ThrowIfNull(version);
        if (version.Length != VerLayout.VersionSize)
            throw new ArgumentException("Invalid version length");

        RequestId = requestId;
        Version = version.ToArray();
    }

    #region Properties

    /// <summary>
    ///	Attempt id, uint32. A different id does not finish the server wait.
    /// </summary>
    public uint RequestId { get; }

    /// <summary>
    ///	Version field, 10 bytes, including trailing zeros. Compared as raw bytes, not as a parsed number.
    /// </summary>
    public byte[] Version { get; }

    #endregion
}
