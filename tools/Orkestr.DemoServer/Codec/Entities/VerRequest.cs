namespace Orkestr.Codec.Entities;

/// <summary>
///	VER request before it is written into five bytes.
///	Holds the attempt id. The command byte is always 0x01 and is not stored here.
///	The encoder writes the bytes. The engine chooses the id and the destination.
/// </summary>
public sealed class VerRequest
{
    /// <summary>
    ///	Stores the attempt id the response must copy.
    /// </summary>
    /// <param name="requestId">Identifier of this attempt. The engine starts at 1.</param>
    public VerRequest(uint requestId)
    {
        RequestId = requestId;
    }

    /// <summary>
    ///	Attempt id, uint32. The response is accepted only when it carries this same value.
    /// </summary>
    public uint RequestId { get; }
}
