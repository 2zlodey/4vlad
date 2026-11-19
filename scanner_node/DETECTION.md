# Детектор сигналов

## Назначение

Текущий detector — generic спектральная эвристика для RX I/Q. Он оценивает спектр и относительные признаки сигнала; это не распознавание QPSK/LDPC, протокола или конкретной модуляции.

| Свойство | Текущее значение |
| --- | --- |
| IQ | S8 и S16 Q11; поддерживаются Stub, HackRF и BladeRF при включённых backend SDK |
| PSD | Welch, Hann, 50% overlap, удаление комплексного DC в каждом сегменте |
| FFT | 64–4096 точек; размер задаёт сервер в параметрах алгоритма |
| FFT backend | FFTW3; forward plan повторно используется для сегментов одного PSD |
| Feature threshold | 8 dB над медианным PSD floor |
| Legacy feature bands | Для одного окна: непрерывная область минимум 2 бина; максимум `ScannerAnalysisResult` — 16 полос |
| Числовой floor | -180 dBFS/Hz; если медиана попала в него, применяется ограниченная оценка `peak - 60 dB` |

## Асинхронный CFAR scan

Команды `0x72`/`0x73` выполняют scan через единственный radio worker. Сервер задаёт полосу, канал, антенну, алгоритм, параметры и общий dwell; частотного шага в запросе нет. Узел выбирает поддерживаемые sample rate/BW, вычисляет число перекрывающихся окон и перестраивает SDR внутри диапазона. Для BladeRF выбранный RX-канал захватывается как `BLADERF_RX_X1`; второй RX не включается, поэтому channel 0 или 1 может использовать собственный максимум sample rate (для данного устройства 61.44 Msps) и BW (56 MHz). Перекрытие окон 20%; после 64 кандидатов на страницу результат опрашивается следующим offset.

| Команда | Запрос после общего заголовка | Результат |
| --- | --- | --- |
| `0x72 DETECT_START` | channel, antenna ID, algorithm ID, start/stop kHz, dwell ms, FFT, sensitivity, Pfa, training/guard bins | status, `job_id`, эффективный antenna ID |
| `0x73 DETECT_GET_RESULT` | `job_id`, candidate offset | state, progress 0–1000, antenna/algorithm IDs, total, offset, page count, затем элементы `{center_hz:u64, bandwidth_hz:u32, power_cdbfs:i16, snr_cdb:i16}` |

Одновременно разрешён один job. Algorithm ID `1` — Welch PSD + CA-CFAR. Его параметры: FFT size (64–4096, степень двойки), минимальный SNR (centi-dB), Pfa (ppm), training bins/side (1–64), guard bins/side (0–32). CA-CFAR использует заданные training/guard bins; стандартное значение Pfa `1000 ppm` (`0.001`). Welch overlap и усреднение меняют фактическую статистику, поэтому Pfa не является обещанием калиброванной вероятности ложной тревоги. PSD усредняется в линейной мощности; соседние активные bins образуют кандидата. Candidate power — интеграл PSD по полосе в dBFS, не dBm; SNR — отношение интегральной мощности кандидата к локально оценённому шуму.

`dwell_ms` — общий бюджет времени интеграции I/Q по окнам диапазона. Кадры распределяются по окнам пропорционально, минимум один capture-блок на окно. Блок содержит до 16384 complex I/Q pairs; Welch усредняет его внутренние FFT-сегменты. Число блоков округляется вверх, поэтому интеграция квантована блоками и может превышать `dwell_ms`. Текущий backend запускает/останавливает sync RX для каждого блока; длинный dwell может завершиться USB/NIOS timeout, поэтому для него нужен непрерывный RX stream. Все кандидаты хранятся до лимита 16384; при превышении job завершается с `RESULT_LIMIT`, чтобы не выдавать частичный результат как полный.

Пример для scan 1.5–2.5 GHz на BladeRF RX_X1: usable width 50.4 MHz, hop 40.32 MHz, 25 окон, 61.44 Msps, 16384 complex pairs/capture.

Число блоков на окно вычисляется как `max(1, ceil(ceil(dwell_ms * sample_rate_hz / 1000 / window_count) / 16384))`. Поэтому короткие dwell округляются вверх до полного блока в каждом окне:

| Запрошенный dwell | Блоков на окно | Всего блоков | Сэмпловая интеграция |
| ---: | ---: | ---: | ---: |
| 1 ms | 1 | 25 | 6.67 ms |
| 10 ms | 2 | 50 | 13.33 ms |
| 100 ms | 15 | 375 | 100 ms |
| 1000 ms | 150 | 3750 | target 1000 ms; ранее scan завершился USB/NIOS timeout до конца |

На практике `dwell_ms=1` и `10` в этом примере дают соответственно около 6.67 и 13.33 ms сэмпловой интеграции; измеренное wall-clock время больше из-за аппаратной настройки окон.

Для выбранной антенны ищется запись `rfin[].in` с совпадающим antenna ID; её `type`, `dBi`, `from/to`, `polar` и `direction` доступны в журнале, а effective input возвращается серверу. Единицы `from/to` исходный протокол не определяет, поэтому они пока не используются как RF-фильтр. На каждом RX-канале по умолчанию задан маршрут `switched`; физическое переключение пока не выполняется, но ID проверяется и подтверждается без ошибки. Опциональный `antennaRouting` в device JSON позволяет задать два маршрута, по одному на RX channel:

```json
"antennaRouting": [
	{ "mode": "switched" },
	{ "mode": "fixed", "antennaInput": 7 }
]
```

Для `fixed` запрос принимается, но эффективным становится указанный fixed antenna ID; ответ `START` и страницы результата возвращают именно его. Это покрывает два коммутатора, один общий фиксированный вход или смешанную конфигурацию. Реальная аппаратная коммутация остаётся отложенной.

Запрос `DETECT_START` — 30 байт: `[request_id:u32, opcode:u8, channel:u8, antenna_id:u8, algorithm_id:u8, start_khz:u32, stop_khz:u32, dwell_ms:u32, fft_size:u16, sensitivity_cdb:i16, pfa_ppm:u32, training_bins:u8, guard_bins:u8]`. Ответ — 11 байт, включая `job_id:u32` и эффективный antenna ID. Запрос `DETECT_GET_RESULT` — 13 байт: общий заголовок, `job_id:u32`, `candidate_offset:u32`. Ответный заголовок — 25 байт, за ним 0–64 элемента по 16 байт. Элемент: `center_hz:u64`, `bandwidth_hz:u32`, `power_cdbfs:i16`, `snr_cdb:i16`. Все multi-byte поля little-endian.

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

При `--debug 1` строка legacy window classifier выводится в консоль. При `--debug 2` ищите её в созданном TXT-файле:

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

Результат диапазонного detector scan запрашивается через `0x72/0x73`; каждый candidate содержит центр, ширину, интегральную мощность dBFS и SNR. Legacy one-window feature result доступен внутри C через `scanner_analysis_worker_analyze(...)` и `ScannerAnalysisResult`; его порог 8 dB и minimum band width 2 bins не являются параметрами CA-CFAR scan.
