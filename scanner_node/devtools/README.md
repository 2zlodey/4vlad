# Scanner Development Helpers

These Windows PowerShell scripts automate the repeatable validation and Raspberry Pi benchmark workflow. Run them from any current directory; paths are resolved relative to this folder.

## Local Verification

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\verify_local.ps1
```

This builds the existing CMake tree, runs CTest, and runs the Python mock-server tests using the workspace `.venv` when present. Add `-RunE2E` to run the full scanner/mock UDP session, or `-SkipPython` to omit Python tests. `-FrontendId 1` selects a different mock frontend for E2E.

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

The current `build_pi_sweep.ps1` assumes SSH port 22, key authentication, a RAM-disk root and the Pi build script's package setup. It is NOT a drop-in deploy command for conductor. LAN discovery also does not locate this external endpoint.

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

The script creates a unique timestamped staging root on the Pi's RAM disk. It copies all production `include/` and `src/` files, only the four needed host perf files, and the two CMake tool sources plus the IQ recorder/mock dependencies. It also copies CMake/build metadata, `device.json`, and the sibling `cJSON.c/.h` dependency. It deliberately omits `tests/`, Python `__pycache__`, `.git`, the Zynq HAL, format/project metadata, plotters, and unrelated tools. This avoids stale files from older deployments and keeps the uploaded source manifest small. It runs `build_pi.sh`, verifies both expected USB IDs, executes the configured sweep, and copies the log into `measurements/`.

Defaults target `rpi@10.123.71.141`, use `~/.ssh/rpi_scanner_ed25519`, and scan 1000-6000 MHz in 10 MHz steps. Parameters can override the Pi and sweep range:

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\build_pi_sweep.ps1 -StartMHz 2400 -StopMHz 2500 -StepMHz 5
powershell -ExecutionPolicy Bypass -File .\devtools\build_pi_sweep.ps1 -BuildOnly
```

The first run may request the Pi account password through `sudo` if build packages are missing. Do not put passwords in command arguments. Source/build staging is on tmpfs and can disappear after a remount or reboot; measurement logs are copied back to the local `measurements/` folder. If SSH drops during a sweep, the Pi log is kept under `/home/rpi/sweep_perf_*.log` and can be retrieved separately.