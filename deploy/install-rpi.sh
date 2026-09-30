#!/usr/bin/env bash
set -Eeuo pipefail

SERVICE_NAME="scaner"
RAM_MOUNT="/mnt/scaner-ram"
RAM_SIZE="512M"
DEPLOY_USER="${SUDO_USER:-rpi}"

if [[ "${EUID}" -ne 0 ]]; then
    echo "Run this script with sudo: sudo bash install-rpi.sh" >&2
    exit 1
fi

if ! id "${DEPLOY_USER}" >/dev/null 2>&1; then
    echo "Deployment user does not exist: ${DEPLOY_USER}" >&2
    exit 1
fi

DEPLOY_UID="$(id -u "${DEPLOY_USER}")"
DEPLOY_GID="$(id -g "${DEPLOY_USER}")"
DEPLOY_GROUP="$(id -gn "${DEPLOY_USER}")"
FSTAB_ENTRY="tmpfs ${RAM_MOUNT} tmpfs rw,nosuid,nodev,noatime,size=${RAM_SIZE},mode=0755,uid=${DEPLOY_UID},gid=${DEPLOY_GID} 0 0"

echo "[1/6] Installing the C toolchain..."
apt-get -o DPkg::Lock::Timeout=300 update
DEBIAN_FRONTEND=noninteractive apt-get -o DPkg::Lock::Timeout=300 install -y gcc

echo "[2/6] Configuring the RAM disk..."
install -d -m 0755 "${RAM_MOUNT}"
if ! grep -qF " ${RAM_MOUNT} " /etc/fstab; then
    cp -a /etc/fstab "/etc/fstab.${SERVICE_NAME}-backup"
    printf '%s\n' "${FSTAB_ENTRY}" >>/etc/fstab
fi
mountpoint -q "${RAM_MOUNT}" || mount "${RAM_MOUNT}"
install -d -o "${DEPLOY_USER}" -g "${DEPLOY_GROUP}" -m 0755 \
    "${RAM_MOUNT}/build/src" "${RAM_MOUNT}/run"

echo "[3/6] Creating persistent release directories..."
install -d -m 0755 /opt/scaner /etc/scaner

echo "[4/6] Installing scaner.service..."
cat >/etc/systemd/system/scaner.service <<EOF
[Unit]
Description=Scaner UDP client
Wants=network-online.target
After=network-online.target
RequiresMountsFor=${RAM_MOUNT}

[Service]
Type=oneshot
RemainAfterExit=yes
User=${DEPLOY_USER}
Group=${DEPLOY_GROUP}
WorkingDirectory=${RAM_MOUNT}/run
ExecStartPre=/usr/bin/install -m 0755 /opt/scaner/device.bin ${RAM_MOUNT}/run/device.bin
ExecStartPre=/usr/bin/install -m 0644 /etc/scaner/device.json ${RAM_MOUNT}/run/device.json
ExecStart=${RAM_MOUNT}/run/device.bin -p 2200
StandardOutput=append:${RAM_MOUNT}/run/scaner.log
StandardError=append:${RAM_MOUNT}/run/scaner.log
NoNewPrivileges=true
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=${RAM_MOUNT}
RestrictAddressFamilies=AF_INET
MemoryMax=128M

[Install]
WantedBy=multi-user.target
EOF

echo "[5/6] Reloading systemd..."
systemctl daemon-reload
systemd-analyze verify /etc/systemd/system/scaner.service

echo "[6/6] Done. Deploy the application from Windows before enabling the service."
echo "RAM disk: ${RAM_MOUNT} (${RAM_SIZE})"
echo "Service:  ${SERVICE_NAME}.service"

