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

#include "PinsBoxDevice.h"
#include "PowerBoxLogging.h"
#include "MCP320X.h"
#include "Device.h"
#include <filesystem>
#include <mutex>
#include <fstream>
#include <sys/sysinfo.h>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <json/json.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

namespace PowerBox
{
    static constexpr unsigned int NUM_PINS = 30;
    static constexpr uint8_t PINS[NUM_PINS] = {
        22, // USB_EN1
        27, // USB_EN2
        20, // USB_EN3
        23, // USB_EN4
        25, // USB_EN5
        24, // USB_EN6
        26, // USB_EN7
        21, // USB_EN8
        16, // SPEAKER
        4,  // EN_BUCK
        55, // port 2: P17
        54, // port 3: P16
        51, // port 4: P13
        50, // port 5: P12
        48, // port 6: P10
        41, // port 7: P01
        40, // port 8: P00
        43, // dew 1: P03
        45, // dew 2: P05
        47, // pwm: P07
        53, // DEN12V_12
        52, // DEN12V_34
        49, // DEN12V_56
        42, // DEN12V_78
        46, // DEN12V_IN
        44, // DEN12_DEW
         3, // DEN12_ADJ
         6, // TEMP1_DEW
         5, // TEMP2_DEW
         2, // DSEL
    };

    static const uint8_t *PWR_PINS = &PINS[10];
    static const uint8_t *USB_PINS = &PINS[0];
    static const uint8_t *DEW_PINS = &PINS[17];
    static const uint8_t BUCK_PIN = PINS[9];
    static const uint8_t PWM_PIN = PINS[19];
    static const uint8_t DSEL_PIN = PINS[29];
    static const uint8_t INDEN_PIN = PINS[24];
    static const uint8_t *PWRDEN_PINS = &PINS[20];
    static const uint8_t DEWDEN_PIN = PINS[25];
    static const uint8_t ADJDEN_PIN = PINS[26];
    static const uint8_t *DS18_PINS = &PINS[27];

    PinsBoxDevice::PinsBoxDevice(void)
    {
        this->statusListenerRunning = false;
        this->temperature = DEVICE_DISCONNECTED_C;
        this->humidity = DEVICE_DISCONNECTED_C;
        this->dewPoint = DEVICE_DISCONNECTED_C;
        this->extSensor = false;

        // Initial reset
        this->ResetProperties();
        
        // Load settings from file at construction time
        this->LoadSettings();

        this->dht_ = new DHT22("device0");

        this->gpio_ = new GPIOManager("/dev/gpiochip0",
                                      "/dev/i2c-10",
                                      0x20,
                                      0,
                                      nullptr,
                                      NUM_PINS,
                                      PINS);

        this->gpio_->begin();

        // ADC
        this->adc_ = new MCP3204(*this->gpio_);
        this->adc_->begin(3.3f);

        // Supply
        this->supply_ = new BTS7006<MCP3204>(*this->gpio_, *this->adc_);
        this->supply_->begin(1200, INDEN_PIN, 255, DSEL_PIN, 0);
        this->supply_->setMaxCurrent(12.f);
        this->supply_->setChannel(2);
        this->supply_->setSampling(100, 50);

        // PWR ports
        this->pwr_ = new BTS7012<MCP3204>*[PINSBOX_NUM_POWER_PORTS];
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i)
        {
            this->pwr_[i] = new BTS7012<MCP3204>(*this->gpio_, *this->adc_);
        }

        // Initialize PWR ports
        this->pwr_[0]->begin(1200, PWRDEN_PINS[0], 255, DSEL_PIN, 0);
        this->pwr_[1]->begin(1200, PWRDEN_PINS[0], PWR_PINS[0], DSEL_PIN, 1);
        this->pwr_[2]->begin(1200, PWRDEN_PINS[1], PWR_PINS[1], DSEL_PIN, 0);
        this->pwr_[3]->begin(1200, PWRDEN_PINS[1], PWR_PINS[2], DSEL_PIN, 1);
        this->pwr_[4]->begin(1200, PWRDEN_PINS[2], PWR_PINS[3], DSEL_PIN, 0);
        this->pwr_[5]->begin(1200, PWRDEN_PINS[2], PWR_PINS[4], DSEL_PIN, 1);
        this->pwr_[6]->begin(1200, PWRDEN_PINS[3], PWR_PINS[5], DSEL_PIN, 0);
        this->pwr_[7]->begin(1200, PWRDEN_PINS[3], PWR_PINS[6], DSEL_PIN, 1);

