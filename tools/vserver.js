'use strict';

const dgram = require('node:dgram');
const crypto = require('node:crypto'); // Добавляем модуль для криптографии

const HOST = '0.0.0.0';
const PORT = 2200

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

  return {
    cnt: buffer.readUInt32LE(0),
    TSS: buffer.readBigInt64LE(4),
    TSN: buffer.readBigInt64LE(12),
    port: buffer.readUInt16LE(20), // <-- Читаем порт из структуры (offset 20)
    dev: parseDevice(buffer, 22)
  };
}

socket.on('message', (buffer, remote) => {
  console.log(
    `Получено ${buffer.length} байт от ` +
    `${remote.address}:${remote.port}`
  );

  try {
    // 1. Разбираем входящий пакет
    const handshake = parseHandShake(buffer);
    console.dir(handshake, { depth: null, colors: true });

    // Достаем порт, на который клиент просит ответить
    const targetPort = handshake.port; 

    // 2. Генерируем 52-байтовый ответ
    // Выделяем буфер строго под 52 байта
    const responseBuffer = Buffer.alloc(52);

    // Записываем Timestamp (8 байт) в формате BigInt (миллисекунды Unix)
    const timestamp = BigInt(Date.now());
    responseBuffer.writeBigInt64LE(timestamp, 0);

    // Генерируем случайный нонс для ChaCha20 IETF (12 байт) начиная с 8-го байта
    crypto.randomFillSync(responseBuffer, 8, 12);

    // Генерируем случайный 256-битный ключ (32 байта) начиная с 20-го байта
    crypto.randomFillSync(responseBuffer, 20, 32);

    // 3. Отправляем 52 байта на порт из структуры
    socket.send(responseBuffer, 0, responseBuffer.length, targetPort, remote.address, (err) => {
      if (err) {
        console.error(`Ошибка отправки ответа на порт ${targetPort}:`, err.message);
      } else {
        console.log(`Успешно отправлен ответ (${responseBuffer.length} байт) на порт ${targetPort}`);
      }
    });

  } catch (error) { 
    console.error('Ошибка разбора HandShake или генерации ответа:', error.message); 
  }
});


socket.on('error', (error) => {
  console.error('Ошибка UDP:', error);
  socket.close();
});

socket.on('listening', () => {
  console.log(`UDP-сервер слушает ${HOST}:${PORT}`);
});

socket.bind(PORT, HOST);
