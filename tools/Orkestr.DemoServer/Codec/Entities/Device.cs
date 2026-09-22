namespace Orkestr.Codec.Entities;

/// <summary>
///	Device description inside a handshake, 604 bytes on the wire.
///	Holds the id, version, coordinates, and sixteen antenna slots from one packet.
///	This is not a registry record. The registry stores a connected device, which points at
///	the latest handshake of this shape.
/// </summary>
public sealed class Device
{
    /// <summary>
    ///	Stores one device block. The antenna array is copied so a later edit of the caller's
    ///	array cannot change the slot count.
    /// </summary>
    /// <param name="id">Device id used as one registry key.</param>
    /// <param name="version">Version double from the packet. It is not checked against a supported range.</param>
    /// <param name="coord">Longitude and latitude. Null is rejected.</param>
    /// <param name="rfin">Sixteen antenna slots. A missing slot or a different count is rejected.</param>
    /// <exception cref="ArgumentNullException">Coordinates or the antenna array is null.</exception>
    /// <exception cref="ArgumentException">The antenna array does not contain sixteen slots.</exception>
    public Device(int id, double version, Coordinates coord, Antenna[] rfin)
    {
        ArgumentNullException.ThrowIfNull(coord);
        ArgumentNullException.ThrowIfNull(rfin);
        if (rfin.Length != WireLayout.AntennaCount || rfin.Any(antenna => antenna is null))
            throw new ArgumentException(
                $"Antenna count must be {WireLayout.AntennaCount}.",
                nameof(rfin));

        Id = id;
        Version = version;
        Coord = coord;
        Rfin = rfin.ToArray();
    }

    #region Properties

    /// <summary>
    ///	Device id, signed 32-bit. Registry lookup compares this value.
    /// </summary>
    public int Id { get; }

    /// <summary>
    ///	Device version, float64 on the wire. No compatibility check is applied.
    /// </summary>
    public double Version { get; }

    /// <summary>
    ///	Coordinates from this packet.
    /// </summary>
    public Coordinates Coord { get; }

    /// <summary>
    ///	Sixteen antenna slots in wire order, index 0 first. Empty slots are still present.
    /// </summary>
    public Antenna[] Rfin { get; }

    #endregion
}