        // PWR boot state
        for(int i = 1; i < PINSBOX_NUM_POWER_PORTS; ++i)
        {
            this->pwr_[i]->setState(this->powerBootstrap[i]);
        }

        // PWR sense sample rate and max current
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i)
        {
            this->pwr_[i]->setMaxCurrent(6.f);
            this->pwr_[i]->setChannel(2);
            this->pwr_[i]->setSampling(10, 1);
        }

        // USB ports
        this->usb_ = new USB*[PINSBOX_NUM_USB_PORTS];
        for(int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i) {
            this->usb_[i] = new USB(*this->gpio_, "/dev/i2c-10");
        }

        // Initialize USB ports
        this->usb_[0]->begin(USB_PINS[0], 0x40, 2.6f, this->usbBootstrap[0]);
        this->usb_[1]->begin(USB_PINS[1], 0x41, 2.6f, this->usbBootstrap[1]);
        this->usb_[2]->begin(USB_PINS[2], 0x42, 2.0f, this->usbBootstrap[2]);
        this->usb_[3]->begin(USB_PINS[3], 0x43, 2.0f, this->usbBootstrap[3]);
        this->usb_[5]->begin(USB_PINS[4], 0x44, 1.2f, this->usbBootstrap[4]);
        this->usb_[4]->begin(USB_PINS[5], 0x45, 1.2f, this->usbBootstrap[5]);
        this->usb_[6]->begin(USB_PINS[7], 0x47, 1.2f, this->usbBootstrap[6]);
        this->usb_[7]->begin(USB_PINS[6], 0x46, 1.2f, this->usbBootstrap[7]);

        // Dew ports
        this->dew_ = new DewPort*[PINSBOX_NUM_DEW_PORTS];
        this->dew_[0] = new DewPort(*this->gpio_, *this->adc_, "pwmchip0", 1);
        this->dew_[1] = new DewPort(*this->gpio_, *this->adc_, "pwmchip1", 0);

        // Initialize Dew ports
        this->dew_[0]->begin(DEWDEN_PIN, DEW_PINS[1], DSEL_PIN, DS18_PINS[0], 1, 400, this->dewAuto[0]);
        this->dew_[1]->begin(DEWDEN_PIN, DEW_PINS[0], DSEL_PIN, DS18_PINS[1], 0, 400, this->dewAuto[1]);

        // Initialize adjustable ports
        this->buck_ = new BuckPort(*this->gpio_, *this->adc_, "/dev/i2c-10", 0x60);
        this->buck_->begin(ADJDEN_PIN, BUCK_PIN, DSEL_PIN, 0);
        //this->buck_->setState(this->buckBootstrap); TODO store last voltage

        this->pwm_ = new PWMPort(*this->gpio_, *this->adc_);
        this->pwm_->begin(ADJDEN_PIN, PWM_PIN, DSEL_PIN, 1, 60000);
        this->pwm_->setState(0, 0.f);
    }

    PinsBoxDevice::~PinsBoxDevice(void)
    {
        // Stop listener if running
        this->StopStatusListener();

        // Clean up DHT22 sensor
        if(this->dht_) {
            delete this->dht_;
            this->dht_ = nullptr;
        }

        // Clean up supply monitoring
        if(this->supply_) {
            delete this->supply_;
            this->supply_ = nullptr;
        }

        // Clean up power ports
        if(this->pwr_) {
            for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i) {
                if(this->pwr_[i]) {
                    delete this->pwr_[i];
                    this->pwr_[i] = nullptr;
                }
            }
            delete[] this->pwr_;
            this->pwr_ = nullptr;
        }

        // Clean up USB ports
        if(this->usb_) {
            for(int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i) {
                if(this->usb_[i]) {
                    delete this->usb_[i];
                    this->usb_[i] = nullptr;
                }
            }
            delete[] this->usb_;
            this->usb_ = nullptr;
        }

        // Clean up dew ports
        if(this->dew_) {
            for(int i = 0; i < PINSBOX_NUM_DEW_PORTS; ++i) {
                if(this->dew_[i]) {
                    delete this->dew_[i];
                    this->dew_[i] = nullptr;
                }
            }
            delete[] this->dew_;
            this->dew_ = nullptr;
        }

        // Clean up adjustable ports
        if(this->buck_) {
            delete this->buck_;
            this->buck_ = nullptr;
        }

        if(this->pwm_) {
            delete this->pwm_;
            this->pwm_ = nullptr;
        }

        // Clean up ADC
        if(this->adc_) {
            delete this->adc_;
            this->adc_ = nullptr;
        }

        // Clean up GPIO manager (must be last)
        if(this->gpio_) {
            delete this->gpio_;
            this->gpio_ = nullptr;
        }
    }

    void PinsBoxDevice::ResetProperties(void)
    {
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i)
        {
            this->powerBootstrap[i] = 0;
        }

        for(int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i)
        {
            this->usbBootstrap[i] = 0;
        }

        for(int i = 0; i < PINSBOX_NUM_DEW_PORTS; ++i)
        {
            this->dewAuto[i] = 1;
            this->dewThreshold[i] = 4.f;
        }

        this->buckBootstrap = 0;
    }

    void PinsBoxDevice::StartStatusListener(void)
    {
        /* Stop any existing listener by setting the flag */
        this->statusListenerRunning = false;

        /* Small delay to let old thread exit if it's still running */
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        /* Start new listener thread */
        this->statusListenerRunning = true;
        std::thread listenerThread(&PinsBoxDevice::StatusListenerThreadFunc, this);
        listenerThread.detach(); /* Detach immediately - let it run independently */
        PB_DEBUG("StartStatusListener: Listener thread started");
    }

    void PinsBoxDevice::StopStatusListener(void)
    {
        /* Signal listener thread to stop */
        this->statusListenerRunning = false;
        PB_DEBUG("StopStatusListener: Listener stop requested");
    }

    /* Background listener thread function for status messages */
    void PinsBoxDevice::StatusListenerThreadFunc()
    {
        PB_DEBUG("StatusListener: starting");

        while(this->statusListenerRunning)
        {
            // Fetch DHT22 sensor data
            this->GetDHT22Data();

            // Fetch MCP3208 data
            this->GetMCP3208Data();

            // 1 sec delay before we query again
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        PB_DEBUG("StatusListener: exiting");
    }

    PB_ERROR_TYPE PinsBoxDevice::Open(void)
    {
        PB_DEBUG("PBOpen: Found PI'N'Stars power box device");

        // Fetch power ports config
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i) {
            this->GetPowerState(i);
        }

        // Fetch usb ports config
        for(int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i) {
            this->GetUSBState(i);
        }

        // Fetch dew ports config
        this->dewPwmResolution = 8;
        for(int i = 0; i < PINSBOX_NUM_DEW_PORTS; ++i) {
            this->GetDewState(i);
        }

        // Fetch buck port config
        this->buckVset = 1.f;
        this->buckVmin = 1.f;
        this->buckVmax = 12.f;//TODO
        this->GetBuckState();

        // Fetch pwm port config
        this->pwmPwmResolution = 8;
        this->GetPWMState();

        this->isOpen = true;
        PB_INFO("[OK] Device opened");
        return PB_SUCCESS;
    }

    void PinsBoxDevice::Close(void)
    {
        this->isOpen = false;
        PB_DEBUG("[OK] Device closed");
    }

    std::string PinsBoxDevice::GetFullSerial(void)
    {
        std::ifstream file("/proc/device-tree/serial-number");
        if(!file.is_open())
        {
            return nullptr;
        }

        std::string serial;
        std::getline(file, serial);

        return serial;
    }

    std::string PinsBoxDevice::GetSerial(void)
    {
        std::string serial = this->GetFullSerial();
        return serial.substr(serial.length() - 7);
    }

    int PinsBoxDevice::GetUpTime(void)
    {
        struct sysinfo info;
        if(sysinfo(&info) == 0)
        {
            return info.uptime;
        }

        return 0;
    }

    float PinsBoxDevice::GetCoreTemp(void)
    {
        float temp = DEVICE_DISCONNECTED_C;
        std::ifstream ifs("/sys/class/thermal/thermal_zone0/temp");

        if (ifs)
        {
            std::string line;
            if (std::getline(ifs, line))
            {
                // Convert millidegrees to degrees
                temp = std::stoi(line) / 1000.f;
            }
        }

        return temp;
    }

    float PinsBoxDevice::GetSupply12AverageA(void)
    {
        int uptimeSeconds = this->GetUpTime();
        if (uptimeSeconds <= 0)
        {
            return 0.0f;
        }
        
        float uptimeHours = uptimeSeconds / 3600.0f;
        return this->supply12Ah / uptimeHours;
    }

    bool PinsBoxDevice::SetTemperatureOffset(float val)
    {
        this->temperatureOffset = val;
        SaveSettings();
        return true;
    }

    bool PinsBoxDevice::SetExtTemperature(float val)
    {
        if(val > -50.f && val < 80.f)
        {
            this->extTemperature = val;
            this->extSensor = true;
            return true;
        }
        return false;
    }

    bool PinsBoxDevice::SetHumidityOffset(float val)
    {
        this->humidityOffset = val;
        SaveSettings();
        return true;
    }

    bool PinsBoxDevice::SetExtHumidity(float val)
    {
        if(val > 0.0f && val <= 100.0f)
        {
            this->extHumidity = val;
            this->extSensor = true;
            return true;
        }
        return false;
    }

    void PinsBoxDevice::LoadSettings(void)
    {
        std::ifstream file("/home/pi/pins/deviceSettings.json");
        if (!file.is_open()) {
            PB_DEBUG("Settings file not found, creating with defaults");
            SaveSettings();
            return;
        }
        
        Json::Value root;
        file >> root;
        file.close();
        
        // Load environment settings
        if (root.isMember("environment")) {
            this->temperatureOffset = root["environment"].get("temperatureOffset", 0.0).asFloat();
            this->humidityOffset = root["environment"].get("humidityOffset", 0.0).asFloat();
        }

        // Load power port bootstrap states
        if (root.isMember("powerPorts")) {
            Json::Value powerPorts = root["powerPorts"];
            this->powerBootstrap[0] = true;
            for (int i = 1; i < PINSBOX_NUM_POWER_PORTS && i < (int)powerPorts.size(); i++) {
                int idx = powerPorts[i].get("port", 0).asInt();
                this->powerBootstrap[idx] = powerPorts[i].get("bootState", 0).asInt();
            }
        }

        // Load USB port bootstrap states
        if (root.isMember("usbPorts")) {
            Json::Value usbPorts = root["usbPorts"];
            for (int i = 0; i < PINSBOX_NUM_USB_PORTS && i < (int)usbPorts.size(); i++) {
                int idx = usbPorts[i].get("port", 0).asInt();
                this->usbBootstrap[idx] = usbPorts[i].get("bootState", 0).asInt();
            }
        }

        // Load Dew port auto mode states
        if (root.isMember("dewPorts")) {
            Json::Value dewPorts = root["dewPorts"];
            for (int i = 0; i < PINSBOX_NUM_DEW_PORTS && i < (int)dewPorts.size(); i++) {
                int idx = dewPorts[i].get("port", 0).asInt();
                this->dewAuto[idx] = dewPorts[i].get("autoMode", 1).asInt();
                this->dewThreshold[idx] = dewPorts[i].get("autoThreshold", 4.0).asFloat();
            }
        }

        // Load Buck port bootstrap states
        if (root.isMember("buckPorts")) {
            Json::Value buckPorts = root["buckPorts"];
            this->buckBootstrap = buckPorts[0].get("bootState", 0).asInt();
            this->buckVset = buckPorts[0].get("targetVoltage", 0).asFloat();
        }
    }

    void PinsBoxDevice::SaveSettings(void)
    {
        Json::Value root;
        
        // Environment settings
        root["environment"]["temperatureOffset"] = this->temperatureOffset;
        root["environment"]["humidityOffset"] = this->humidityOffset;
        
        // Power port bootstrap states
        root["powerPorts"][0]["port"] = 0;
        root["powerPorts"][0]["bootState"] = 1;
        for (int i = 1; i < PINSBOX_NUM_POWER_PORTS; i++) {
            root["powerPorts"][i]["port"] = i;
            root["powerPorts"][i]["bootState"] = this->powerBootstrap[i];
        }

        // USB port bootstrap states
        for (int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i) {
            root["usbPorts"][i]["port"] = i;
            root["usbPorts"][i]["bootState"] = this->usbBootstrap[i];
        }
        
        // Dew port automode states
        for (int i = 0; i < PINSBOX_NUM_DEW_PORTS; ++i) {
            root["dewPorts"][i]["port"] = i;
            root["dewPorts"][i]["autoMode"] = this->dewAuto[i];
            root["dewPorts"][i]["autoThreshold"] = this->dewThreshold[i];
        }

        // Buck port bootstrap states
        root["buckPorts"][0]["port"] = 0;
        root["buckPorts"][0]["bootState"] = this->buckBootstrap;
        root["buckPorts"][0]["targetVoltage"] = this->buckVset;

        std::ofstream file("/home/pi/pins/deviceSettings.json");
        if (file.is_open()) {
            file << root;
            file.close();
            PB_DEBUG("Settings saved");
        }
    }

    void PinsBoxDevice::GetDHT22Data(void)
    {
        float currentTemperature = this->dht_->getTemperature();
        float currentHumidity = this->dht_->getHumidity();

        if(std::isnan(currentTemperature) ||
           std::isnan(currentHumidity) ||
           currentTemperature == DEVICE_DISCONNECTED_C ||
           currentHumidity == DEVICE_DISCONNECTED_C)
        {
            if(std::isnan(this->extTemperature) ||
               std::isnan(this->extHumidity) ||
               this->extTemperature == DEVICE_DISCONNECTED_C ||
               this->extHumidity == DEVICE_DISCONNECTED_C)
            {
                this->temperature = DEVICE_DISCONNECTED_C;
                this->humidity = DEVICE_DISCONNECTED_C;
                this->dewPoint = DEVICE_DISCONNECTED_C;
                this->extSensor = false;
                return;
            }

            // If we have externally set values, use them
            currentTemperature = this->extTemperature;
            currentHumidity = this->extHumidity;
            this->extSensor = true;
        }
        else
        {
            // We have valid environment data, disable external sensor
            this->extSensor = false;
        }

        // Apply temperature offset (calibration correction)
        currentTemperature += this->temperatureOffset;
        currentHumidity += this->humidityOffset;

        // Clamp humidity to physically meaningful range to avoid log10(<=0) and NaNs
        if(std::isnan(currentHumidity) || currentHumidity < 0.1f) currentHumidity = 0.1f;
        if(currentHumidity > 100.0f) currentHumidity = 100.0f;

        // Calculate dew point
        float tmp = 0.66077f + 7.5f * currentTemperature / (237.3f + currentTemperature) + (log10(currentHumidity) - 2.f);
        float currentDewPoint = (tmp - 0.66077f) * 237.3f / (0.66077f + 7.5f - tmp);

        this->temperature = currentTemperature;
        this->humidity = currentHumidity;
        this->dewPoint = std::isnan(currentDewPoint) ? DEVICE_DISCONNECTED_C : currentDewPoint;
        this->dewPoint = std::min(this->dewPoint, currentTemperature);

        // Reset external values
        this->extTemperature = DEVICE_DISCONNECTED_C;
        this->extHumidity = DEVICE_DISCONNECTED_C;
    }

    void PinsBoxDevice::GetMCP3208Data(void)
    {
        // Supply
        this->supply_->measureCurrent();
        this->supply12A = this->supply_->getCurrent_mA() * 0.001f;
        this->supply12V = this->adc_->analogReadAverage(1, 20, 0) * (36e3 + 4.7e3) / 4.7e3 * 0.001f;
        this->supply5V = this->adc_->analogReadAverage(0, 20, 0) * (35.7e3 + 10e3) / 10e3 * 0.001f;

        // Accumulate energy (called every 1 second)
        // Time delta: 1 second = 1/3600 hours
        float timeDeltaHours = 1.0f / 3600.0f;
        
        // Ah = Amps * Hours
        this->supply12Ah += this->supply12A * timeDeltaHours;
        
        // Wh = Volts * Amps * Hours
        this->supply12Wh += this->supply12V * this->supply12A * timeDeltaHours;

        // PWR ports
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i)
        {
            this->pwr_[i]->measureCurrent();
            this->powerCurrent[i] = this->pwr_[i]->getCurrent_mA() * 0.001f;
            this->powerOvercurrent[i] = this->pwr_[i]->getOverCurrent() ? 1 : 0;
        }
        
        for(int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i)
        {
            this->usb_[i]->measureVoltage();
            this->usb_[i]->measureCurrent();
            
            this->usbCurrent[i] = this->usb_[i]->getCurrent_mA() * 0.001f;
            this->usbVoltage[i] = this->usb_[i]->getVoltage_mV() * 0.001f;
            this->usbOvercurrent[i] = this->usb_[i]->getOverCurrent() ? 1 : 0;
        }

        // Dew ports
        for(int i = 0; i < PINSBOX_NUM_DEW_PORTS; ++i)
        {
            this->dew_[i]->update(this->dewPoint, this->dewThreshold[i]);
            this->dewCurrent[i] = this->dew_[i]->getCurrent_mA() * 0.001f;
            this->dewOvercurrent[i] = this->dew_[i]->getOverCurrent() ? 1 : 0;
            this->dewProbe[i] = std::round(this->dew_[i]->getTemperature() * 100.0f) / 100.0f;
            this->dewPWM[i] = this->dew_[i]->getDutyCycle();
            this->dewState[i] = this->dew_[i]->getState();
        }

        // Adjustable ports
        this->buck_->setSupplyVoltage(this->supply12V);
        this->buck_->measureCurrent();
        this->buckVmin = this->buck_->getMinVoltage();
        this->buckVmax = this->buck_->getMaxVoltage();
        this->buckVset = this->buck_->getTargetVoltage();
        this->buckVoltage = this->buck_->getVoltage();
        this->buckCurrent = this->buck_->getCurrent_mA() * 0.001f;
        this->buckOvercurrent = this->buck_->getOverCurrent() ? 1 : 0;
        this->buckOvervoltage = this->buck_->getOverVoltage() ? 1 : 0;

        this->pwm_->measureCurrent();
        this->pwmCurrent = this->pwm_->getCurrent_mA() * 0.001f;
        this->pwmOvercurrent = this->pwm_->getOverCurrent() ? 1 : 0;
    }

    int PinsBoxDevice::GetPowerState(int i)
    {
        // Port 0 is always on
        if (i == 0) {
            return 1;
        }

        if (i < 1 || i >= PINSBOX_NUM_POWER_PORTS) {
            return 0;
        }

        uint8_t state = this->pwr_[i]->getState();
        this->powerState[i] = state;
        return state;
    }

    bool PinsBoxDevice::SetPowerState(int i, int state)
    {
        // Port 0 is always on
        if (i == 0) {
            return false;
        }

        if (i < 1 || i >= PINSBOX_NUM_POWER_PORTS) {
            return false;
        }

        this->pwr_[i]->setState(state);
        this->powerState[i] = state;
        return true;
    }

    bool PinsBoxDevice::SetPowerBootState(int i, int state)
    {
        this->powerBootstrap[i] = state;
        SaveSettings();
        return true;
    }

    int PinsBoxDevice::GetUSBState(int i)
    {
        if (i < 0 || i >= PINSBOX_NUM_USB_PORTS) {
            return 0;
        }

        uint8_t state = this->usb_[i]->getState();
        this->usbState[i] = state;
        return state;
    }

    bool PinsBoxDevice::SetUSBState(int i, int state)
    {
        if (i < 0 || i >= PINSBOX_NUM_USB_PORTS) {
            return false;
        }

        if(state)
        {
            this->usb_[i]->enable();
        }
        else
        {
            this->usb_[i]->disable();
        }

        this->usbState[i] = state;
        return true;
    }

    bool PinsBoxDevice::SetUSBBootState(int i, int state)
    {
        this->usbBootstrap[i] = state;
        SaveSettings();
        return true;
    }

    int PinsBoxDevice::GetDewState(int i)
    {
        if (i < 0 || i >= PINSBOX_NUM_DEW_PORTS) {
            return 0;
        }

        uint8_t state = this->dew_[i]->getState();
        this->dewState[i] = state;
        this->dewPWM[i] = this->dew_[i]->getDutyCycle();
        return state;
    }

    bool PinsBoxDevice::SetDewState(int i, int state, int power)
    {
        if (i < 0 || i >= PINSBOX_NUM_DEW_PORTS) {
            return false;
        }

        // On auto mode, do nothing
        if (dewAuto[i]) {
            return false;
        }

        this->dew_[i]->setState(state, power);
        this->dewState[i] = state;
        this->dewPWM[i] = power;
        return true;
    }

    bool PinsBoxDevice::SetDewAutoMode(int i, int state)
    {
        this->dewAuto[i] = state;
        this->dew_[i]->setAutoMode(state);
        SaveSettings();
        return true;
    }

    bool PinsBoxDevice::SetDewAutoThreshold(int i, float val)
    {
        this->dewThreshold[i] = val;
        SaveSettings();
        return true;
    }

    int PinsBoxDevice::GetBuckState(void)
    {
        uint8_t state = this->buck_->getState();
        this->buckState = state;
        return state;
    }

    bool PinsBoxDevice::SetBuckState(int state, int target)
    {
        this->buck_->setState(state, target);
        this->buckState = state;
        return true;
    }

    bool PinsBoxDevice::SetBuckBootState(int state)
    {
        this->buckBootstrap = state;
        SaveSettings();
        return true;
    }

    int PinsBoxDevice::GetPWMState(void)
    {
        uint8_t state = this->pwm_->getState();
        this->pwmState = state;
        this->pwmPWM = this->pwm_->getDutyCycle();
        return state;
    }

    bool PinsBoxDevice::SetPWMState(int state, int power)
    {
        this->pwm_->setState(state, power);
        this->pwmState = state;
        this->pwmPWM = power;
        return true;
    }












    static bool IsCM5(void)
    {
        std::ifstream file("/proc/device-tree/model");
        if(!file.is_open())
        {
            return false;
        }

        std::string model;
        std::getline(file, model);

        return model.find("Compute Module 5") != std::string::npos;
    }

    bool ScanPinsBox(int *ids)
    {
        // Check if this is a Raspberry PI CM5
        if(!ids || !IsCM5() || !std::filesystem::exists("/etc/pinsDevice"))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_globalMutex);

        PB_DEBUG("Valid device found!");

        // Check if device already exists, reuse it instead of creating a new one
        if(g_devices.find(0) == g_devices.end())
        {
            auto device = std::make_shared<PinsBoxDevice>();
            g_devices[0] = device;

            // Start status listener thread
            g_devices[0]->StartStatusListener();
        }
        else
        {
            PB_DEBUG("Device at id=0 already exists, reusing it");
        }
        
        ids[0] = 0;

        return true;
    }
} /* namespace PowerBox */