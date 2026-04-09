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

#include "PinsBoxLightDevice.h"
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

#define PINSBOX_NUM_POWER_PORTS 4
#define PINSBOX_NUM_USB_PORTS 4
#define PINSBOX_NUM_DEW_PORTS 0
#define PINSBOX_NUM_BUCK_PORTS 0
#define PINSBOX_NUM_PWM_PORTS 0

namespace PowerBox
{
    static constexpr unsigned int NUM_PINS = 8;
    static constexpr uint8_t PINS[NUM_PINS] = {
        13, // PWR0
        12, // PWR1
        18, // PWR2
        26, // PWR3
        16, // DEN12V_12
        24, // DEN12V_34
        23, // DEN12V_IN
        25, // DSEL
    };

    static const uint8_t *PWR_PINS = &PINS[0];
    static const uint8_t DSEL_PIN = PINS[7];
    static const uint8_t INDEN_PIN = PINS[6];
    static const uint8_t *PWRDEN_PINS = &PINS[4];

    PinsBoxLightDevice::PinsBoxLightDevice(void)
    {
        this->statusListenerRunning = false;

        // Initial reset
        this->ResetProperties();
        
        // Load settings from file at construction time
        this->LoadSettings();

        this->gpio_ = new GPIOManager("/dev/gpiochip0",
                                      nullptr,
                                      -1,
                                      0,
                                      nullptr,
                                      NUM_PINS,
                                      PINS);

        this->gpio_->begin();

        // ADC
        this->adc_ = new MCP3202(*this->gpio_);
        this->adc_->begin(3.3f);

        // Supply
        this->supply_ = new BTS7006<MCP3202>(*this->gpio_, *this->adc_);
        this->supply_->begin(1200, INDEN_PIN, 255, DSEL_PIN, 0);
        this->supply_->setMaxCurrent(12.f);
        this->supply_->setChannel(0);
        this->supply_->setSampling(100, 50);

        // PWR ports
        this->pwr12_ = new BTS7012<MCP3202>*[2];
        this->pwr34_ = new BTS7080<MCP3202> *[2];

        this->pwr12_[0] = new BTS7012<MCP3202>(*this->gpio_, *this->adc_);
        this->pwr12_[1] = new BTS7012<MCP3202>(*this->gpio_, *this->adc_);
        this->pwr34_[0] = new BTS7080<MCP3202>(*this->gpio_, *this->adc_);
        this->pwr34_[1] = new BTS7080<MCP3202>(*this->gpio_, *this->adc_);

        // Initialize PWR ports
        this->pwr12_[0]->begin(1200, PWRDEN_PINS[0], PWR_PINS[0], DSEL_PIN, 0, this->powerBootstrap[0]);
        this->pwr12_[1]->begin(1200, PWRDEN_PINS[0], PWR_PINS[1], DSEL_PIN, 1, this->powerBootstrap[1]);
        this->pwr34_[0]->begin(1200, PWRDEN_PINS[1], PWR_PINS[2], DSEL_PIN, 0, this->powerBootstrap[2]);
        this->pwr34_[1]->begin(1200, PWRDEN_PINS[1], PWR_PINS[3], DSEL_PIN, 1, this->powerBootstrap[3]);

        // PWR sense sample rate and max current
        this->pwr12_[0]->setMaxCurrent(6.f);
        this->pwr12_[0]->setChannel(0);
        this->pwr12_[0]->setSampling(10, 1);
        this->pwr12_[1]->setMaxCurrent(6.f);
        this->pwr12_[1]->setChannel(0);
        this->pwr12_[1]->setSampling(10, 1);
        this->pwr34_[0]->setMaxCurrent(3.f);
        this->pwr34_[0]->setChannel(0);
        this->pwr34_[0]->setSampling(10, 1);
        this->pwr34_[1]->setMaxCurrent(3.f);
        this->pwr34_[1]->setChannel(0);
        this->pwr34_[1]->setSampling(10, 1);

        // All USB ports are readonly
        for (int i = 0; i < this->GetNumUSBPorts(); ++i)
        {
            this->usbReadOnly[i] = true;
        }
    }

