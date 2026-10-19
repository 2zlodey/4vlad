namespace Orkestr.Codec.Entities;

/// <summary>
///	Sizes and offsets of the VER frame. Little-endian, no padding.
///	The request is command 0x01. The response is command 0x01 and ten version bytes.
///	Handshake sizes stay in <see cref="WireLayout"/>. This type does not open a socket.
/// </summary>
public static class VerLayout
{
    #region Sizes

    /// <summary>
    ///	Request length in bytes. The opcode only.
    /// </summary>
    public const int RequestSize = 1;

    /// <summary>
    ///	Successful response length in bytes. One opcode and ten version bytes.
    /// </summary>
    public const int ResponseSize = 11;

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
    ///	Offset of the command byte in both request and response.
    /// </summary>
    public const int CommandOffset = 0;

    /// <summary>
    ///	Offset of the ten version bytes in the response.
    /// </summary>
    public const int VersionOffset = 1;

    #endregion
}
