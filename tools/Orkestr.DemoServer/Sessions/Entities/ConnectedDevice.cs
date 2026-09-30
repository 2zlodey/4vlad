using System.Net;
using Orkestr.Codec.Entities;

namespace Orkestr.Sessions.Entities;

/// <summary>
///	One device remembered by the process. The id is the handshake device id, and the
///	device address is the source IP plus the handshake reply port.
///	Holds that IP, the latest handshake, and the session that the reply will carry.
///	An ordinary update replaces the handshake and the IP on this same object and keeps the session.
/// </summary>
public sealed class ConnectedDevice
{
    /// <summary>
    ///	Stores a record that is about to enter the in-memory collection.
    /// </summary>
    /// <param name="ipAddress">Source IP of the datagram that created or replaced this record.</param>
    /// <param name="handShake">Handshake from that datagram. Its port is the device-address port.</param>
    /// <param name="session">Session that will be copied into the 52-byte reply.</param>
    /// <exception cref="ArgumentNullException">The address, handshake, or session is null.</exception>
    public ConnectedDevice(IPAddress ipAddress, HandShake handShake, DeviceSession session)
    {
        ArgumentNullException.ThrowIfNull(ipAddress);
        ArgumentNullException.ThrowIfNull(handShake);
        ArgumentNullException.ThrowIfNull(session);

        IpAddress = ipAddress;
        HandShake = handShake;
        Session = session;
    }

    #region Properties

    /// <summary>
    ///	Source IP. An ordinary update may replace it when the same record is seen again.
    ///	Together with <see cref="HandShake.Port"/> it is the device address.
    /// </summary>
    public IPAddress IpAddress { get; set; }

    /// <summary>
    ///	Latest handshake for this record. An ordinary update replaces the whole object.
    ///	The device id used for lookup is <see cref="Device.Id"/> inside it.
    /// </summary>
    public HandShake HandShake { get; set; }

    /// <summary>
    ///	Session sent in the reply. An update and an address change keep this same object.
    ///	A new device id or a conflict stores a different session on a new record.
    /// </summary>
    public DeviceSession Session { get; }

    #endregion
}
