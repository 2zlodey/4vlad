namespace Orkestr.Codec.Entities;

/// <summary>
///	Root of one handshake packet, 626 bytes on the wire.
///	Holds the send counter, the two device timestamps, the reply port, and the device block.
///	The reply port is where the 52-byte session is sent. It is not the UDP port of the socket
///	that delivered the datagram.
/// </summary>
public sealed class HandShake
{
    /// <summary>
    ///	Stores one decoded handshake. The device block is kept whole and replaced together
    ///	when a later packet updates the registry record.
    /// </summary>
    /// <param name="cnt">Send counter from the device. It is not checked for repeats.</param>
    /// <param name="tss">Seconds reported by the device clock. Age is not checked.</param>
    /// <param name="tsn">Microseconds reported beside <paramref name="tss"/>. Age is not checked.</param>
    /// <param name="port">UDP port that receives the 52-byte reply.</param>
    /// <param name="dev">Device block from the same packet. Null is rejected.</param>
    /// <exception cref="ArgumentNullException">The device block is null.</exception>
    public HandShake(uint cnt, long tss, long tsn, ushort port, Device dev)
    {
        ArgumentNullException.ThrowIfNull(dev);

        Cnt = cnt;
        TSS = tss;
        TSN = tsn;
        Port = port;
        Dev = dev;
    }

    #region Properties

    /// <summary>
    ///	Send counter, uint32. The device increments it before each send. Repeats are kept.
    /// </summary>
    public uint Cnt { get; }

    /// <summary>
    ///	Seconds from the device clock at the moment the handshake was built. Not refreshed later.
    /// </summary>
    public long TSS { get; }

    /// <summary>
    ///	Microseconds from the same device clock reading as <see cref="TSS"/>.
    /// </summary>
    public long TSN { get; }

    /// <summary>
    ///	Reply port, uint16. The engine sends the session here, not to the source UDP port.
    /// </summary>
    public ushort Port { get; }

    /// <summary>
    ///	Device block, 604 bytes decoded. The registry id is <see cref="Device.Id"/> inside it.
    /// </summary>
    public Device Dev { get; }

    #endregion
}
