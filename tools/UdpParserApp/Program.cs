using System;
using System.Buffers.Binary;
using System.Net;
using System.Net.Sockets;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Threading.Tasks;

namespace UdpHandshakeParser
{
    // Аналог parseAntenna()
    public class Antenna
    {
        public int In { get; set; }
        public int From { get; set; }
        public int To { get; set; }
        public double Polar { get; set; }
        public int Type { get; set; }

        [JsonPropertyName("dBi")]
        public int DBi { get; set; }

        public double Direction { get; set; }
    }

    // Аналог parseDevice()
    public class Device
    {
        public int Id { get; set; }
        public double Version { get; set; }
        public Coordinates Coord { get; set; } = new();
        public Antenna[] Rfin { get; set; } = new Antenna[16];
    }

    public class Coordinates
    {
        public double Lon { get; set; }
        public double Lat { get; set; }
    }

    // Аналог parseHandShake()
    public class HandShake
    {
        public uint Cnt { get; set; }
        public long TSS { get; set; }
        public long TSN { get; set; }
        public ushort Port { get; set; }
        public Device Dev { get; set; } = new();
    }

    public static class PacketParser
    {
        private const int ExpectedSize = 626;

        // Чтение Int32 little-endian
        private static int ReadInt32LE(byte[] buffer, int offset)
        {
            return BinaryPrimitives.ReadInt32LittleEndian(
                buffer.AsSpan(offset, 4));
        }

        // Чтение UInt32 little-endian
        private static uint ReadUInt32LE(byte[] buffer, int offset)
        {
            return BinaryPrimitives.ReadUInt32LittleEndian(
                buffer.AsSpan(offset, 4));
        }

        // Чтение UInt16 little-endian
        private static ushort ReadUInt16LE(byte[] buffer, int offset)
        {
            return BinaryPrimitives.ReadUInt16LittleEndian(
                buffer.AsSpan(offset, 2));
        }

        // Чтение Int64 little-endian
        private static long ReadInt64LE(byte[] buffer, int offset)
        {
            return BinaryPrimitives.ReadInt64LittleEndian(
                buffer.AsSpan(offset, 8));
        }

        // Аналог buffer.readDoubleLE()
        private static double ReadDoubleLE(byte[] buffer, int offset)
        {
            long bits = ReadInt64LE(buffer, offset);
            return BitConverter.Int64BitsToDouble(bits);
        }

        private static Antenna ParseAntenna(byte[] buffer, int offset)
        {
            return new Antenna
            {
                In = ReadInt32LE(buffer, offset),
                From = ReadInt32LE(buffer, offset + 4),
                To = ReadInt32LE(buffer, offset + 8),
                Polar = ReadDoubleLE(buffer, offset + 12),
                Type = ReadInt32LE(buffer, offset + 20),
                DBi = ReadInt32LE(buffer, offset + 24),
                Direction = ReadDoubleLE(buffer, offset + 28)
            };
        }

        private static Device ParseDevice(byte[] buffer, int offset)
        {
            var device = new Device
            {
                Id = ReadInt32LE(buffer, offset),
                Version = ReadDoubleLE(buffer, offset + 4),
                Coord = new Coordinates
                {
                    Lon = ReadDoubleLE(buffer, offset + 12),
                    Lat = ReadDoubleLE(buffer, offset + 20)
                }
            };

            const int antennasOffset = 28;
            const int antennaSize = 36;

            for (int i = 0; i < 16; i++)
            {
                int antennaOffset =
                    offset + antennasOffset + i * antennaSize;

                device.Rfin[i] = ParseAntenna(buffer, antennaOffset);
            }

            return device;
        }

        public static HandShake ParseHandShake(byte[] buffer)
        {
            if (buffer.Length < ExpectedSize)
            {
                throw new Exception(
                    $"Слишком короткий пакет: {buffer.Length} байт, " +
                    $"ожидалось минимум {ExpectedSize}");
            }

            return new HandShake
            {
                Cnt = ReadUInt32LE(buffer, 0),
                TSS = ReadInt64LE(buffer, 4),
                TSN = ReadInt64LE(buffer, 12),
                Port = ReadUInt16LE(buffer, 20),
                Dev = ParseDevice(buffer, 22)
            };
        }
    }

    internal class Program
    {
        private const string HOST = "0.0.0.0";
        private const int PORT = 2222;

        private static async Task Main()
        {
            using var socket = new UdpClient(
                new IPEndPoint(IPAddress.Any, PORT));

            Console.WriteLine(
                $"UDP-сервер слушает {HOST}:{PORT}");

            while (true)
            {
                try
                {
                    UdpReceiveResult result =
                        await socket.ReceiveAsync();

                    byte[] buffer = result.Buffer;
                    IPEndPoint remote = result.RemoteEndPoint;

                    // Аналог:
                    // socket.send(buffer, 0, 2, 3333, remote.address)
                    //
                    // В Node.js отправляются первые 2 байта.
                    await socket.SendAsync(
                        buffer.AsMemory(0, Math.Min(2, buffer.Length)),
                        new IPEndPoint(remote.Address, 3333));

                    Console.WriteLine(
                        $"Получено {buffer.Length} байт от " +
                        $"{remote.Address}:{remote.Port}");

                    try
                    {
                        HandShake handshake =
                            PacketParser.ParseHandShake(buffer);

                        var jsonOptions = new JsonSerializerOptions
                        {
                            WriteIndented = true
                        };

                        string json = JsonSerializer.Serialize(
                            handshake, jsonOptions);

                        Console.WriteLine(json);
                    }
                    catch (Exception ex)
                    {
                        Console.WriteLine(
                            $"Ошибка разбора HandShake: {ex.Message}");
                    }
                }
                catch (SocketException ex)
                {
                    Console.WriteLine($"Ошибка UDP: {ex.Message}");
                    break;
                }
                catch (Exception ex)
                {
                    Console.WriteLine($"Ошибка: {ex.Message}");
                }
            }
        }
    }
}