/* *******************************************************************************
 * MIT License
 *
 * Copyright (c) 2026 Nico Trost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 * **************************************************************************** */

#include "PowerBoxSDK.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void PrintMenu()
{
    printf("\n=== Power Box Control Menu ===\n");
    printf("1. Get device status\n");
    printf("2. Get device configuration\n");
    printf("3. Set device configuration\n");
    printf("4. Open device\n");
    printf("5. Close device\n");
    printf("6. Restart device\n");
    printf("7. Factory reset device\n");
    printf("8. Get supply status\n");    printf("9. Get port status\n");    printf("10. Refresh and display all info\n");
    printf("11. Exit\n");
    printf("> ");
}

void DisplayStatus(int deviceId)
{
    PB_DEVICE_STATUS status;
    PB_ERROR_TYPE result = PBGetStatus(deviceId, &status);
    if (result == PB_SUCCESS)
    {
        printf("\n--- Device Status ---\n");
        printf("Up time: %d s\n", status.upTime);
        printf("Temperature: %.2f °C\n", status.temperature);
        printf("Humidity: %.2f %%\n", status.humidity);
        printf("Dew Point: %.2f °C\n", status.dewPoint);
    }
    else
    {
        printf("[FAIL] Failed to get status (Error: %d)\n", result);
    }
} 

void DisplayConfig(int deviceId)
{
    PB_DEVICE_CONFIG config_v;
    PB_ERROR_TYPE result = PBGetConfig(deviceId, &config_v);
    if (result == PB_SUCCESS)
    {
        printf("\n--- Current Configuration ---\n");
        printf("Mask: 0x%X\n", config_v.mask);
        printf("Temperature Offset: %.2f °C\n", config_v.temperatureOffset);
        printf("Humidity Offset: %.2f %%\n", config_v.humidityOffset);
        printf("Update Rate: %d s\n", config_v.envUpdateRate);
    }
    else
    {
        printf("[FAIL] Failed to get config (Error: %d)\n", result);
    }
}

void DisplaySupplyStatus(int deviceId)
{
    PB_SUPPLY_STATUS supply;
    PB_ERROR_TYPE result = PBGetSupplyStatus(deviceId, &supply);
    if (result == PB_SUCCESS)
    {
        printf("\n--- Supply Status ---\n");
        printf("Main Voltage: %.2f V\n", supply.mainVoltage);
        printf("USB Voltage: %.2f V\n", supply.usbVoltage);
        printf("Current: %.2f A\n", supply.current);
        printf("Ampere Hours: %.2f Ah\n", supply.ampereHours);
        printf("Watt Hours: %.2f Wh\n", supply.wattHours);
    }
    else
    {
        printf("[FAIL] Failed to get supply status (Error: %d)\n", result);
    }
}

