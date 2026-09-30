# Raspberry Pi deployment

The Windows checkout remains the source of truth. Compilation and runtime files
are placed on a 512 MiB `tmpfs` at `/mnt/scaner-ram`; only the last accepted
binary, `device.json`, the mount configuration, and the systemd unit are stored
persistently on the Raspberry Pi.

## Prepare a new Raspberry Pi

From the repository root, run:

```powershell
.\deploy\bootstrap-rpi.ps1 -Target rpi@<pi-address>
```

This wrapper uploads and executes `install-rpi.sh`. Without an SSH key, Windows
may ask for the Raspberry Pi password once for `scp` and again for `ssh`.

## Build and deploy from Windows

```powershell
.\deploy\deploy.ps1 -Target rpi@<pi-address>
```

The script deliberately does not start the service by default. The current wire
format contains native C `long` fields: the Raspberry Pi must be 64-bit to match
the 626-byte handshake expected by the repository's Node.js and .NET parsers.
After verifying compatibility, start it with:

```powershell
.\deploy\deploy.ps1 -Target rpi@<pi-address> -StartService
```

Runtime output is kept in RAM:

```bash
cat /mnt/scaner-ram/run/scaner.log
```
