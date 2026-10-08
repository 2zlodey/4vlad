# Scanner Development Helpers

These helpers automate local validation, local hardware Pi benchmarks and remote conductor Stub deployment. Script paths below assume the current directory is `scanner_node`; once invoked, helpers resolve their inputs relative to their own location.

## Workflow Audit (2026-10-08)

Repeated session actions were: build + CTest + Python tests + four logging-mode E2E runs; manually package the working tree, transfer it over SSH port 2222, repeat GCC flags on conductor and verify the resulting binary; separately upload hardware E2E scripts; diagnose stalled SSH connections. The helpers now cover these steps:

| Task | Helper | Checks and limits |
| --- | --- | --- |
| Complete local regression | `verify_local.ps1 -RunLoggingE2E` | Build, all CTest targets, mock unit tests, complete UDP sessions in debug 0..3. |
| Inspect remote deployment | `deploy_conductor.ps1 -DryRun` | Checks local inputs and displays manifest/commands without network or writes. |
| Deploy current source to conductor | `deploy_conductor.ps1` | Scoped archive, hostname guard, isolated directory, GCC Stub build, focused C tests and four-mode E2E. |
| Rebuild an existing remote source tree | `sh devtools/build_remote_stub.sh` | Publishes the binary only after successful build/tests. |
| Local Pi hardware build/sweep | `build_pi_sweep.ps1` | SDK/CMake build; stages radio/logging E2E tools and uses SSH keepalive. |

No common-helper framework or SDK/CMake changes were needed: hardware and Stub deployments have different environments. Keep them separate rather than silently falling back to synthetic RF data on a hardware target. No helper commits source, installs a service, stores a password or launches production scanning.

Validation on 2026-10-08: `verify_local.ps1 -RunLoggingE2E` passed all 8 CTest targets, 8 Python mock tests and four logging-mode E2E sessions. The conductor helper's dry run and the remote shell script's syntax check passed. After earlier SSH authentication failures, a complete `deploy_conductor.ps1` run succeeded: hostname verification, upload, GCC Stub build, logger/SDR-reinitialization/commutator C tests and all four logging-mode UDP E2E sessions passed. The verified binary is `/home/vlad/scanner-deploy/run_20261008_160456_55936/scanner_node/build-remote/scanner_node`. Tests stopped the scanner afterwards; previous deployments and production services were not changed.

## Local Verification

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\verify_local.ps1
```

This builds the existing CMake tree, runs CTest, and runs the Python mock-server tests using the workspace `.venv` when present. Add `-RunE2E` to run the full scanner/mock UDP session, or `-SkipPython` to omit Python tests. `-FrontendId 1` selects a different mock frontend for E2E.

For the full regression workflow used during development:

```powershell
.\devtools\verify_local.ps1 -RunLoggingE2E
```

`-RunLoggingE2E` runs complete radio sessions under all four debug modes, including SDR reinitialization, commutator placeholder replies, capture and shutdown. It takes precedence over `-RunE2E` to avoid repeating the same sessions and currently starts at frontend 0; omit `-FrontendId`. `-SkipPython` skips mock unit tests only; explicitly requested E2E tests still run. An existing configured `build/` tree is required. Windows development uses the configured Stub fallback when no SDK/device is available.

## Find The Pi

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\find_pi.ps1
```

The helper derives IPv4 networks from active local adapters and checks TCP port 22 in bounded parallel batches. It then authenticates using `rpi` and `~/.ssh/rpi_scanner_ed25519`, verifies `/proc/device-tree/model`, and returns `Address`, `SshTarget`, `Model`, `HostName`, and `Architecture`. An open SSH port alone is not considered a Raspberry Pi. Authentication failures are reported without prompting for passwords. New SSH host keys are accepted and stored; changed keys are not silently accepted.

By default, at most 4096 addresses are checked; larger adapter networks are skipped with a warning. Use explicit CIDR ranges to restrict discovery, and override the account/key when needed:

```powershell
.\devtools\find_pi.ps1 -Subnet 10.215.246.0/24
.\devtools\find_pi.ps1 -Subnet 192.168.1.0/24 -UserName rpi -IdentityFile "$HOME\.ssh\rpi_scanner_ed25519"
```

Use the verified result with the build helper instead of its fixed default IP. If multiple Pis are returned, select the intended hostname:

```powershell
$pi = .\devtools\find_pi.ps1 | Where-Object HostName -eq 'rpi4'
if (@($pi).Count -ne 1) { throw 'Expected exactly one rpi4.' }
.\devtools\build_pi_sweep.ps1 -PiHost $pi.SshTarget -BuildOnly
```

