namespace Orkestr.Codec.Entities;

/// <summary>
///	Sizes and offsets of the VER frame. Little-endian, no padding.
///	The request is the attempt id and command 0x01. The response is the same id and ten version bytes.
///	Handshake sizes stay in <see cref="WireLayout"/>. This type does not open a socket.
/// </summary>
public static class VerLayout
{
    #region Sizes

    /// <summary>
    ///	Request length in bytes. Four bytes of id and one command byte, with no arguments.
    /// </summary>
    public const int RequestSize = 5;

    /// <summary>
    ///	Successful response length in bytes. Four bytes of id and ten version bytes.
    /// </summary>
    public const int ResponseSize = 14;

    /// <summary>
    ///	Version field length in bytes. The value is ASCII text padded with zeros.
    /// </summary>
    public const int VersionSize = 10;

    /// <summary>
    ///	Command byte of VER. The request writes this value and the client accepts only this value.
    /// </summary>
    public const byte CommandCode = 0x01;

    #endregion

    #region Offsets

    /// <summary>
    ///	Offset of the uint32 attempt id from the start of the request and of the response.
    /// </summary>
    public const int RequestIdOffset = 0;

    /// <summary>
    ///	Offset of the command byte from the start of the request.
    /// </summary>
    public const int CommandOffset = 4;

    /// <summary>
    ///	Offset of the ten version bytes from the start of the response.
    /// </summary>
    public const int VersionOffset = 4;

    #endregion
}
