namespace Orkestr.Codec.Entities;

/// <summary>
///	One antenna slot from the device block, 36 bytes on the wire.
///	Stores the numbers the device sent. Range, type, and gain are not interpreted here.
///	Whether the slot is unused is a device concern. The decoder still builds all sixteen slots.
/// </summary>
public sealed class Antenna
{
    /// <summary>
    ///	Copies one antenna slot. The numbers are kept as they appeared in the packet.
    /// </summary>
    /// <param name="in">Input index from the device. The name matches the wire field.</param>
    /// <param name="from">Range start reported by the device.</param>
    /// <param name="to">Range end reported by the device.</param>
    /// <param name="polar">Polarization as a double, without a unit conversion.</param>
    /// <param name="type">Antenna type code from the device.</param>
    /// <param name="dBi">Gain code from the device.</param>
    /// <param name="direction">Direction as a double, without a unit conversion.</param>
    public Antenna(int @in, int from, int to, double polar, int type, int dBi, double direction)
    {
        In = @in;
        From = from;
        To = to;
        Polar = polar;
        Type = type;
        DBi = dBi;
        Direction = direction;
    }

    #region Properties

    /// <summary>
    ///	Input index, 4 bytes on the wire. Stored as a signed integer.
    /// </summary>
    public int In { get; }

    /// <summary>
    ///	Range start, 4 bytes on the wire. The decoder does not check that it precedes <see cref="To"/>.
    /// </summary>
    public int From { get; }

    /// <summary>
    ///	Range end, 4 bytes on the wire.
    /// </summary>
    public int To { get; }

    /// <summary>
    ///	Polarization, 8 bytes little-endian on the wire.
    /// </summary>
    public double Polar { get; }

    /// <summary>
    ///	Type code, 4 bytes on the wire.
    /// </summary>
    public int Type { get; }

    /// <summary>
    ///	Gain, 4 bytes on the wire.
    /// </summary>
    public int DBi { get; }

    /// <summary>
    ///	Direction, 8 bytes little-endian on the wire.
    /// </summary>
    public double Direction { get; }

    #endregion
}
