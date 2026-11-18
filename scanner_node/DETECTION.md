# Детектор сигналов

## Назначение

Текущий detector — generic спектральная эвристика для RX I/Q. Он оценивает спектр и относительные признаки сигнала; это не распознавание QPSK/LDPC, протокола или конкретной модуляции.

| Свойство | Текущее значение |
| --- | --- |
| IQ | S8 и S16 Q11; поддерживаются Stub, HackRF и BladeRF при включённых backend SDK |
| PSD | Welch, Hann, 50% overlap, удаление комплексного DC в каждом сегменте |
| FFT | 64–4096 точек; измерение: 512, sweep: 1024 |
| Feature threshold | 8 dB над медианным PSD floor |
| Signal band | Непрерывная область минимум 2 бина; максимум результата — 16 полос |
| Числовой floor | -180 dBFS/Hz; если медиана попала в него, применяется ограниченная оценка `peak - 60 dB` |

## Асинхронный CFAR scan

Команды `0x72`/`0x73` выполняют ограниченный scan через единственный radio worker. `START` подтверждается сразу, а worker делает по одному capture/FFT шагу между UDP-запросами; `GET_RESULT` возвращает прогресс и лучший кандидат, не удерживая запрос до конца полного диапазона.

| Команда | Запрос после общего заголовка | Результат |
| --- | --- | --- |
| `0x72 DETECT_START` | channel, start/stop/step kHz, FFT size, averages per frequency | status и `job_id`; допустимо до 128 частот, FFT 64–4096, 1–32 averages |
| `0x73 DETECT_GET_RESULT` | `job_id` | state, progress 0–1000, текущая частота, число CFAR-полос, границы и центр лучшей полосы в Hz, SNR в centi-dB, флаг записи IQ |

Одновременно разрешён один job. CFAR использует CA-CFAR: 12 training bins с каждой стороны, 2 guard bins и номинальную настройку `Pfa=0.001` для независимых экспоненциальных power bins. Welch overlap и усреднение меняют фактическую статистику, поэтому это не обещание калиброванной вероятности ложной тревоги. PSD усредняется по кадрам в линейной мощности до CFAR; соседние активные bins образуют кандидата. Сканирование заканчивается состоянием `complete`; ошибка capture/tune или изменение frontend/sample rate/bandwidth даёт `failed`. IQ каждого шага добавляется в SCIQREC1 при заданном `--iq-file`; записи содержат частоту и timestamp, а флаг результата означает, что найденный кандидат имеет запись в этом файле. Это не распознавание модуляции или протокола.

Запрос `DETECT_START` имеет фиксированный размер 22 байта: `request_id:u32, opcode:u8, channel:u8, start_khz:u32, stop_khz:u32, step_khz:u32, fft_size:u16, averages:u16`. Ответ `DETECT_START` — 10 байт с `job_id:u32`. `DETECT_GET_RESULT` — 9-байтовый запрос (`request_id:u32, opcode:u8, job_id:u32`) и 46-байтовый ответ. Многобайтовые поля little-endian; статусы используют общую таблицу radio status.

## Классификация

Правила выполняются по порядку; срабатывает первое подходящее. Иначе результат `unknown`.

| Класс | Условие | Confidence |
| --- | --- | --- |
| `narrowband-tone` | Пик ≥12 dB над floor и занято ≤2% бинов | `min(0.95, 0.55 + peak_above_floor_db/80)` |
| `multicarrier-like` | Не менее 4 signal bands и занято ≥3% бинов | `min(0.85, 0.5 + occupied_fraction)` |
| `wideband-noiselike` | Spectral flatness ≥0.45 и занято ≥15% бинов | `min(0.8, 0.45 + spectral_flatness/2)` |
| `unknown` | Ни одно правило не подошло | `0` |

## Результат

Внутренний `ScannerAnalysisResult` содержит:

| Поле | Смысл |
| --- | --- |
| `mean_window_power_cdbfs` | Средняя нормированная мощность окна, dBFS × 100 |
| `features.noise_floor_dbfs_per_hz` | Медианный PSD floor |
| `features.peak_dbfs_per_hz`, `peak_above_floor_db` | Уровень пика и превышение над floor |
| `features.peak_offset_hz` | Смещение пика от центра baseband |
| `features.occupied_bandwidth_hz`, `occupied_fraction` | Суммарная ширина активных полос и доля активных бинов |
| `features.spectral_flatness` | Спектральная плоскостность, 0–1 |
| `bands[]` | Границы, пик и уровень каждой непрерывной полосы |
| `classification.signal_class`, `confidence`, `classifier_name` | Класс, уверенность и имя правила/plugin |

Класс и PSD-признаки сейчас **не передаются по UDP**. Они видны в debug-журнале. Ответ `0x6D`/`0x02`/`0x03` содержит только `power_cdbfs` — среднюю мощность окна, не PSD floor и не classification.

## Запуск

| Задача | Команда из `scanner_node` |
| --- | --- |
| Собрать и протестировать локально | `cmake --build build` затем `ctest --test-dir build --output-on-failure` |
| Прогнать Stub + mock, включая частоту/gain/измерения/sweep | `python tools/radio_e2e_test.py --client build/scanner_node.exe --device-json ../device.json --debug 1` |
| Проверить все режимы логирования | `python tools/logging_e2e_test.py --client build/scanner_node.exe --device-json ../device.json` |
| Запустить клиента для сервера | `build/scanner_node.exe --server 127.0.0.1 --server-port 2653 --device-json ../device.json --debug 2` |

В последнем варианте сервер должен выбрать frontend, настроить sample rate и bandwidth, затем отправить команду измерения. `0x6D` анализирует текущую частоту; `0x02` сначала настраивает частоту; `0x03` сканирует диапазон. Для стандартного RX scanner лениво задаёт 2 MS/s и 1.75 MHz, если настройки ещё не заданы.

## Получение результата

При `--debug 1` строка detector выводится в консоль. При `--debug 2` ищите её в созданном TXT-файле:

```text
DSP class=narrowband-tone confidence=0.95 peak-offset=+500000 Hz occupied-BW=... Hz PSD-floor=... dBFS/Hz
```

Windows PowerShell:

```powershell
Select-String .\scanner_*.txt -Pattern "DSP class="
```

Linux:

```sh
grep 'DSP class=' scanner_*.txt
```

Полный результат доступен внутри C через `scanner_analysis_worker_analyze(...)` и `ScannerAnalysisResult`; описание типов — в `include/dsp.h` и `include/analysis_worker.h`. Порог 8 dB и minimum band width 2 bins задаются при вызове в `src/radio_commands.c`.
