using System.Buffers.Binary;
using Orkestr.Codec.Entities;
using Orkestr.Codec.Interfaces;

namespace Orkestr.Codec.Implementation;

/// <summary>
///	Decodes a packed little-endian handshake into an object tree.
///	Reads the header, the device block, and sixteen antenna slots from the first 626 bytes.
///	Does not score coordinates, version, counter, or clock fields, and does not log success or failure.
/// </summary>
public sealed class HandshakeDecoder : IHandshakeDecoder
{
    /// <summary>
    ///	Reads the first 626 bytes. A longer buffer's tail is ignored.
    ///	The field order matches the device struct: counter, two timestamps, reply port, then device.
    /// </summary>
    /// <param name="payload">Datagram body.</param>
    /// <returns>The handshake stored at the start of the buffer.</returns>
    /// <exception cref="ArgumentNullException">The payload is null.</exception>
    /// <exception cref="ArgumentException">Packet too short: {length} bytes, expected at least 626</exception>
    public HandShake Decode(byte[] payload)
    {
        ArgumentNullException.ThrowIfNull(payload);
        if (payload.Length < WireLayout.HandshakeSize)
        {
            throw new ArgumentException(
                $"Packet too short: {payload.Length} bytes, expected at least {WireLayout.HandshakeSize}");
        }

        var device = ReadDevice(payload, WireLayout.DeviceOffset);
        return new HandShake(
            ReadUInt32(payload, WireLayout.CntOffset),
            ReadInt64(payload, WireLayout.TssOffset),
            ReadInt64(payload, WireLayout.TsnOffset),
            ReadUInt16(payload, WireLayout.PortOffset),
            device);
    }

    #region Private methods

    /// <summary>
    ///	Reads the device id, version, coordinates, and sixteen antennas from the device block.
    /// </summary>
    /// <param name="payload">Datagram that is already known to hold a full handshake.</param>
    /// <param name="offset">Absolute offset of the device block, 22 for a normal packet.</param>
    /// <returns>A device with exactly sixteen antenna objects.</returns>
    private static Device ReadDevice(byte[] payload, int offset)
    {
        var antennas = new Antenna[WireLayout.AntennaCount];
        for (var index = 0; index < WireLayout.AntennaCount; index++)
        {
            var antennaOffset = offset + WireLayout.RfinOffset + index * WireLayout.AntennaSize;
            antennas[index] = ReadAntenna(payload, antennaOffset);
        }

        return new Device(
            ReadInt32(payload, offset + WireLayout.DeviceIdOffset),
            ReadDouble(payload, offset + WireLayout.VersionOffset),
            new Coordinates(
                ReadDouble(payload, offset + WireLayout.LonOffset),
                ReadDouble(payload, offset + WireLayout.LatOffset)),
            antennas);
    }

    /// <summary>
    ///	Reads one 36-byte antenna. The numbers are stored and not interpreted.
    /// </summary>
    /// <param name="payload">Datagram that is already known to hold a full handshake.</param>
    /// <param name="offset">Absolute offset of this antenna slot.</param>
    /// <returns>The antenna at that offset.</returns>
    private static Antenna ReadAntenna(byte[] payload, int offset)
    {
        return new Antenna(
            ReadInt32(payload, offset + WireLayout.InOffset),
            ReadInt32(payload, offset + WireLayout.FromOffset),
            ReadInt32(payload, offset + WireLayout.ToOffset),
            ReadDouble(payload, offset + WireLayout.PolarOffset),
            ReadInt32(payload, offset + WireLayout.TypeOffset),
            ReadInt32(payload, offset + WireLayout.DbiOffset),
            ReadDouble(payload, offset + WireLayout.DirectionOffset));
    }

    /// <summary>
    ///	Reads a signed 32-bit integer, little-endian.
    /// </summary>
    /// <param name="payload">Source buffer.</param>
    /// <param name="offset">First byte of the integer.</param>
    /// <returns>The integer at that offset.</returns>
    private static int ReadInt32(byte[] payload, int offset)
    {
        return BinaryPrimitives.ReadInt32LittleEndian(payload.AsSpan(offset, sizeof(int)));
    }

    /// <summary>
    ///	Reads an unsigned 32-bit integer, little-endian.
    /// </summary>
    /// <param name="payload">Source buffer.</param>
    /// <param name="offset">First byte of the integer.</param>
    /// <returns>The integer at that offset.</returns>
    private static uint ReadUInt32(byte[] payload, int offset)
    {
        return BinaryPrimitives.ReadUInt32LittleEndian(payload.AsSpan(offset, sizeof(uint)));
    }

    /// <summary>
    ///	Reads an unsigned 16-bit integer, little-endian.
    /// </summary>
    /// <param name="payload">Source buffer.</param>
    /// <param name="offset">First byte of the integer.</param>
    /// <returns>The integer at that offset.</returns>
    private static ushort ReadUInt16(byte[] payload, int offset)
    {
        return BinaryPrimitives.ReadUInt16LittleEndian(payload.AsSpan(offset, sizeof(ushort)));
    }

    /// <summary>
    ///	Reads a signed 64-bit integer, little-endian.
    /// </summary>
    /// <param name="payload">Source buffer.</param>
    /// <param name="offset">First byte of the integer.</param>
    /// <returns>The integer at that offset.</returns>
    private static long ReadInt64(byte[] payload, int offset)
    {
        return BinaryPrimitives.ReadInt64LittleEndian(payload.AsSpan(offset, sizeof(long)));
    }

    /// <summary>
    ///	Reads a float64 as a little-endian int64 bit pattern, independent of host endianness.
    /// </summary>
    /// <param name="payload">Source buffer.</param>
    /// <param name="offset">First byte of the double.</param>
    /// <returns>The double at that offset.</returns>
    private static double ReadDouble(byte[] payload, int offset)
    {
        var bits = ReadInt64(payload, offset);
        return BitConverter.Int64BitsToDouble(bits);
    }

    #endregion
}
