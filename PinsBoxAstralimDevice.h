#ifndef PINS_BOX_ASTRALIM_DEVICE_H
#define PINS_BOX_ASTRALIM_DEVICE_H

#include "Device.h"
#include "GPIOManager.h"
#include "PWM.h"

#include <array>
#include <chrono>
#include <limits>
#include <memory>
#include <string>
#include <thread>

namespace PowerBox
{

class PinsBoxAstralimDevice : public Device
{
public:
    PinsBoxAstralimDevice();
    virtual ~PinsBoxAstralimDevice();

    virtual PB_ERROR_TYPE Open() override;
    virtual void Close() override;

    virtual std::string GetSerial() override;
    virtual int GetUpTime() override;
    virtual float GetCoreTemp() override;
    virtual int GetExtSensor() override;
    virtual float GetTemperature() override;
    virtual float GetHumidity() override;
    virtual float GetDewPoint() override;
    virtual float GetTemperatureOffset() override;
    virtual bool SetTemperatureOffset(float offset) override;
    virtual float GetHumidityOffset() override;
    virtual bool SetHumidityOffset(float offset) override;

    virtual float GetSupply12V() override;
    virtual float GetSupply5V() override;
    virtual float GetSupply12A() override;
    virtual float GetSupply12Ah() override;
    virtual float GetSupply12Wh() override;
    virtual float GetSupply12AverageA() override;

    virtual bool SetExtTemperature(float) override { return true; }
    virtual bool SetExtHumidity(float) override { return true; }
    virtual int GetEnvUpdateRate() override { return envUpdateRate; }
    virtual bool SetEnvUpdateRate(int val) override { envUpdateRate = val; return true; }
    virtual int GetUpdateRate() override { return updateRate; }
    virtual bool SetUpdateRate(int val) override { updateRate = val; return true; }

    virtual int GetNumPowerPorts(void) const override { return PowerPortCount; }
    virtual int GetNumUSBPorts(void) const override { return 0; }
    virtual int GetNumDewPorts(void) const override { return DewPortCount; }
    virtual int GetNumBuckPorts(void) const override { return 0; }
    virtual int GetNumPWMPorts(void) const override { return 0; }

    virtual int GetPowerState(int port) override;
    virtual bool SetPowerState(int port, int state) override;
    virtual int GetPowerBootState(int port) override;
    virtual bool SetPowerBootState(int port, int state) override;
    virtual bool ResetPowerOvercurrent(int port) override;

    virtual int GetUSBState(int port) override;
    virtual bool SetUSBState(int port, int state) override;
    virtual int GetUSBBootState(int port) override;
    virtual bool SetUSBBootState(int port, int state) override;
    virtual bool ResetUSBOvercurrent(int port) override;

    virtual int GetDewState(int port) override;
    virtual bool SetDewState(int port, int state, int power) override;
    virtual int GetDewAutoMode(int port) override;
    virtual bool SetDewAutoMode(int port, int state) override;
    virtual float GetDewAutoThreshold(int port) override;
    virtual bool SetDewAutoThreshold(int port, float val) override;
    virtual bool ResetDewOvercurrent(int port) override;
    virtual int GetDewPWMPower(int port) override;

    virtual int GetBuckState() override;
    virtual bool SetBuckState(int state, float target) override;
    virtual int GetBuckBootState() override;
    virtual bool SetBuckBootState(int state) override;
    virtual float GetBuckSetVoltage() override;
    virtual bool ResetBuckOvercurrent(void) override;

    virtual int GetPWMState(void) override;
    virtual bool SetPWMState(int state, int power) override;
    virtual bool GetPWMPower(void) override;
    virtual bool ResetPWMOvercurrent(void) override;

    virtual int GetFirmwareVersion() override;
    virtual std::string GetModelType() override;
    virtual std::string GetUUID() override;
    virtual PB_ERROR_TYPE FactoryReset() override;

    virtual void StartStatusListener() override;
    virtual void StopStatusListener() override;
    virtual void StatusListenerThreadFunc() override;

private:
    class BME280Reader;
    class INA219Reader;

    bool InitSensors();
    void UpdateSensors();
    bool ConfigureDewPort(int port);
    bool ApplyDewOutput(int port);
    std::string GetFullSerial();
    bool ValidPowerPort(int port) const;
    bool ValidDewPort(int port) const;

    static constexpr int PowerPortCount = 3;
    static constexpr int DewPortCount = 2;
    static constexpr unsigned int DewPeriodNs = 1000000;

    GPIOManager *gpio;
    std::array<PWM *, DewPortCount> pwm;
    std::unique_ptr<BME280Reader> bme280;
    std::array<std::unique_ptr<INA219Reader>, 6> ina219;
    std::thread statusListenerThread;
    std::chrono::steady_clock::time_point sessionStart;
    std::chrono::steady_clock::time_point lastEnergyUpdate;
};

#ifdef HAVE_LIBGPIOD
bool ScanPinsBoxAstralim(int *ids);
#else
inline bool ScanPinsBoxAstralim(int *ids) { (void)ids; return false; }
#endif

}

#endif
