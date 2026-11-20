# Ручной запуск скана BladeRF RX1

Выполняй команды в одном окне PowerShell на Windows. Антенна подключена к RX1, которому соответствует `--channel 0`. Скрипт запускает сканер на Raspberry Pi по SSH и останавливает его после прохода. Для графиков в `.venv` должен быть установлен `matplotlib`.

## 1. Проверка сети

```powershell
Set-Location 'C:\M\Distributed peleng'

$source = Find-NetRoute -RemoteIPAddress 10.13.16.141 |
    Where-Object IPAddress |
    Select-Object -First 1 -ExpandProperty IPAddress

if ($source -ne '10.13.16.169') {
    throw "Не та сеть: локальный адрес $source. Скан не запускать."
}
```

## 2. Сканирование и графики

```powershell
$python = (Resolve-Path '.\.venv\Scripts\python.exe').Path
Set-Location '.\4vlad\scanner_node'

$runRoot = '/mnt/scaner-ram/scanner-refactor/compare_20261120_165527'
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'

$reports = foreach ($dwell in 50, 100, 200) {
    $source = Find-NetRoute -RemoteIPAddress 10.13.16.141 |
        Where-Object IPAddress |
        Select-Object -First 1 -ExpandProperty IPAddress
    if ($source -ne '10.13.16.169') {
        throw "Сеть изменилась: локальный адрес $source. Скан остановлен."
    }

    $report = "bladerf-rx1-80-6000-${dwell}ms-$stamp.txt"
    Write-Host "Скан RX1: 80 МГц - 6 ГГц, dwell $dwell мс"

    & $python -u .\tools\real_detector_scan.py --run-root $runRoot `
        --channel 0 --start-khz 80000 --stop-khz 6000000 `
        --dwell-ms $dwell --result-timeout-sec 120 *> $report

    if ($LASTEXITCODE -ne 0) { throw "Скан не завершился. См. $report" }
    $report
}

& $python .\tools\plot_detector_report.py @reports
```

TXT-отчёты и PNG появятся в папке `scanner_node`. Графики строятся одним вызовом с одинаковыми шкалами. Для одного прохода замени `50, 100, 200` на нужное значение.

## Если Pi перезагружался

Сборка в RAM могла исчезнуть. После проверки сети выполни в папке `scanner_node`:

```powershell
.\devtools\build_pi_sweep.ps1 -PiHost 'rpi@10.13.16.141' `
    -IdentityFile "$HOME\.ssh\rpi_scanner_ed25519" `
    -RemoteRoot '/mnt/scaner-ram/scanner-refactor' -BuildOnly
```

После успешной сборки возьми путь из строки `Staged project:` без конечного `/scanner_node` и подставь его в `$runRoot` перед сканированием.

## Реализация на стороне UDP-сервера

Сервер слушает UDP на адресе и порту, указанных узлу через `--server` и `--server-port` (в ручном скрипте `10.13.16.169:26531`). Узел первым отправляет handshake. Ответ на handshake и команды отправляй на IP узла и порт `reply_port` из handshake (в этой установке `3333`). Все многобайтовые числа передаются в **little-endian**. Каждый новый запрос команды получает отдельный `request_id:u32`; сопоставляй ответы по адресу узла, `request_id` и opcode, кроме `VER`, где opcode в ответе отсутствует.

| Шаг | Запрос или действие сервера | Ожидаемый ответ |
| --- | --- | --- |
| 1. Handshake, без opcode | Принять 626 байт от узла. Прочитать `reply_port:u16` со смещения 20 и `device_id:i32` со смещения 22. Отправить `[key:32 байта][nonce:12 байт][timestamp_ms:i64]`, всего 52 байта. Рабочий скрипт генерирует случайные key/nonce и использует Unix-время в мс. | Отдельного ACK не ждать: проверить сессию командой `VER`. |
| 2. `0x01 VER` | `[request_id:u32][0x01:u8]`, 5 байт. | `[request_id:u32][version:10 ASCII-байт с заполнением нулями]`, 14 байт, без opcode и status. |
| 3. `0x04 GET_RADIO_FRONTENDS` | `[request_id:u32][0x04:u8]`, 5 байт. | `[request_id:u32][0x04:u8][status:u8][count:u8][active_id:u8][descriptors...]`, `8 + count * 128` байт. При `status=0` выбрать BladeRF по inventory; рабочий скрипт ищет RX capability `0x01`, 12-битный ADC и наличие нужного канала. Не фиксировать ID frontend в коде. |
| 4. `0x05 SET_ACTIVE_RADIO` | `[request_id:u32][0x05:u8][frontend_id:u8]`, 6 байт. | `[request_id:u32][0x05:u8][status:u8][active_id:u8]`, 7 байт. Проверить `status=0` и `active_id == frontend_id`. |
| 5. `0x72 DETECT_START` | 30 байт, поля и пример ниже. | `[request_id:u32][0x72:u8][status:u8][job_id:u32][effective_antenna_id:u8]`, 11 байт. При `status=0` сохранить `job_id` и эффективный ID антенны. |
| 6. `0x73 DETECT_GET_RESULT` | `[request_id:u32][0x73:u8][job_id:u32][candidate_offset:u32]`, 13 байт. Сначала опрашивать с `candidate_offset=0`. | Заголовок 25 байт и `candidate_count` записей по 16 байт; разбор ниже. |
| 7. `0xDE EXIT` | `[request_id:u32][0xDE:u8]`, 5 байт. Отправлять только если сервер должен остановить процесс узла. | `[request_id:u32][0xDE:u8][status:u8]`, 6 байт. `status=0` при успехе. Для следующего скана в той же сессии `EXIT` не нужен. |

