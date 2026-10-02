# Scanner Development Helpers

These Windows PowerShell scripts automate the repeatable validation and Raspberry Pi benchmark workflow. Run them from any current directory; paths are resolved relative to this folder.

## Local Verification

```powershell
powershell -ExecutionPolicy Bypass -File .\devtools\verify_local.ps1
```

This builds the existing CMake tree, runs CTest, and runs the Python mock-server tests using the workspace `.venv` when present. Add `-RunE2E` to run the full scanner/mock UDP session, or `-SkipPython` to omit Python tests. `-FrontendId 1` selects a different mock frontend for E2E.

## Pi Build And Sweep

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