Discovery was verified on 2026-10-07: `rpi4`, Raspberry Pi 4 Model B Rev 1.5, `armv7l`, at `10.215.246.141`. The IP is not hardcoded in the discovery helper and may change with DHCP.

## Remote Conductor

Verified on 2026-10-07. This is a separate remote device, NOT the local `rpi4` discovered by `find_pi.ps1`:

| Setting | Value |
| --- | --- |
| SSH host | `np.lora-wan.net` |
| SSH port | `2222` |
| Account | `vlad` |
| Remote hostname | `conductor` |
| Hardware | Raspberry Pi 4 Model B Rev 1.4 |
| Architecture | `aarch64` (64-bit ARM) |
| Deployed commit | `24e4a24` (`Add logging`) |
| Deployment root | `/home/vlad/scanner-deploy/run_20261007_logging` |
| Scanner executable | `/home/vlad/scanner-deploy/run_20261007_logging/scanner_node/build-remote/scanner_node` |

Connect from Windows PowerShell:

```powershell
ssh -p 2222 vlad@np.lora-wan.net
```

Password authentication was verified; the local test Pi key `~/.ssh/rpi_scanner_ed25519` was rejected for this account. Enter the password directly at the terminal prompt. Do not store passwords in this repository, command arguments, scripts, logs, or chat notes. If SSH offers unrelated keys first, use `ssh -p 2222 -o PreferredAuthentications=password -o PubkeyAuthentication=no vlad@np.lora-wan.net`. Keep host-key checking enabled.

For SCP, the port flag is uppercase `-P`:

```powershell
scp -P 2222 .\some-file vlad@np.lora-wan.net:/home/vlad/scanner-deploy/
```

At verification time, GCC, Git, Make, pkg-config and Python 3 were available, but CMake, Ninja and BladeRF/HackRF development packages were absent; no USB SDR was connected. `/mnt/scaner-ram` did not exist. `/home/vlad/4vlad` belongs to root and was left untouched. Deploy into a separate user-owned directory under `/home/vlad/scanner-deploy`; this is not the local Pi's tmpfs staging path.

`build_pi_sweep.ps1` assumes SSH port 22, key authentication, a RAM-disk root and the Pi build script's package setup. It is NOT a drop-in deploy command for conductor. LAN discovery also does not locate this external endpoint.

### Automated Deployment

```powershell
.\devtools\deploy_conductor.ps1 -DryRun
.\devtools\deploy_conductor.ps1
.\devtools\deploy_conductor.ps1 -IdentityFile "$HOME\.ssh\conductor_ed25519"
```

Defaults are `-SshTarget vlad@np.lora-wan.net -Port 2222 -ExpectedHostName conductor -RemoteRoot /home/vlad/scanner-deploy`. Without `-IdentityFile`, passwords are entered directly in SSH/SCP terminal prompts (up to three logins); with a key, batch-mode authentication fails rather than prompting. Host-key checks remain enabled. SSH keepalive detects broken established connections; connection timeout is 10 seconds. `RemoteRoot` must be an absolute path without spaces or parent traversal. `SshTarget` accepts `user@hostname` or `user@IPv4`, not SSH aliases/IPv6.

The helper archives the **current working tree**, including uncommitted production changes. It includes all `src/` and `include/` modules (including the commutator), four host perf files, `cJSON.c/.h`, runtime `device.json`, the remote build script, three focused C tests and the mock/radio/logging Python scripts. It omits local binaries, `.git`, measurements, the XLSX and unrelated files. A unique `run_YYYYMMDD_HHMMSS_PID` directory is created; existing deployments and `/home/vlad/4vlad` remain untouched. The upload archive is retained in that directory for diagnosis; local temporary files are removed automatically.

Both remote SSH steps require `hostname` to equal `ExpectedHostName`. The GCC build enables Stub explicitly and does not compile SDK backends. It writes `scanner_node.new`, runs logger/SDR-reset/commutator C tests and four-mode loopback UDP E2E, then publishes `build-remote/scanner_node` only on success. Failed checks leave any prior binary intact and return a nonzero exit code. `-BuildOnly` skips tests with a warning; GCC is required, and normal verification also requires Python 3. No sudo, package installation, service or production-server connection occurs. The final output reports the exact binary path; the scanner is not left running.

To rerun from an existing source tree on conductor:

