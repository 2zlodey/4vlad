using System.Net;
using Orkestr.Codec.Entities;
using Orkestr.Logging.Implementation;
using Orkestr.Logging.Interfaces;
using Orkestr.Sessions.Entities;
using Orkestr.Sessions.Interfaces;

namespace Orkestr.Sessions.Implementation;

/// <summary>
///	Process-memory registry of connected devices. Lookup is a linear scan, and one caller
///	updates the list: the engine loop. There is no lock and no disk copy.
///	Applies exactly one of the five identity rules and logs that rule in English.
///	Does not open a socket and does not build the reply bytes. The engine does that with the returned record.
/// </summary>
public sealed class InMemorySessionProvider : ISessionProvider
{
    #region Fields

    private readonly List<ConnectedDevice> _devices = new();
    private readonly ISessionFactory _factory;
    private readonly ILogger _logger;

    #endregion

    /// <summary>
    ///	Stores the factory that creates keys and the logger that receives registry lines.
    /// </summary>
    /// <param name="factory">Source of a new session. Called only for a new id, a replaced id, or a conflict.</param>
    /// <param name="logger">Logger for the five outcome texts. The source string is this provider's type name.</param>
    /// <exception cref="ArgumentNullException">The factory or the logger is null.</exception>
    public InMemorySessionProvider(ISessionFactory factory, ILogger logger)
    {
        ArgumentNullException.ThrowIfNull(factory);
        ArgumentNullException.ThrowIfNull(logger);

        _factory = factory;
        _logger = logger;
    }

    /// <summary>
    ///	How many records are stored. Silent records stay until the process exits.
    /// </summary>
    public int Count => _devices.Count;

    /// <summary>
    ///	Finds or creates a device record and applies one of the five registry rules.
    ///	Logs the outcome. Returns the record the engine sends the response to.
    /// </summary>
    /// <param name="sourceAddress">IP of the socket that sent the datagram.</param>
    /// <param name="handshake">Decoded packet. The reply port is read from <see cref="HandShake.Port"/>.</param>
    /// <returns>The outcome and the record already stored in the collection.</returns>
    /// <exception cref="ArgumentNullException">The address or the handshake is null.</exception>
    public RegistryResult Apply(IPAddress sourceAddress, HandShake handshake)
    {
        ArgumentNullException.ThrowIfNull(sourceAddress);
        ArgumentNullException.ThrowIfNull(handshake);

        var byId = _devices.Find(device => device.HandShake.Dev.Id == handshake.Dev.Id);
        var byAddress = _devices.Find(device =>
            device.IpAddress.Equals(sourceAddress) &&
            device.HandShake.Port == handshake.Port);

        // No record has this id or this device address.
        if (byId is null && byAddress is null)
        {
            var created = Remember(sourceAddress, handshake, _factory.Create());
            _logger.Information(LogSource.From(this), $"+ New device: ID={handshake.Dev.Id}");
            return new RegistryResult(RegistryOutcome.NewDevice, created);
        }

        // The id and the device address are the same list object. The session stays.
        if (byId is not null && byAddress == byId)
        {
            byId.HandShake = handshake;
            byId.IpAddress = sourceAddress;
            _logger.Information(LogSource.From(this), $"+ Device updated: ID={handshake.Dev.Id}");
            return new RegistryResult(RegistryOutcome.Updated, byId);
        }

        // The id moved. Drop the old record and keep its session object.
        if (byId is not null && byAddress is null)
        {
            Warn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            Warn("!!! OPERATOR WARNING !!!");
            Warn("!!! DEVICE ADDRESS CHANGED !!!");
            Warn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            Warn($"! DeviceId: {handshake.Dev.Id}");
            Warn($"! Previous address: {byId.IpAddress}:{byId.HandShake.Port}");
            Warn($"! New address: {sourceAddress}:{handshake.Port}");
            Warn("! Previous record will be removed.");

            var session = byId.Session;
            _devices.Remove(byId);
            var moved = Remember(sourceAddress, handshake, session);

            Warn("! Previous record removed.");
            return new RegistryResult(RegistryOutcome.AddressChanged, moved);
        }

        // The address now names a different id. Drop the old session and create another.
        if (byId is null && byAddress is not null)
        {
            Warn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            Warn("!!! OPERATOR WARNING !!!");
            Warn("!!! DEVICE ID CHANGED !!!");
            Warn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
            Warn($"Address: {sourceAddress}:{handshake.Port}");
            Warn($"Previous DeviceId: {byAddress.HandShake.Dev.Id}");
            Warn($"New DeviceId: {handshake.Dev.Id}");
            Warn("Previous record will be removed.");

            _devices.Remove(byAddress);
            var replaced = Remember(sourceAddress, handshake, _factory.Create());

            Warn("Previous record removed.");
            return new RegistryResult(RegistryOutcome.DeviceIdChanged, replaced);
        }

        // The id and the address point at two different records. Drop both.
        WarnCritical("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        WarnCritical("!!! CRITICAL WARNING !!!");
        WarnCritical("!!! DEVICE CONFLICT !!!");
        WarnCritical("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
        WarnCritical($"New DeviceId: {handshake.Dev.Id}");
        WarnCritical($"New address: {sourceAddress}:{handshake.Port}");
        WarnCritical($"Existing DeviceId found at address: {byId!.IpAddress}:{byId.HandShake.Port}");
        WarnCritical($"Existing address is used by DeviceId: {byAddress!.HandShake.Dev.Id}");
        WarnCritical("Previous records will be removed.");

        _devices.Remove(byId);
        _devices.Remove(byAddress!);
        var resolved = Remember(sourceAddress, handshake, _factory.Create());

        WarnCritical("Previous records removed.");
        return new RegistryResult(RegistryOutcome.Conflict, resolved);

        void Warn(string message) => _logger.Warning(LogSource.From(this), message);
        void WarnCritical(string message) => _logger.Critical(LogSource.From(this), message);
    }

    /// <summary>
    ///	Appends a record and returns it so the caller can hand that same object back to the engine.
    /// </summary>
    /// <param name="sourceAddress">Source IP stored on the record.</param>
    /// <param name="handshake">Handshake stored on the record.</param>
    /// <param name="session">Session stored on the record. It may be a session created earlier.</param>
    /// <returns>The record now sitting at the end of the collection.</returns>
    private ConnectedDevice Remember(IPAddress sourceAddress, HandShake handshake, DeviceSession session)
    {
        var device = new ConnectedDevice(sourceAddress, handshake, session);
        _devices.Add(device);
        return device;
    }
}
