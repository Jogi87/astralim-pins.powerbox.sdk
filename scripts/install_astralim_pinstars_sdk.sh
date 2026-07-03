#!/usr/bin/env bash
set -euo pipefail

PLUGIN_SDK="/home/pi/.local/share/NINA/Plugins/3.0.0/pins.plugin/PowerBoxSDK.dll"
INSTALLED_SDK="/usr/local/lib/libPowerBoxSDK.so"
SERVICE_NAME="pins.service"
RESTARTED=0

restart_on_exit()
{
    if [[ "${RESTARTED}" != "1" ]]; then
        echo "Restarting ${SERVICE_NAME} after early exit..."
        sudo systemctl start "${SERVICE_NAME}" || true
    fi
}
trap restart_on_exit EXIT

if [[ ! -f "${PLUGIN_SDK}" ]]; then
    echo "ERROR: active Pi'n'Stars plugin SDK not found:"
    echo "  ${PLUGIN_SDK}"
    exit 1
fi

if ! command -v cmake >/dev/null 2>&1; then
    echo "ERROR: cmake is required. Install build dependencies first."
    exit 1
fi

if ! command -v strings >/dev/null 2>&1; then
    echo "ERROR: strings is required for SDK marker verification."
    exit 1
fi

echo "Stopping ${SERVICE_NAME}..."
sudo systemctl stop "${SERVICE_NAME}"

echo "Building Astralim-enabled PowerBoxSDK..."
rm -rf build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build

if [[ ! -f "${INSTALLED_SDK}" ]]; then
    echo "ERROR: installed SDK not found after cmake --install:"
    echo "  ${INSTALLED_SDK}"
    exit 1
fi

timestamp="$(date +%Y%m%d-%H%M%S)"
backup="${PLUGIN_SDK}.backup.${timestamp}"

echo "Backing up current plugin SDK..."
sudo cp -a "${PLUGIN_SDK}" "${backup}"
echo "Backup written to:"
echo "  ${backup}"

echo "Installing Astralim-enabled SDK into Pi'n'Stars plugin directory..."
sudo cp "${INSTALLED_SDK}" "${PLUGIN_SDK}"
sudo chown pi:pi "${PLUGIN_SDK}"
sudo chmod 755 "${PLUGIN_SDK}"

echo "Verifying active plugin SDK markers..."
for marker in PinsBoxAstralim Astralim BME280 INA219; do
    if ! strings "${PLUGIN_SDK}" | grep -q "${marker}"; then
        echo "ERROR: active plugin SDK does not contain expected marker: ${marker}"
        echo "Rollback with:"
        echo "  sudo cp '${backup}' '${PLUGIN_SDK}'"
        echo "  sudo chown pi:pi '${PLUGIN_SDK}'"
        echo "  sudo chmod 755 '${PLUGIN_SDK}'"
        echo "  sudo systemctl restart '${SERVICE_NAME}'"
        exit 1
    fi
done

echo "Restarting ${SERVICE_NAME}..."
sudo systemctl restart "${SERVICE_NAME}"
RESTARTED=1

echo
echo "Astralim PowerBoxSDK reinstall complete."
echo
echo "Final checklist in Touch'n'Stars / Pi'n'Stars:"
echo "  - PINS.PowerBox visible"
echo "  - DC outputs switch"
echo "  - Dew outputs switch"
echo "  - temperature/humidity/dew point visible"
echo "  - voltage/current plausible"
