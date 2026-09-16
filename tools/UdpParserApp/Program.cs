using System;
using System.Collections.Generic;
using System.Net;
using System.Net.Sockets;
using System.Security.Cryptography;
using System.Threading.Tasks;
using UdpHandshakeParser.Protocol;

namespace UdpParserApp
{
    internal class Program
    {
        private const int ListenPort = 2222;

        private const int KeySize = 32;
        private const int NonceSize = 12;

        private static readonly List<ConnectedDevice> Devices = new();

        private static async Task Main()
        {
            using var socket = new UdpClient(ListenPort);

            Console.WriteLine(
                $"UDP-сервер слушает 0.0.0.0:{ListenPort}");

            while (true)
            {
                try
                {
                    UdpReceiveResult result =
                        await socket.ReceiveAsync();

                    byte[] buffer = result.Buffer;
                    IPEndPoint remote = result.RemoteEndPoint;

                    Console.WriteLine();
                    Console.WriteLine(
                        $"Получено {buffer.Length} байт от " +
                        $"{remote.Address}:{remote.Port}");

                    try
                    {
                        HandShake handshake =
                            HandshakeParser.Parse(buffer);

                        int deviceId = handshake.Dev.Id;

                        IPAddress ipAddress = remote.Address;
                        ushort devicePort = handshake.Port;

                        Console.WriteLine(
                            $"Device ID: {deviceId}");

                        Console.WriteLine(
                            $"Device address: " +
                            $"{ipAddress}:{devicePort}");

                        // -------------------------------------------------
                        // Ищем существующее устройство по DeviceId
                        // -------------------------------------------------

                        ConnectedDevice? byId = Devices.Find(
                            d => d.HandShake.Dev.Id == deviceId);

                        // -------------------------------------------------
                        // Ищем существующее устройство по IP + Port
                        // -------------------------------------------------

                        ConnectedDevice? byAddress = Devices.Find(
                            d =>
                                d.IpAddress.Equals(ipAddress) &&
                                d.HandShake.Port == devicePort);

                        ConnectedDevice device;

                        // =================================================
                        // НОВОЕ УСТРОЙСТВО
                        //
                        // Нет ни такого ID, ни такого IP:Port.
                        // =================================================

                        if (byId == null && byAddress == null)
                        {
                            device = CreateDevice(
                                ipAddress,
                                handshake);

                            Devices.Add(device);

                            Console.WriteLine(
                                $"Новое устройство: " +
                                $"ID={deviceId}");
                        }

                        // =================================================
                        // ОБЫЧНЫЙ HANDSHAKE
                        //
                        // И ID, и IP:Port совпадают с одной записью.
                        // =================================================

                        else if (byId != null &&
                                 byAddress == byId)
                        {
                            device = byId;

                            device.HandShake = handshake;
                            device.IpAddress = ipAddress;

                            Console.WriteLine(
                                $"Устройство обновлено: " +
                                $"ID={deviceId}");
                        }

                        // =================================================
                        // КОНФЛИКТ ПО DEVICE ID
                        //
                        // Такой ID уже есть, но адрес другой.
                        //
                        // Считаем, что устройство переехало.
                        // Старую запись удаляем.
                        // Session сохраняем.
                        // =================================================

                        else if (byId != null &&
                                 byAddress == null)
                        {
                            Console.WriteLine();
                            Console.WriteLine(
                                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                            Console.WriteLine(
                                "!!! ВНИМАНИЕ ОПЕРАТОРУ !!!");
                            Console.WriteLine(
                                "!!! ИЗМЕНЕНИЕ АДРЕСА УСТРОЙСТВА !!!");
                            Console.WriteLine(
                                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

                            Console.WriteLine(
                                $"DeviceId: {deviceId}");

                            Console.WriteLine(
                                $"Старый адрес: " +
                                $"{byId.IpAddress}:" +
                                $"{byId.HandShake.Port}");

                            Console.WriteLine(
                                $"Новый адрес: " +
                                $"{ipAddress}:{devicePort}");

                            Console.WriteLine(
                                "Старая запись удаляется.");

                            DeviceSession session =
                                byId.Session;

                            Devices.Remove(byId);

                            device = new ConnectedDevice
                            {
                                IpAddress = ipAddress,
                                HandShake = handshake,
                                Session = session
                            };

                            Devices.Add(device);

                            Console.WriteLine(
                                "Старая запись удалена.");
                        }

                        // =================================================
                        // КОНФЛИКТ ПО IP + PORT
                        //
                        // Этот адрес уже принадлежит другому DeviceId.
                        //
                        // Старую запись удаляем.
                        // Для нового устройства создаём новую Session.
                        // =================================================

                        else if (byId == null &&
                                 byAddress != null)
                        {
                            Console.WriteLine();
                            Console.WriteLine(
                                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                            Console.WriteLine(
                                "!!! ВНИМАНИЕ ОПЕРАТОРУ !!!");
                            Console.WriteLine(
                                "!!! ИЗМЕНЕНИЕ DEVICE ID !!!");
                            Console.WriteLine(
                                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

                            Console.WriteLine(
                                $"Адрес: " +
                                $"{ipAddress}:{devicePort}");

                            Console.WriteLine(
                                $"Старый DeviceId: " +
                                $"{byAddress.HandShake.Dev.Id}");

                            Console.WriteLine(
                                $"Новый DeviceId: " +
                                $"{deviceId}");

                            Console.WriteLine(
                                "Старая запись удаляется.");

                            Devices.Remove(byAddress);

                            device = CreateDevice(
                                ipAddress,
                                handshake);

                            Devices.Add(device);

                            Console.WriteLine(
                                "Старая запись удалена.");
                        }

                        // =================================================
                        // КОНФЛИКТ ПО ОБОИМ ПРИЗНАКАМ
                        //
                        // byId и byAddress существуют,
                        // но это разные записи.
                        //
                        // Удаляем обе старые записи.
                        // Создаём новую.
                        // =================================================

                        else
                        {
                            Console.WriteLine();
                            Console.WriteLine(
                                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
                            Console.WriteLine(
                                "!!! КРИТИЧЕСКОЕ ПРЕДУПРЕЖДЕНИЕ !!!");
                            Console.WriteLine(
                                "!!! КОНФЛИКТ УСТРОЙСТВ !!!");
                            Console.WriteLine(
                                "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");

                            Console.WriteLine(
                                $"Новый DeviceId: {deviceId}");

                            Console.WriteLine(
                                $"Новый адрес: " +
                                $"{ipAddress}:{devicePort}");

                            Console.WriteLine(
                                $"Существующий DeviceId найден по адресу: " +
                                $"{byId!.IpAddress}:" +
                                $"{byId.HandShake.Port}");

                            Console.WriteLine(
                                $"Существующий адрес используется DeviceId: " +
                                $"{byAddress!.HandShake.Dev.Id}");

                            Console.WriteLine(
                                "Старые записи удаляются.");

                            Devices.Remove(byId);
                            Devices.Remove(byAddress);

                            device = CreateDevice(
                                ipAddress,
                                handshake);

                            Devices.Add(device);

                            Console.WriteLine(
                                "Старые записи удалены.");
                        }

                        // -------------------------------------------------
                        // Отправляем ответ
                        // -------------------------------------------------

                        byte[] response = CreateResponse(
                            device.Session);

                        ushort responsePort =
                            device.HandShake.Port;

                        await socket.SendAsync(
                            response,
                            response.Length,
                            new IPEndPoint(
                                device.IpAddress,
                                responsePort));

                        Console.WriteLine();
                        Console.WriteLine(
                            $"Отправлено {response.Length} байт на " +
                            $"{device.IpAddress}:{responsePort}");

                        // -------------------------------------------------
                        // Информация
                        // -------------------------------------------------

                        Console.WriteLine(
                            $"Key: " +
                            $"{Convert.ToHexString(device.Session.Key)}");

                        Console.WriteLine(
                            $"Nonce: " +
                            $"{Convert.ToHexString(device.Session.Nonce)}");

                        Console.WriteLine(
                            $"Timestamp: " +
                            $"{device.Session.Timestamp}");

                        Console.WriteLine();
                        Console.WriteLine("HandShake:");

                        Console.WriteLine(
                            $"  Cnt:  {device.HandShake.Cnt}");

                        Console.WriteLine(
                            $"  TSS:  {device.HandShake.TSS}");

                        Console.WriteLine(
                            $"  TSN:  {device.HandShake.TSN}");

                        Console.WriteLine(
                            $"  Port: {device.HandShake.Port}");

                        Console.WriteLine();
                        Console.WriteLine("Device:");

                        Console.WriteLine(
                            $"  Id:       {device.HandShake.Dev.Id}");

                        Console.WriteLine(
                            $"  Version:  {device.HandShake.Dev.Version}");

                        Console.WriteLine(
                            $"  Lon:      {device.HandShake.Dev.Coord.Lon}");

                        Console.WriteLine(
                            $"  Lat:      {device.HandShake.Dev.Coord.Lat}");

                        Console.WriteLine(
                            $"  Antennas: " +
                            $"{device.HandShake.Dev.Rfin.Length}");

                        Console.WriteLine(
                            $"  IP:       {device.IpAddress}");

                        Console.WriteLine();
                        Console.WriteLine(
                            $"Всего устройств: {Devices.Count}");
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine(
                            $"Ошибка обработки HandShake: " +
                            $"{ex.Message}");
                    }
                }
                catch (SocketException ex)
                {
                    Console.WriteLine(
                        $"Ошибка UDP: {ex.Message}");

                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine(
                        $"Ошибка: {ex.Message}");
                }
            }
        }

        // =============================================================
        // Создание ConnectedDevice
        // =============================================================

        private static ConnectedDevice CreateDevice(
            IPAddress ipAddress,
            HandShake handshake)
        {
            return new ConnectedDevice
            {
                IpAddress = ipAddress,
                HandShake = handshake,
                Session = CreateSession()
            };
        }

        // =============================================================
        // Создание криптографической сессии
        // =============================================================

        private static DeviceSession CreateSession()
        {
            byte[] key = new byte[KeySize];
            byte[] nonce = new byte[NonceSize];

            RandomNumberGenerator.Fill(key);
            RandomNumberGenerator.Fill(nonce);

            return new DeviceSession
            {
                Key = key,
                Nonce = nonce,
                Timestamp =
                    DateTimeOffset.UtcNow.ToUnixTimeMilliseconds()
            };
        }

        // =============================================================
        // Формирование ответа
        //
        // 32 bytes Key
        // 12 bytes Nonce
        //  8 bytes Timestamp
        //
        // Итого 52 байта.
        // =============================================================

        private static byte[] CreateResponse(
            DeviceSession session)
        {
            byte[] response = new byte[
                KeySize +
                NonceSize +
                sizeof(long)];

            Buffer.BlockCopy(
                session.Key,
                0,
                response,
                0,
                KeySize);

            Buffer.BlockCopy(
                session.Nonce,
                0,
                response,
                KeySize,
                NonceSize);

            Buffer.BlockCopy(
                BitConverter.GetBytes(session.Timestamp),
                0,
                response,
                KeySize + NonceSize,
                sizeof(long));

            return response;
        }
    }

    // =============================================================
    // Устройство
    // =============================================================

    public class ConnectedDevice
    {
        public IPAddress IpAddress { get; set; } = IPAddress.None;

        public HandShake HandShake { get; set; } = new();

        public DeviceSession Session { get; set; } = new();
    }

    // =============================================================
    // Криптографическая сессия
    // =============================================================

    public class DeviceSession
    {
        public byte[] Key { get; set; } = Array.Empty<byte>();

        public byte[] Nonce { get; set; } = Array.Empty<byte>();

        public long Timestamp { get; set; }
    }
}
