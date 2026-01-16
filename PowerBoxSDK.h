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

#ifndef POWER_BOX_SDK_H
#define POWER_BOX_SDK_H

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef _WINDOWS
#define PBAPI __declspec(dllexport)
#else
#define PBAPI
#endif

#define PB_MAX_NUM 32               /* Maximum device numbers supported by this SDK */
#define PB_NAME_LEN 32              /* Buffer length for name strings */
#define PB_VERSION_LEN 32           /* Buffer length for version strings */
#define PB_UUID_LEN 37              /* Buffer length for UUID */

#define PB_NUM_POWER_PORTS 6
#define PB_NUM_USB_PORTS 6
#define PB_NUM_DEW_PORTS 2

    typedef enum _PB_ERROR_TYPE
    {
        PB_SUCCESS = 0,             /* Success */
        PB_ERROR_INVALID_ID,        /* Device ID is invalid */
        PB_ERROR_INVALID_PARAMETER, /* One or more parameters are invalid */
        PB_ERROR_INVALID_STATE,     /* Device is not in correct state for specific API call */
        PB_ERROR_COMMUNICATION,     /* Data communication error such as device has been removed from USB port */
        PB_ERROR_NULL_POINTER,      /* Caller passes null-pointer parameter which is not expected */
    } PB_ERROR_TYPE;

/*
 * Used by PBxxxSetConfig() to indicate which field wants to be set
 */
#define MASK_PB_TEMPERATURE_OFFSET 0x01 /* Temperature offset */
#define MASK_PB_HUMIDITY_OFFSET    0x02 /* Humidity offset */
#define MASK_PB_ENV_UPDATE_RATE    0x04 /* Environment update rate */
#define MASK_PB_UPDATE_RATE        0x08 /* Property update rate */
#define MASK_PB_OVERCURRENT_RESET  0x10 /* Overcurrent reset */
#define MASK_PB_ALL                0x1F /* All valid masks combined */

    typedef struct _PB_VERSION
    {
        unsigned int firmware;          /* Device firmware version */
        char model[PB_NAME_LEN];        /* Model type (e.g., "Lite", "Mini") */
        char uuid[PB_UUID_LEN];         /* Unique identifier */
        char serial[PB_VERSION_LEN];    /* Device ID */
    } PB_VERSION;

    typedef struct _PB_DEVICE_CONFIG
    {
        unsigned int mask;              /* Used by PBSetConfig() to indicate which field wants to be set */
        float temperatureOffset;        /* Temperature offset [-12.5, +12.5] [°C] */
        float humidityOffset;           /* Temperature offset [-12.5, +12.5] [%] */
        int envUpdateRate;              /* Sensor refresh rate [s], default: 3s */
        int updateRate;                 /* Property refresh rate [s], default: 1s */
        int overcurrentReset;           /* Reset overcurrent flag */
    } PB_DEVICE_CONFIG;

    typedef struct _PB_DEVICE_STATUS
    {
        int upTime;                 /* Up time [s] */
        float temperature;          /* Ambient temperature [°C] */
        float humidity;             /* Humidity [%] */
        float dewPoint;             /* Dew Point [°C] */
    } PB_DEVICE_STATUS;

    typedef struct _PB_SUPPLY_STATUS
    {
        float mainVoltage;                           /* Main supply voltage [V] */
        float usbVoltage;                            /* USB supply voltage [V] */
        float current;                               /* Total current draw [A] */
        float ampereHours;                           /* Battery capacity [Ah] */
        float wattHours;                             /* Battery energy [Wh] */
    } PB_SUPPLY_STATUS;

    typedef struct _PB_POWER_PORT_STATUS
    {
        float current[PB_NUM_POWER_PORTS];          /* Current draw [A] */
        int overcurrent[PB_NUM_POWER_PORTS];        /* Overcurrent flag */
    } PB_POWER_PORT_STATUS;

    typedef struct _PB_USB_PORT_STATUS
    {
        float current[PB_NUM_USB_PORTS];             /* Current draw [A] */
        float voltage[PB_NUM_USB_PORTS];             /* Output voltage [V] */
        int overcurrent[PB_NUM_USB_PORTS];           /* Overcurrent flag */
    } PB_USB_PORT_STATUS;

    typedef struct _PB_DEW_PORT_STATUS
    {
        int pwmResolution;                           /* PWM resolution (bit depth) */
        float current[PB_NUM_DEW_PORTS];             /* Current draw [A] */
        int overcurrent[PB_NUM_DEW_PORTS];           /* Overcurrent flag */
        float probe[PB_NUM_DEW_PORTS];               /* Probe temperature [°C] */
        int pwm[PB_NUM_DEW_PORTS];                   /* PWM value [%] */
        int state[PB_NUM_DEW_PORTS];                 /* Port state (ON/OFF) */
    } PB_DEW_PORT_STATUS;

    typedef struct _PB_BUCK_PORT_STATUS
    {
        float current;                               /* Current draw [A] */
        float voltage;                               /* Output voltage [V] */
        int overcurrent;                             /* Overcurrent flag */
        float vset;                                  /* Set voltage [V] */
        float vmin;                                  /* Minimum voltage [V] */
        float vmax;                                  /* Maximum voltage [V] */
    } PB_BUCK_PORT_STATUS;

    typedef struct _PB_PWM_PORT_STATUS
    {
        int pwmResolution;                           /* PWM resolution (bit depth) */
        float current;                               /* Current draw [A] */
        int overcurrent;                             /* Overcurrent flag */
        int pwm;                                     /* PWM value [%] */
    } PB_PWM_PORT_STATUS;

