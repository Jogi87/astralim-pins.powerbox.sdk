# Reinstall Astralim PowerBoxSDK After Pi'n'Stars Updates

Pi'n'Stars updates may overwrite the active NINA plugin copy of PowerBoxSDK:

```text
/home/pi/.local/share/NINA/Plugins/3.0.0/pins.plugin/PowerBoxSDK.dll
```

When that happens, Astralim DC and dew/PWM outputs may still work through the
generic `PINS.PowerBox` path, but Astralim-specific environment telemetry can
disappear because the active plugin SDK no longer contains
`PinsBoxAstralimDevice`.

## When To Run

Run this after a Pi'n'Stars update if Touch'n'Stars still shows the power box but
no longer shows Astralim BME280 temperature, humidity, and dew point telemetry.

## Reinstall Command

From the repository root on Pi'n'Stars:

```bash
./scripts/install_astralim_pinstars_sdk.sh
```

The script:

- stops `pins.service`
- rebuilds PowerBoxSDK
- installs the SDK to `/usr/local/lib/libPowerBoxSDK.so`
- backs up the current plugin SDK as `PowerBoxSDK.dll.backup.YYYYMMDD-HHMMSS`
- copies the Astralim-enabled SDK to the active NINA plugin path
- sets ownership and permissions
- verifies Astralim marker strings are present in the active plugin SDK
- restarts `pins.service`

## Rollback

If NINA or `pins.service` fails to start after reinstalling, restore the newest
backup:

```bash
cd /home/pi/.local/share/NINA/Plugins/3.0.0/pins.plugin
latest_backup="$(ls -1t PowerBoxSDK.dll.backup.* | head -n 1)"
sudo cp "$latest_backup" PowerBoxSDK.dll
sudo chown pi:pi PowerBoxSDK.dll
sudo chmod 755 PowerBoxSDK.dll
sudo systemctl restart pins.service
```

## After Future Updates

Pi'n'Stars updates may overwrite `PowerBoxSDK.dll` again. Re-run the reinstall
script whenever Astralim-specific telemetry disappears after an update.
