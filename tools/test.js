'use strict';

const dgram = require('node:dgram');

const HOST = '0.0.0.0';
const PORT = 2222;

const socket = dgram.createSocket('udp4');

function parseAntenna(buffer, offset) {
  return {
    in: buffer.readInt32LE(offset),
    from: buffer.readInt32LE(offset + 4),
    to: buffer.readInt32LE(offset + 8),
    polar: buffer.readDoubleLE(offset + 12),
    type: buffer.readInt32LE(offset + 20),
    dBi: buffer.readInt32LE(offset + 24),
    direction: buffer.readDoubleLE(offset + 28)
  };
}

function parseDevice(buffer, offset) {
  const device = {
    id: buffer.readInt32LE(offset),
    version: buffer.readDoubleLE(offset + 4),
    coord: {
      lon: buffer.readDoubleLE(offset + 12),
      lat: buffer.readDoubleLE(offset + 20)
    },
    rfin: []
  };
  const antennasOffset = offset + 28;
  const antennaSize = 36;
  for (let i = 0; i < 16; i++) {
    const antennaOffset = antennasOffset + i * antennaSize;
    device.rfin.push(parseAntenna(buffer, antennaOffset));
  }
  return device;
}

function parseHandShake(buffer) {
  const expectedSize = 626;
  if (buffer.length < expectedSize) {
    throw new Error(
      `Слишком короткий пакет: ${buffer.length} байт, ` +
      `ожидалось минимум ${expectedSize}`
    );
  }
//  const devOffset = 22;
  return {
    cnt: buffer.readUInt32LE(0),
    // BigInt используется для безопасного чтения 64-битного integer.
    TSS: buffer.readBigInt64LE(4),
    TSN: buffer.readBigInt64LE(12),
    port: buffer.readUInt16LE(20),
    dev: parseDevice(buffer,22)
  };
}

socket.on('message', (buffer, remote) => {
  socket.send(buffer, 0, 2, 3333, remote.address);

  console.log(
    `Получено ${buffer.length} байт от ` +
    `${remote.address}:${remote.port}`
  );

  try {
    const handshake = parseHandShake(buffer);
    console.dir(handshake, {depth: null, colors: true  });
    console.log(handshake);
  } catch (error) { console.error('Ошибка разбора HandShake:', error.message); }
});

socket.on('error', (error) => {
  console.error('Ошибка UDP:', error);
  socket.close();
});

socket.on('listening', () => {
  console.log(`UDP-сервер слушает ${HOST}:${PORT}`);
});

socket.bind(PORT, HOST);