/*
 * Mask definitions for port configs - reused across different port types
 * Each structure only uses the masks that apply to its fields
 */
#define MASK_PORT_ENABLE                0x01    /* Enable/disable port */
#define MASK_PORT_BOOT_STATE            0x02    /* Port state at boot */
#define MASK_PORT_AUTO_DEW_MODE         0x04    /* Auto dew mode (dew only) */
#define MASK_PORT_AUTO_DEW_THRESHOLD    0x08    /* Auto dew threshold (dew only) */
#define MASK_PORT_POWER                 0x10    /* Power percentage (dew, pwm) */
#define MASK_PORT_VOLTAGE               0x20    /* Voltage (buck only) */
#define MASK_PORT_ALL                   0x3F    /* All valid masks combined */

    typedef struct _PB_POWER_PORT_CONFIG
    {
        unsigned int mask;      /* Which fields to update */
        unsigned int index;     /* Which port to update */
        int enabled;            /* Enable/disable each power port */
        int bootState;          /* Power port state on boot */
    } PB_POWER_PORT_CONFIG;

    typedef struct _PB_USB_PORT_CONFIG
    {
        unsigned int mask;          /* Which fields to update */
        unsigned int index;         /* Which port to update */
        int enabled;                /* Enable/disable the USB port */
        int bootState;              /* USB port state on boot */
    } PB_USB_PORT_CONFIG;

    typedef struct _PB_DEW_PORT_CONFIG
    {
        unsigned int mask;          /* Which fields to update */
        unsigned int index;         /* Which port to update */
        int enabled;                /* Enable/disable the dew heater */
        int autoMode;               /* Auto mode (automatic regulation) */
        float autoThreshold;        /* Auto dew threshold gap [°C] (dew point - probe temperature) */
        int power;                  /* Dew heater power [%] */
    } PB_DEW_PORT_CONFIG;

    typedef struct _PB_BUCK_PORT_CONFIG
    {
        unsigned int mask;          /* Which fields to update */
        float voltage;              /* Output voltage [V] */
        int enabled;                /* Enable/disable the buck converter */
        int bootState;              /* Buck converter state on boot */
    } PB_BUCK_PORT_CONFIG;

    typedef struct _PB_PWM_PORT_CONFIG
    {
        unsigned int mask;          /* Which fields to update */
        int enabled;                /* Enable/disable the PWM port */
        int power;                  /* PWM port power [%] */
    } PB_PWM_PORT_CONFIG;

        /* Device scanning and management */
    PBAPI PB_ERROR_TYPE PBScan(int *number, int *ids);
    PBAPI PB_ERROR_TYPE PBOpen(int id);
    PBAPI PB_ERROR_TYPE PBClose(int id);
    PBAPI PB_ERROR_TYPE PBGetSerial(int id, char *serial);

    /* Configuration */
    PBAPI PB_ERROR_TYPE PBGetConfig(int id, PB_DEVICE_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBSetConfig(int id, PB_DEVICE_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBGetPowerPortConfig(int id, PB_POWER_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBSetPowerPortConfig(int id, PB_POWER_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBGetUSBPortConfig(int id, PB_USB_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBSetUSBPortConfig(int id, PB_USB_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBGetDewPortConfig(int id, PB_DEW_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBSetDewPortConfig(int id, PB_DEW_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBGetBuckPortConfig(int id, PB_BUCK_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBSetBuckPortConfig(int id, PB_BUCK_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBGetPWMPortConfig(int id, PB_PWM_PORT_CONFIG *config);
    PBAPI PB_ERROR_TYPE PBSetPWMPortConfig(int id, PB_PWM_PORT_CONFIG *config);

    /* Status and information */
    PBAPI PB_ERROR_TYPE PBGetStatus(int id, PB_DEVICE_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetSupplyStatus(int id, PB_SUPPLY_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetPowerPortStatus(int id, PB_POWER_PORT_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetUSBPortStatus(int id, PB_USB_PORT_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetDewPortStatus(int id, PB_DEW_PORT_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetBuckPortStatus(int id, PB_BUCK_PORT_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetPWMPortStatus(int id, PB_PWM_PORT_STATUS *status);
    PBAPI PB_ERROR_TYPE PBGetVersion(int id, PB_VERSION *version);

    /* Utility */
    PBAPI PB_ERROR_TYPE PBRestart(int id);
    PBAPI PB_ERROR_TYPE PBFactoryReset(int id);
    PBAPI PB_ERROR_TYPE PBGetSDKVersion(char *version);

#ifdef __cplusplus
}
#endif

#endif /* POWER_BOX_SDK_H */