    PinsBoxLightDevice::~PinsBoxLightDevice(void)
    {
        // Stop listener if running
        this->StopStatusListener();

        // Clean up supply monitoring
        if(this->supply_) {
            delete this->supply_;
            this->supply_ = nullptr;
        }

        // Clean up power ports
        if(this->pwr12_) {
            for(int i = 0; i < 2; ++i) {
                if(this->pwr12_[i]) {
                    delete this->pwr12_[i];
                    this->pwr12_[i] = nullptr;
                }
            }
            delete[] this->pwr12_;
            this->pwr12_ = nullptr;
        }

        if(this->pwr34_) {
            for(int i = 0; i < 2; ++i) {
                if(this->pwr34_[i]) {
                    delete this->pwr34_[i];
                    this->pwr34_[i] = nullptr;
                }
            }
            delete[] this->pwr34_;
            this->pwr34_ = nullptr;
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

    void PinsBoxLightDevice::ResetProperties(void)
    {
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i)
        {
            this->powerBootstrap[i] = 0;
        }

        for(int i = 0; i < PINSBOX_NUM_USB_PORTS; ++i)
        {
            this->usbCurrent[i] = std::numeric_limits<float>::quiet_NaN();
            this->usbVoltage[i] = std::numeric_limits<float>::quiet_NaN();
        }
    }

    void PinsBoxLightDevice::StartStatusListener(void)
    {
        /* Stop any existing listener by setting the flag */
        this->statusListenerRunning = false;

        /* Wait for old thread to exit if it's still running */
        if(this->statusListenerThread_.joinable()) {
            this->statusListenerThread_.join();
        }

        /* Start new listener thread */
        this->statusListenerRunning = true;
        this->statusListenerThread_ = std::thread(&PinsBoxLightDevice::StatusListenerThreadFunc, this);
        PB_DEBUG("StartStatusListener: Listener thread started");
    }

    void PinsBoxLightDevice::StopStatusListener(void)
    {
        /* Signal listener thread to stop */
        this->statusListenerRunning = false;
        PB_DEBUG("StopStatusListener: Listener stop requested");
        
        /* Wait for the listener thread to actually exit */
        if(this->statusListenerThread_.joinable()) {
            this->statusListenerThread_.join();
            PB_DEBUG("StopStatusListener: Listener thread joined");
        }
    }

    /* Background listener thread function for status messages */
    void PinsBoxLightDevice::StatusListenerThreadFunc()
    {
        PB_DEBUG("StatusListener: starting");

        while(this->statusListenerRunning)
        {
            // Fetch MCP3202 data
            this->GetMCP3202Data();

            // 1 sec delay before we query again
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        PB_DEBUG("StatusListener: exiting");
    }

    PB_ERROR_TYPE PinsBoxLightDevice::Open(void)
    {
        PB_DEBUG("PBOpen: Found PI'N'Stars power box light device");

        // Fetch power ports config
        for(int i = 0; i < PINSBOX_NUM_POWER_PORTS; ++i) {
            this->GetPowerState(i);
        }

        this->isOpen = true;
        PB_INFO("[OK] Device opened");
        return PB_SUCCESS;
    }

    void PinsBoxLightDevice::Close(void)
    {
        this->isOpen = false;
        PB_DEBUG("[OK] Device closed");
    }

    int PinsBoxLightDevice::GetNumPowerPorts(void) const
    {
        return PINSBOX_NUM_POWER_PORTS;
    }

    int PinsBoxLightDevice::GetNumUSBPorts(void) const
    {
        return PINSBOX_NUM_USB_PORTS;
    }

    int PinsBoxLightDevice::GetNumDewPorts(void) const
    {
        return PINSBOX_NUM_DEW_PORTS;
    }

    int PinsBoxLightDevice::GetNumBuckPorts(void) const
    {
        return PINSBOX_NUM_BUCK_PORTS;
    }

    int PinsBoxLightDevice::GetNumPWMPorts(void) const
    {
        return PINSBOX_NUM_PWM_PORTS;
    }

    std::string PinsBoxLightDevice::GetFullSerial(void)
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

    std::string PinsBoxLightDevice::GetSerial(void)
    {
        std::string serial = this->GetFullSerial();
        return serial.substr(serial.length() - 7);
    }

    int PinsBoxLightDevice::GetUpTime(void)
    {
        struct sysinfo info;
        if(sysinfo(&info) == 0)
        {
            return info.uptime;
        }

        return 0;
    }

    float PinsBoxLightDevice::GetCoreTemp(void)
    {
        float temp = -127.f;
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

    float PinsBoxLightDevice::GetSupply12AverageA(void)
    {
        int uptimeSeconds = this->GetUpTime();
        if (uptimeSeconds <= 0)
        {
            return 0.0f;
        }
        
        float uptimeHours = uptimeSeconds / 3600.0f;
        return this->supply12Ah / uptimeHours;
    }

    void PinsBoxLightDevice::LoadSettings(void)
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
        
        // Load power port bootstrap states
        if (root.isMember("powerPorts")) {
            Json::Value powerPorts = root["powerPorts"];
            this->powerBootstrap[0] = true;
            for (int i = 1; i < PINSBOX_NUM_POWER_PORTS && i < (int)powerPorts.size(); i++) {
                int idx = powerPorts[i].get("port", 0).asInt();
                this->powerBootstrap[idx] = powerPorts[i].get("bootState", 0).asInt();
            }
        }
    }

    void PinsBoxLightDevice::SaveSettings(void)
    {
        Json::Value root;
        
        // Power port bootstrap states
        root["powerPorts"][0]["port"] = 0;
        root["powerPorts"][0]["bootState"] = 1;
        for (int i = 1; i < PINSBOX_NUM_POWER_PORTS; i++) {
            root["powerPorts"][i]["port"] = i;
            root["powerPorts"][i]["bootState"] = this->powerBootstrap[i];
        }

        std::ofstream file("/home/pi/pins/deviceSettings.json");
        if (file.is_open()) {
            file << root;
            file.close();
            PB_DEBUG("Settings saved");
        }
    }

    void PinsBoxLightDevice::GetMCP3202Data(void)
    {
        // Supply
        this->supply_->measureCurrent();
        this->supply12A = this->supply_->getCurrent_mA() * 0.001f;
        this->supply12V = this->adc_->analogReadAverage(1, 20, 0) * (36e3 + 4.7e3) / 4.7e3 * 0.001f;

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
            if(i < 2)
            {
                this->pwr12_[i]->measureCurrent();
                this->powerCurrent[i] = this->pwr12_[i]->getCurrent_mA() * 0.001f;
                this->powerOvercurrent[i] = this->pwr12_[i]->getOverCurrent() ? 1 : 0;
            }
            else
            {
                this->pwr34_[i - 2]->measureCurrent();
                this->powerCurrent[i] = this->pwr34_[i - 2]->getCurrent_mA() * 0.001f;
                this->powerOvercurrent[i] = this->pwr34_[i - 2]->getOverCurrent() ? 1 : 0;
            }
        }
    }

    int PinsBoxLightDevice::GetPowerState(int i)
    {
        // Port 0 is always on
        if (i == 0) {
            return 1;
        }

        if (i < 1 || i >= PINSBOX_NUM_POWER_PORTS) {
            return 0;
        }

        uint8_t state = (i < 2) ? this->pwr12_[i]->getState() : this->pwr34_[i - 2]->getState();
        this->powerState[i] = state;
        return state;
    }

    bool PinsBoxLightDevice::SetPowerState(int i, int state)
    {
        // Port 0 is always on
        if (i == 0) {
            return false;
        }

        if (i < 1 || i >= PINSBOX_NUM_POWER_PORTS) {
            return false;
        }

        if(i < 2)
        {
            this->pwr12_[i]->setState(state);
        }
        else{
            this->pwr34_[i - 2]->setState(state);
        }
        this->powerState[i] = state;
        return true;
    }

    bool PinsBoxLightDevice::SetPowerBootState(int i, int state)
    {
        this->powerBootstrap[i] = state;
        SaveSettings();
        return true;
    }

    static bool IsRPI(void)
    {
        std::ifstream file("/proc/device-tree/model");
        if(!file.is_open())
        {
            return false;
        }

        std::string model;
        std::getline(file, model);

        return model.find("Raspberry Pi") != std::string::npos;
    }

    bool ScanPinsBoxLight(int *ids)
    {
        // Check if this is a Raspberry PI
        if(!ids || !IsRPI() || !std::filesystem::exists("/etc/pinsLightDevice"))
        {
            return false;
        }

        std::lock_guard<std::mutex> lock(g_globalMutex);

        PB_DEBUG("Valid device found!");

        // Check if device already exists, reuse it instead of creating a new one
        if(g_devices.find(0) == g_devices.end())
        {
            auto device = std::make_shared<PinsBoxLightDevice>();
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