void DisplayPortStatus(int deviceId)
{
    printf("\n--- Power Port Status ---\n");
    PB_POWER_PORT_STATUS powerPorts;
    PB_ERROR_TYPE result = PBGetPowerPortStatus(deviceId, &powerPorts);
    if (result == PB_SUCCESS)
    {
        for (int i = 0; i < PB_NUM_POWER_PORTS; ++i)
        {
            const char *overcurrent = powerPorts.overcurrent[i] ? "[OVERCURRENT]" : "OK";
            printf("Power Port %d: %.3f A - %s\n", i + 1, powerPorts.current[i], overcurrent);
        }
    }
    else
    {
        printf("[FAIL] Failed to get power port status (Error: %d)\n", result);
    }

    printf("\n--- USB Port Status ---\n");
    PB_USB_PORT_STATUS usbPorts;
    result = PBGetUSBPortStatus(deviceId, &usbPorts);
    if (result == PB_SUCCESS)
    {
        for (int i = 0; i < PB_NUM_USB_PORTS; ++i)
        {
            const char *overcurrent = usbPorts.overcurrent[i] ? "[OVERCURRENT]" : "OK";
            printf("USB Port %d: %.3f A, %.2f V - %s\n", i + 1, usbPorts.current[i], usbPorts.voltage[i], overcurrent);
        }
    }
    else
    {
        printf("[FAIL] Failed to get USB port status (Error: %d)\n", result);
    }

    printf("\n--- Dew Port Status ---\n");
    PB_DEW_PORT_STATUS dewPorts;
    result = PBGetDewPortStatus(deviceId, &dewPorts);
    if (result == PB_SUCCESS)
    {
        for (int i = 0; i < PB_NUM_DEW_PORTS; ++i)
        {
            const char *state = dewPorts.state[i] ? "ON" : "OFF";
            const char *overcurrent = dewPorts.overcurrent[i] ? "[OVERCURRENT]" : "OK";
            printf("Dew Port %d: %.3f A, Probe Temp: %.2f °C, PWM: %d%%, State: %s - %s\n", 
                   i + 1, dewPorts.current[i], dewPorts.probe[i], dewPorts.pwm[i], state, overcurrent);
        }
    }
    else
    {
        printf("[FAIL] Failed to get dew port status (Error: %d)\n", result);
    }

    printf("\n--- Auxiliary Ports ---\n");
    PB_BUCK_PORT_STATUS buckPort;
    result = PBGetBuckPortStatus(deviceId, &buckPort);
    if (result == PB_SUCCESS)
    {
        const char *buckOvercurrent = buckPort.overcurrent ? "[OVERCURRENT]" : "OK";
        printf("Buck Port: %.3f A, %.2f V (Set: %.2f V, Min: %.2f V, Max: %.2f V) - %s\n", 
               buckPort.current, buckPort.voltage, buckPort.vset, buckPort.vmin, buckPort.vmax, buckOvercurrent);
    }
    else
    {
        printf("[FAIL] Failed to get buck port status (Error: %d)\n", result);
    }

    PB_PWM_PORT_STATUS pwmPort;
    result = PBGetPWMPortStatus(deviceId, &pwmPort);
    if (result == PB_SUCCESS)
    {
        const char *pwmOvercurrent = pwmPort.overcurrent ? "[OVERCURRENT]" : "OK";
        printf("PWM Port: %.3f A, PWM: %d%% - %s\n", pwmPort.current, pwmPort.pwm, pwmOvercurrent);
    }
    else
    {
        printf("[FAIL] Failed to get PWM port status (Error: %d)\n", result);
    }
}

void SetConfigInteractive(int deviceId)
{
    PB_DEVICE_CONFIG curCfg;
    if (PBGetConfig(deviceId, &curCfg) != PB_SUCCESS)
    {
        printf("[FAIL] Could not read current configuration\n");
        return;
    }

    PB_DEVICE_CONFIG updates = {};
    unsigned int mask = 0;
    char input[128];

    printf("\nSet new configuration values (leave empty to keep current)\n");

    printf("Temperature Offset (current: %.4f): ", curCfg.temperatureOffset);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.temperatureOffset = (float)atof(input);
        mask |= MASK_PB_TEMPERATURE_OFFSET;
    }

    printf("Humidity Offset (current: %.4f): ", curCfg.humidityOffset);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.humidityOffset = (float)atof(input);
        mask |= MASK_PB_HUMIDITY_OFFSET;
    }

    printf("Environment Update Rate in seconds (current: %d): ", curCfg.envUpdateRate);
    fflush(stdout);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n')
    {
        updates.updateRate = atoi(input);
        mask |= MASK_PB_UPDATE_RATE;
    }

    if (mask == 0)
    {
        printf("No changes provided; nothing to set.\n");
        return;
    }

    updates.mask = mask;
    PB_ERROR_TYPE r = PBSetConfig(deviceId, &updates);
    if (r == PB_SUCCESS)
    {
        printf("Configuration updated successfully.\n");
    }
    else
    {
        printf("Failed to set configuration (Error: %d)\n", r);
    }
}

void DisplayAllInfo(int deviceId)
{
    PB_VERSION version;
    PB_ERROR_TYPE result = PBGetVersion(deviceId, &version);
    if (result == PB_SUCCESS)
    {
        printf("\n=== Device Information ===\n");
        printf("Model: %s\n", version.model);
        printf("Firmware: %u\n", version.firmware);
        printf("UUID: %s\n", version.uuid);
        printf("DeviceID: %s\n", version.serial);
    }
    else
    {
        printf("[FAIL] Failed to get version (Error: %d)\n", result);
        return;
    }

    DisplayStatus(deviceId);
    DisplayConfig(deviceId);
}