```sh
cd /home/vlad/scanner-deploy/run_YOUR_TIMESTAMP/scanner_node
sh devtools/build_remote_stub.sh
```

Use `--build-only` only when deliberately skipping tests. The shell helper expects POSIX/Linux, GCC, pthreads and the staged sibling `cJSON`/`device.json`. Scripts copied from Windows must retain LF line endings. The manual commands below describe the earlier Git-based deployment and remain a fallback; they deploy committed source rather than the current working copy.

To create a fresh deployment, run inside the remote SSH shell, choosing a new unused directory name:

```sh
mkdir -p ~/scanner-deploy
git clone --depth 1 --branch feature/project-initial-structure \
	https://github.com/2zlodey/4vlad.git ~/scanner-deploy/run_NEW_TIMESTAMP
cd ~/scanner-deploy/run_NEW_TIMESTAMP/scanner_node
mkdir -p build-remote
gcc -std=c99 -O2 -Wall -Wextra -Wpedantic \
	-D_POSIX_C_SOURCE=200809L -DSCANNER_ENABLE_STUB_SDR=1 \
	-DCONFIG_PERF_ENABLE=1 -DCONFIG_PERF_STRINGS=1 -DCONFIG_PERF_LOG_ENABLE=1 \
	-Iinclude -Iperf_lib -I../cJSON src/*.c \
	perf_lib/perf_probe.c perf_lib/perf_probe_hal_host.c ../cJSON/cJSON.c \
	-pthread -lm -o build-remote/scanner_node.new
```

Only after GCC succeeds, install the new binary and run the local-controller tests:

```sh
mv build-remote/scanner_node.new build-remote/scanner_node
python3 tools/radio_e2e_test.py --client ./build-remote/scanner_node --device-json ../device.json --debug 0
python3 tools/logging_e2e_test.py --client ./build-remote/scanner_node --device-json ../device.json
```

This GCC command intentionally enables the Stub fallback and does not compile hardware SDK backends. Without physical SDRs, the verified deployment exposes frontend ID `0`, one RX channel, synthetic IQ/power, separate gain settings and the normal UDP commands. Real SDR support requires installing the SDK dependencies and rebuilding with the corresponding backends, preferably through CMake. Stub results are not RF measurements.

The full radio E2E and all four logging modes passed on conductor. Tests use a loopback UDP controller, do not contact the production server, and terminate the scanner afterwards. Deployment does not configure a service or autostart. For a manual production run, explicitly provide the device JSON and desired server settings; `--debug 2/3` creates timestamped logs in the current working directory.

## Pi Build And Sweep
Logging verification for all four debug modes is available through `tools/logging_e2e_test.py --client PATH --device-json PATH`. It runs complete UDP command sessions, verifies console/file destinations, HTML escaping, packet metadata, IQ omission and fatal errors. `tools/radio_e2e_test.py` also accepts `--debug 0..3` for a single run. File modes create timestamped logs in the process working directory.


```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\build_pi_sweep.ps1
```

The script creates a unique timestamped staging root on the Pi's RAM disk. It copies all production `include/` and `src/` files, only the four needed host perf files, and the two CMake tool sources plus the IQ recorder/mock dependencies and radio/logging E2E runners. It also copies CMake/build metadata, `device.json`, and the sibling `cJSON.c/.h` dependency. It deliberately omits `tests/`, Python `__pycache__`, `.git`, the Zynq HAL, format/project metadata, plotters, and unrelated tools. This avoids stale files from older deployments and keeps the uploaded source manifest small. It runs `build_pi.sh`, verifies both expected USB IDs, executes the configured sweep, and copies the log into `measurements/`. SSH uses keepalive (`5` seconds, `2` missed replies) to detect broken established connections instead of waiting indefinitely.

Defaults target `rpi@10.123.71.141`, use `~/.ssh/rpi_scanner_ed25519`, and scan 1000-6000 MHz in 10 MHz steps. Parameters can override the Pi and sweep range:

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\build_pi_sweep.ps1 -StartMHz 2400 -StopMHz 2500 -StepMHz 5
powershell -ExecutionPolicy Bypass -File .\devtools\build_pi_sweep.ps1 -BuildOnly
```

The first run may request the Pi account password through `sudo` if build packages are missing. Do not put passwords in command arguments. Source/build staging is on tmpfs and can disappear after a remount or reboot; measurement logs are copied back to the local `measurements/` folder. If SSH drops during a sweep, the Pi log is kept under `/home/rpi/sweep_perf_*.log` and can be retrieved separately.