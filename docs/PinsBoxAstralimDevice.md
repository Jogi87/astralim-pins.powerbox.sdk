# PinsBoxAstralimDevice Proof of Concept

`PinsBoxAstralimDevice` is an Astralim Power Hat specific PowerBoxSDK device for
Pi'n'Stars / Touch'n'Stars. It keeps the already validated PowerBoxSDK control
path used by `PinsBoxCustomDevice`, but adds real BME280 and INA219 telemetry so
the SDK no longer reports dummy values for environment and current data.

No Touch'n'Stars frontend change is required for this proof of concept. The
device uses existing PowerBoxSDK status fields and port APIs.

## Hardware Mapping

| Function | GPIO / bus | SDK representation |
| --- | --- | --- |
| DC1 | GPIO26, INA219 0x41 | Power port 0 |
| DC2 | GPIO20, INA219 0x44 | Power port 1 |
| DC3 | GPIO21, INA219 0x46 | Power port 2 |
| Dew1 / Hat PWM1 | GPIO18, pwmchip0 channel 2, INA219 0x4d | Dew port 0 |
| Dew2 / Hat PWM2 | GPIO13, pwmchip0 channel 1, INA219 0x49 | Dew port 1 |
| System supply | INA219 0x40 | Supply voltage/current |
| Environment | BME280 0x76 | Temperature, humidity, dew point |

The PWM period is fixed at `1000000 ns` for a 1000 Hz carrier. Dew ports use the
same 8-bit duty semantics as the existing SDK dew ports.

Astralim currently exposes BME280 environment temperature, humidity, and
calculated dew point through the SDK environment status fields. Dew probe
temperatures are not supported unless dedicated probe sensors are added later,
so dew probe status remains at the SDK no-sensor value.

## Scan Behavior

`PBScan()` probes `PinsBoxAstralimDevice` after the fixed PINS devices and before
`PinsBoxCustomDevice`. This makes Astralim hardware prefer the sensor-aware
device instead of the generic custom-device route when the expected hardware is
present.

The proof-of-concept scan requires:

- `/dev/i2c-1`
- `/sys/class/pwm/pwmchip0`
- BME280 chip ID response at I2C address `0x76`
- INA219 response at I2C address `0x40`

## Pi'n'Stars / Debian Build

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config libudev-dev libssl-dev libjsoncpp-dev libgpiod-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

## Pi'n'Stars Plugin Deployment

The active Pi'n'Stars / NINA plugin currently loads PowerBoxSDK from the plugin
directory, so the tested SDK library was deployed manually after installation:

```bash
sudo cp /usr/local/lib/libPowerBoxSDK.so /home/pi/.local/share/NINA/Plugins/3.0.0/pins.plugin/PowerBoxSDK.dll
sudo systemctl restart pins.service
```

Before replacing the plugin copy, keep a backup of the previous
`PowerBoxSDK.dll`. If NINA or `pins.service` fails to start after the update,
restore the backup and restart `pins.service`.

## Hardware Validation

Pi'n'Stars hardware validation succeeded with this proof of concept:

- Build succeeds on Raspberry Pi / Pi'n'Stars.
- `cmake --install` succeeds after the CMake install header fixes.
- The new `libPowerBoxSDK.so` was copied into the NINA `pins.plugin` directory
  as `PowerBoxSDK.dll`.
- `pins.service` starts successfully.
- NINA reports `Found 1 PINS.PowerBox Switch Hubs`.
- Touch'n'Stars shows and controls the Astralim PowerBox.
- DC outputs switch correctly.
- Dew/PWM outputs switch correctly.
- BME280 environment telemetry is shown:
  - temperature
  - humidity
  - dew point
- Dew probe temperature is no longer incorrectly filled with dew point.
- Voltage/current telemetry appears plausible.
- No Touch'n'Stars frontend changes were required.
- No Astralim INDI driver changes were required.

## Hardware Test Plan

1. Confirm boot config includes:

   ```text
   dtparam=i2c_arm=on
   dtoverlay=pwm-2chan,pin=18,func=2,pin2=13,func2=4
   ```

2. Confirm hardware visibility:

   ```bash
   test -e /dev/i2c-1
   test -e /sys/class/pwm/pwmchip0
   i2cdetect -y 1
   ```

   Expected I2C addresses include `0x40`, `0x41`, `0x44`, `0x46`, `0x49`,
   `0x4d`, and `0x76`.

3. Build and install PowerBoxSDK.
4. Restart `pins.service`.
5. Verify logs show Astralim / `PinsBoxAstralimDevice` detection.
6. Verify in Touch'n'Stars:
   - DC1/DC2/DC3 switch correctly.
   - Dew1/Dew2 PWM works.
   - Temperature is no longer `-127`.
   - Humidity is no longer `-127`.
   - Dew point is plausible.
   - Supply voltage/current are plausible.
   - Port currents change when loads are switched.

## Known Risks

- INA219 shunt values are based on the existing Astralim INDI implementation:
  `0.005 ohm` for system supply and `0.01 ohm` for output channels.
- Longer-term voltage/current calibration should be verified against known loads
  and measurement equipment.
- The Astralim scan path intentionally takes precedence over
  `PinsBoxCustomDevice` when Astralim hardware is detected.