int main(int argc, char *argv[])
{
    printf("=== Power Box Interactive Control ===\n\n");

    /* Get SDK version */
    char sdkVersion[32];
    PB_ERROR_TYPE result = PBGetSDKVersion(sdkVersion);
    if (result == PB_SUCCESS)
    {
        printf("SDK Version: %s\n\n", sdkVersion);
    }

    /* Scan for devices */
    printf("Scanning for Power Box devices...\n");
    int deviceCount = 32;
    int deviceIds[32] = {0};

    result = PBScan(&deviceCount, deviceIds);
    if (result != PB_SUCCESS)
    {
        printf("[FAIL] Scan failed (Error: %d)\n", result);
        return 1;
    }

    printf("[OK] Found %d device(s)\n\n", deviceCount);

    if (deviceCount == 0)
    {
        printf("No Power Box devices found. Please connect a device.\n");
        return 0;
    }

    /* List found devices */
    printf("Available devices:\n");
    for (int i = 0; i < deviceCount; i++)
    {
        printf("  [%d] Device ID: %d\n", i, deviceIds[i]);
    }
    printf("\n");

    /* Select device */
    int deviceId = deviceIds[0];
    if (deviceCount > 1)
    {
        printf("Enter device index to use (0-%d) [default: 0]: ", deviceCount - 1);
        char input[10];
        if (fgets(input, sizeof(input), stdin) != NULL)
        {
            int idx = atoi(input);
            if (idx >= 0 && idx < deviceCount)
            {
                deviceId = deviceIds[idx];
            }
        }
    }

    printf("Using device: %d\n\n", deviceId);

    /* Start with device closed; user may open via menu option 4 */
    bool deviceOpened = false;
    printf("Device is currently closed. Use menu option 4 to open it.\n\n");

    /* Interactive menu */
    char input[256];
    bool running = true;

    while (running)
    {
        PrintMenu();
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            break;
        }

        int choice = atoi(input);

        switch (choice)
        {
        case 1:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayStatus(deviceId);
            }
            break;

        case 2:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayConfig(deviceId);
            }
            break;

        case 3:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                SetConfigInteractive(deviceId);
            }
            break;

        case 4:
            if (deviceOpened)
            {
                printf("Device already open.\n");
            }
            else
            {
                printf("Opening device...\n");
                result = PBOpen(deviceId);
                if (result == PB_SUCCESS)
                {
                    deviceOpened = true;
                    printf("[OK] Device opened\n");
                }
                else
                {
                    printf("[FAIL] Failed to open device (Error: %d)\n", result);
                }
            }
            break;

        case 5:
            if (!deviceOpened)
            {
                printf("Device already closed.\n");
            }
            else
            {
                printf("Closing device...\n");
                result = PBClose(deviceId);
                if (result == PB_SUCCESS)
                {
                    deviceOpened = false;
                    printf("[OK] Device closed\n");
                }
                else
                {
                    printf("[FAIL] Failed to close device (Error: %d)\n", result);
                }
            }
            break;

        case 6:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                printf("Restarting device...\n");
                result = PBRestart(deviceId);
                if (result == PB_SUCCESS)
                {
                    printf("[OK] Restart command sent\n");
                }
                else
                {
                    printf("[FAIL] Failed to restart device (Error: %d)\n", result);
                }
            }
            break;

        case 7:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                printf("Factory reset will restore device to defaults. Are you sure? (y/N): ");
                fflush(stdout);
                char conf[8];
                if (fgets(conf, sizeof(conf), stdin) && (conf[0] == 'y' || conf[0] == 'Y'))
                {
                    printf("Performing factory reset...\n");
                    result = PBFactoryReset(deviceId);
                    if (result == PB_SUCCESS)
                    {
                        printf("[OK] Factory reset command sent\n");
                    }
                    else
                    {
                        printf("[FAIL] Failed to factory reset device (Error: %d)\n", result);
                    }
                }
                else
                {
                    printf("Factory reset cancelled.\n");
                }
            }
            break;

        case 8:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplaySupplyStatus(deviceId);
            }
            break;

        case 9:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayPortStatus(deviceId);
            }
            break;

        case 10:
            if (!deviceOpened)
            {
                printf("Device not opened. Use option 4 to open it first.\n");
            }
            else
            {
                DisplayAllInfo(deviceId);
            }
            break;

        case 11:
            running = false;
            break;

        default:
            printf("[FAIL] Unknown command\n");
            break;
        }
    }

    /* Close device if still open */
    if (deviceOpened)
    {
        printf("\nClosing device...\n");
        result = PBClose(deviceId);
        if (result == PB_SUCCESS)
        {
            printf("[OK] Device closed\n");
        }
        else
        {
            printf("[FAIL] Failed to close device (Error: %d)\n", result);
        }
    } else {
        printf("\nDevice was already closed.\n");
    }

    printf("\n=== Test Complete ===\n");
    return 0;
}
