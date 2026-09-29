namespace Orkestr.Codec.Entities;

/// <summary>
///	Geographic point carried inside the device block.
///	Holds longitude and latitude as the doubles from the packet.
///	Does not check that the point lies on Earth. The decoder does not reject a zero point.
/// </summary>
public sealed class Coordinates
{
    /// <summary>
    ///	Stores the two doubles from the packed coordinate struct.
    /// </summary>
    /// <param name="lon">Longitude, little-endian float64 on the wire.</param>
    /// <param name="lat">Latitude, little-endian float64 on the wire.</param>
    public Coordinates(double lon, double lat)
    {
        Lon = lon;
        Lat = lat;
    }

    #region Properties

    /// <summary>
    ///	Longitude as sent by the device, in the device's own units.
    /// </summary>
    public double Lon { get; }

    /// <summary>
    ///	Latitude as sent by the device, in the device's own units.
    /// </summary>
    public double Lat { get; }

    #endregion
}
