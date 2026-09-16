using System;
using System.Buffers.Binary;

namespace UdpHandshakeParser.Protocol
{
    public class Antenna
    {
        public int In { get; set; }
        public int From { get; set; }
        public int To { get; set; }
        public double Polar { get; set; }
        public int Type { get; set; }
        public int DBi { get; set; }
        public double Direction { get; set; }
    }

    public class Coordinates
    {
        public double Lon { get; set; }
        public double Lat { get; set; }
    }

    public class Device
    {
        public int Id { get; set; }
        public double Version { get; set; }
        public Coordinates Coord { get; set; } = new();
        public Antenna[] Rfin { get; set; } = new Antenna[16];
    }

    public class HandShake
    {
        public uint Cnt { get; set; }
        public long TSS { get; set; }
        public long TSN { get; set; }
        public ushort Port { get; set; }
        public Device Dev { get; set; } = new();
    }

    public static class HandshakeParser
    {
        public const int ExpectedSize = 626;

        public static HandShake Parse(byte[] buffer)
        {
            if (buffer == null)
                throw new ArgumentNullException(nameof(buffer));

            if (buffer.Length < ExpectedSize)
            {
                throw new ArgumentException(
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

                device.Rfin[i] =
                    ParseAntenna(buffer, antennaOffset);
            }

            return device;
        }

        private static Antenna ParseAntenna(
            byte[] buffer,
            int offset)
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

        private static int ReadInt32LE(
            byte[] buffer,
            int offset)
        {
            return BinaryPrimitives.ReadInt32LittleEndian(
                buffer.AsSpan(offset, 4));
        }

        private static uint ReadUInt32LE(
            byte[] buffer,
            int offset)
        {
            return BinaryPrimitives.ReadUInt32LittleEndian(
                buffer.AsSpan(offset, 4));
        }

        private static ushort ReadUInt16LE(
            byte[] buffer,
            int offset)
        {
            return BinaryPrimitives.ReadUInt16LittleEndian(
                buffer.AsSpan(offset, 2));
        }

        private static long ReadInt64LE(
            byte[] buffer,
            int offset)
        {
            return BinaryPrimitives.ReadInt64LittleEndian(
                buffer.AsSpan(offset, 8));
        }

        private static double ReadDoubleLE(
            byte[] buffer,
            int offset)
        {
            long bits = ReadInt64LE(buffer, offset);

            return BitConverter.Int64BitsToDouble(bits);
        }
    }
}

