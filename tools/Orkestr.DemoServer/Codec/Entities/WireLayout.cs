namespace Orkestr.Codec.Entities;

/// <summary>
///	Single source of packet sizes and field offsets for the handshake and the 52-byte reply.
///	Offsets are little-endian and packed, matching the device struct with pack(1).
///	Header offsets are from the first byte of the datagram. Device offsets are from the start
///	of the device block. Antenna offsets are from the start of that antenna. Reply offsets
///	are from the first byte of the response. The session factory receives only the key and
///	nonce lengths, so it does not need this type.
/// </summary>
public static class WireLayout
{
    #region Sizes

    /// <summary>
    ///	Handshake length in bytes. A shorter datagram is rejected. Extra trailing bytes are ignored.
    /// </summary>
    public const int HandshakeSize = 626;

    /// <summary>
    ///	Device block length in bytes, including sixteen antenna slots.
    /// </summary>
    public const int DeviceSize = 604;

    /// <summary>
    ///	One antenna slot length in bytes: three ints, a double, two ints, and a double.
    /// </summary>
    public const int AntennaSize = 36;

    /// <summary>
    ///	Number of antenna slots stored for every device, including slots the device left zero.
    /// </summary>
    public const int AntennaCount = 16;

    /// <summary>
    ///	Session key length in bytes. The reply copies this many bytes at the start.
    /// </summary>
    public const int KeySize = 32;

    /// <summary>
    ///	Session nonce length in bytes. The reply copies this many bytes after the key.
    /// </summary>
    public const int NonceSize = 12;

    /// <summary>
    ///	Reply timestamp length in bytes. The value is a signed 64-bit Unix time in milliseconds.
    /// </summary>
    public const int TimestampSize = 8;

    /// <summary>
    ///	Reply length in bytes. Key, nonce, and timestamp occupy the whole buffer.
    /// </summary>
    public const int ResponseSize = 52;

    #endregion

    #region Handshake offsets

    /// <summary>
    ///	Offset of the uint32 send counter from the start of the handshake.
    /// </summary>
    public const int CntOffset = 0;

    /// <summary>
    ///	Offset of the int64 second timestamp from the start of the handshake.
    /// </summary>
    public const int TssOffset = 4;

    /// <summary>
    ///	Offset of the int64 microsecond timestamp from the start of the handshake.
    /// </summary>
    public const int TsnOffset = 12;

    /// <summary>
    ///	Offset of the uint16 reply port from the start of the handshake.
    /// </summary>
    public const int PortOffset = 20;

    /// <summary>
    ///	Offset of the device block from the start of the handshake.
    /// </summary>
    public const int DeviceOffset = 22;

    #endregion

    #region Device offsets

    /// <summary>
    ///	Offset of the int32 device id from the start of the device block.
    /// </summary>
    public const int DeviceIdOffset = 0;

    /// <summary>
    ///	Offset of the float64 version from the start of the device block.
    /// </summary>
    public const int VersionOffset = 4;

    /// <summary>
    ///	Offset of the float64 longitude from the start of the device block.
    /// </summary>
    public const int LonOffset = 12;

    /// <summary>
    ///	Offset of the float64 latitude from the start of the device block.
    /// </summary>
    public const int LatOffset = 20;

    /// <summary>
    ///	Offset of the first antenna from the start of the device block.
    /// </summary>
    public const int RfinOffset = 28;

    #endregion

    #region Antenna offsets

    /// <summary>
    ///	Offset of the int32 input index from the start of one antenna.
    /// </summary>
    public const int InOffset = 0;

    /// <summary>
    ///	Offset of the int32 range start from the start of one antenna.
    /// </summary>
    public const int FromOffset = 4;

    /// <summary>
    ///	Offset of the int32 range end from the start of one antenna.
    /// </summary>
    public const int ToOffset = 8;

    /// <summary>
    ///	Offset of the float64 polarization from the start of one antenna.
    /// </summary>
    public const int PolarOffset = 12;

    /// <summary>
    ///	Offset of the int32 antenna type from the start of one antenna.
    /// </summary>
    public const int TypeOffset = 20;

    /// <summary>
    ///	Offset of the int32 gain from the start of one antenna.
    /// </summary>
    public const int DbiOffset = 24;

    /// <summary>
    ///	Offset of the float64 direction from the start of one antenna.
    /// </summary>
    public const int DirectionOffset = 28;

    #endregion

    #region Response offsets

    /// <summary>
    ///	Offset of the 32-byte key from the start of the reply.
    /// </summary>
    public const int KeyOffset = 0;

    /// <summary>
    ///	Offset of the 12-byte nonce from the start of the reply.
    /// </summary>
    public const int NonceOffset = 32;

    /// <summary>
    ///	Offset of the int64 Unix millisecond timestamp from the start of the reply.
    /// </summary>
    public const int TimestampOffset = 44;

    #endregion
}