### Аргументы `DETECT_START` (`0x72`)

Поля после `request_id:u32, opcode:u8`, в порядке передачи:

| Поле | Тип | Значение для этого скана |
| --- | --- | --- |
| `channel` | `u8` | `0` (физический RX1) |
| `antenna_id` | `u8` | `0` (логический вход; не переключает физическую антенну) |
| `algorithm_id` | `u8` | `1` (Welch PSD + CA-CFAR) |
| `start_khz`, `stop_khz` | `u32`, `u32` | `80000`, `6000000` |
| `dwell_ms` | `u32` | `50`, `100` или `200` в отдельных заданиях; общий бюджет интеграции, не на окно |
| `fft_size` | `u16` | `2048` |
| `sensitivity_cdb` | `i16` | `600` (+6 дБ) |
| `pfa_ppm` | `u32` | `1000` |
| `training_bins`, `guard_bins` | `u8`, `u8` | `16`, `4` с каждой стороны |

Команды настройки частоты, sample rate и полосы перед `0x72` не нужны: узел сам выбирает их для заданного диапазона. Одновременно доступно одно задание. Для серии `50 -> 100 -> 200` полностью вычитывай результаты предыдущего задания перед следующим `0x72`; handshake, `VER` и выбор frontend повторять не нужно, пока сессия сохраняется.

### Формат `DETECT_GET_RESULT` (`0x73`)

```text
request_id:u32, opcode:u8, status:u8, job_id:u32, state:u8,
progress_permille:u16, effective_antenna_id:u8, algorithm_id:u8,
total_candidates:u32, candidate_offset:u32, candidate_count:u16,
candidates[candidate_count]

candidate = center_hz:u64, bandwidth_hz:u32, power_cdbfs:i16, snr_cdb:i16
```

1. Опрашивай примерно раз в 100 мс, с таймаутом ожидания ответа и общим дедлайном задания. `state`: `0=IDLE`, `1=RUNNING`, `2=COMPLETE`, `3=FAILED`. Прогресс `0..1000` дан в промилле. Пока `RUNNING`, используй ответ только для прогресса; промежуточные кандидаты не добавляй в итог.
2. При `COMPLETE` и `status=0` сохрани `total_candidates`. Ответ на опрос с offset `0` уже может содержать первую страницу: используй её либо запроси заново, но не добавляй дважды. При нулевом total результат пустой. Иначе запрашивай следующий offset, прибавляя фактически полученный `candidate_count`, пока offset не достигнет total. Максимум 64 записи на страницу.
3. До разбора записей проверь ожидаемые `request_id`, opcode, `job_id`, offset, неизменность итогового total и длину `25 + 16 * candidate_count`. Пустая страница до достижения total, неожиданный `IDLE`, `FAILED` или ненулевой status не считаются полным результатом. В частности, `10` означает, что задание уже выполняется, а `11` (`RESULT_LIMIT`) означает неполный набор. При таймауте `DETECT_START` не повторяй его вслепую: команда могла дойти, даже если ответ потерялся.
4. Для отчёта переведи `center_hz / 1e6` в МГц, `bandwidth_hz / 1e3` в кГц, `power_cdbfs / 100` в dBFS и `snr_cdb / 100` в дБ. Мощность не калибрована в dBm.

Подробности: [wire protocol и список команд](README.md#wire-protocol), [коды статусов](README.md#status-codes), [параметры CA-CFAR и процедура сервера](DETECTION.md#процедура-сервера). Рабочий пример обмена: [tools/real_detector_scan.py](tools/real_detector_scan.py); разбор handshake и inventory: [tools/demo_server_mock.py](tools/demo_server_mock.py).