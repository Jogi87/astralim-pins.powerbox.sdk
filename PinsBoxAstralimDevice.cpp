#include "PinsBoxAstralimDevice.h"
#include "PowerBoxLogging.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <linux/i2c-dev.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>

namespace PowerBox
{
namespace
{
constexpr const char *I2CBus = "/dev/i2c-1";
constexpr const char *PWMChip = "pwmchip0";
constexpr const char *PWMChipPath = "/sys/class/pwm/pwmchip0";
constexpr int AstralimPowerPortCount = 3;
constexpr int AstralimDewPortCount = 2;
constexpr std::array<uint8_t, AstralimPowerPortCount> PowerGPIOs = {26, 20, 21};
constexpr std::array<int, AstralimDewPortCount> PWMChannels = {2, 1};
constexpr std::array<uint8_t, AstralimDewPortCount> DewGPIOs = {18, 13};
constexpr std::array<uint8_t, 6> INAAddresses = {0x40, 0x41, 0x44, 0x46, 0x4d, 0x49};
constexpr std::array<float, 6> INAShuntOhms = {0.005f, 0.01f, 0.01f, 0.01f, 0.01f, 0.01f};

bool IsRPI()
{
    std::ifstream file("/proc/device-tree/model");
    if (!file.is_open())
        return false;

    std::string model;
    std::getline(file, model);
    return model.find("Raspberry Pi") != std::string::npos;
}

uint16_t Swap16(uint16_t value)
{
    return static_cast<uint16_t>((value >> 8) | (value << 8));
}

bool ReadI2CReg8(const char *bus, uint8_t address, uint8_t reg, uint8_t &value)
{
    int fd = ::open(bus, O_RDWR);
    if (fd < 0)
        return false;

    bool ok = false;
    if (::ioctl(fd, I2C_SLAVE, address) >= 0 && ::write(fd, &reg, 1) == 1)
        ok = (::read(fd, &value, 1) == 1);

    ::close(fd);
    return ok;
}

bool ReadI2CReg16(const char *bus, uint8_t address, uint8_t reg, uint16_t &value)
{
    uint8_t data[2] = {0, 0};
    int fd = ::open(bus, O_RDWR);
    if (fd < 0)
        return false;

    bool ok = false;
    if (::ioctl(fd, I2C_SLAVE, address) >= 0 && ::write(fd, &reg, 1) == 1)
    {
        if (::read(fd, data, 2) == 2)
        {
            value = static_cast<uint16_t>((data[0] << 8) | data[1]);
            ok = true;
        }
    }

    ::close(fd);
    return ok;
}

std::string PWMChannelPath(int channel)
{
    std::ostringstream path;
    path << PWMChipPath << "/pwm" << channel;
    return path.str();
}

} // namespace

class PinsBoxAstralimDevice::INA219Reader
{
public:
    INA219Reader(uint8_t address, float shuntOhms) : address(address), shuntOhms(shuntOhms) {}
    ~INA219Reader() { Close(); }

    bool Begin()
    {
        Close();
        fd = ::open(I2CBus, O_RDWR);
        if (fd < 0)
        {
            PB_ERROR("Astralim INA219 0x%02x: failed to open %s: %s", address, I2CBus, std::strerror(errno));
            return false;
        }
        if (::ioctl(fd, I2C_SLAVE, address) < 0)
        {
            PB_ERROR("Astralim INA219 0x%02x: failed to select I2C address: %s", address, std::strerror(errno));
            Close();
            return false;
        }
        return WriteRegister(0x00, 0x399f);
    }

    bool Read(float &busVoltageV, float &currentA)
    {
        uint16_t busRaw = 0;
        uint16_t shuntRaw = 0;

        if (!ReadRegister(0x02, busRaw) || !ReadRegister(0x01, shuntRaw))
            return false;

        const int16_t signedShuntRaw = static_cast<int16_t>(shuntRaw);
        busVoltageV = static_cast<float>((busRaw >> 3) * 4) / 1000.0f;
        currentA = (static_cast<float>(signedShuntRaw) * 0.00001f) / shuntOhms;
        if (currentA < 0.0f)
            currentA = 0.0f;
        return true;
    }

private:
    bool WriteRegister(uint8_t reg, uint16_t value)
    {
        const uint16_t beValue = Swap16(value);
        uint8_t data[3] = {reg, static_cast<uint8_t>(beValue & 0xff), static_cast<uint8_t>(beValue >> 8)};
        if (::write(fd, data, 3) != 3)
        {
            PB_ERROR("Astralim INA219 0x%02x: failed to write register 0x%02x: %s", address, reg, std::strerror(errno));
            return false;
        }
        return true;
    }

    bool ReadRegister(uint8_t reg, uint16_t &value)
    {
        uint8_t data[2] = {0, 0};
        if (::write(fd, &reg, 1) != 1)
        {
            PB_ERROR("Astralim INA219 0x%02x: failed to select register 0x%02x: %s", address, reg, std::strerror(errno));
            return false;
        }
        if (::read(fd, data, 2) != 2)
        {
            PB_ERROR("Astralim INA219 0x%02x: failed to read register 0x%02x: %s", address, reg, std::strerror(errno));
            return false;
        }
        value = static_cast<uint16_t>((data[0] << 8) | data[1]);
        return true;
    }

    void Close()
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }

    uint8_t address = 0;
    float shuntOhms = 0.01f;
    int fd = -1;
};

class PinsBoxAstralimDevice::BME280Reader
{
public:
    ~BME280Reader() { Close(); }

    bool Begin()
    {
        Close();
        fd = ::open(I2CBus, O_RDWR);
        if (fd < 0)
        {
            PB_ERROR("Astralim BME280: failed to open %s: %s", I2CBus, std::strerror(errno));
            return false;
        }
        if (::ioctl(fd, I2C_SLAVE, 0x76) < 0)
        {
            PB_ERROR("Astralim BME280: failed to select I2C address 0x76: %s", std::strerror(errno));
            Close();
            return false;
        }

        uint8_t chipID = 0;
        if (!ReadReg8(0xd0, chipID) || chipID != 0x60)
        {
            PB_ERROR("Astralim BME280: chip id probe failed");
            Close();
            return false;
        }

        if (!ReadCalibration())
        {
            Close();
            return false;
        }

        return WriteReg8(0xf2, 0x01) && WriteReg8(0xf5, 0xa0) && WriteReg8(0xf4, 0x27);
    }

    bool Read(float &temperatureC, float &humidityPct, float &dewPointC)
    {
        uint8_t data[8] = {0};
        if (!ReadBlock(0xf7, data, sizeof(data)))
            return false;

        const int32_t adcT = static_cast<int32_t>((static_cast<uint32_t>(data[3]) << 12) |
                                                  (static_cast<uint32_t>(data[4]) << 4) |
                                                  (static_cast<uint32_t>(data[5]) >> 4));
        const int32_t adcH = static_cast<int32_t>((static_cast<uint32_t>(data[6]) << 8) | data[7]);

        int32_t var1 = ((((adcT >> 3) - (static_cast<int32_t>(dig_T1) << 1))) * static_cast<int32_t>(dig_T2)) >> 11;
        int32_t var2 = (((((adcT >> 4) - static_cast<int32_t>(dig_T1)) *
                          ((adcT >> 4) - static_cast<int32_t>(dig_T1))) >> 12) *
                        static_cast<int32_t>(dig_T3)) >> 14;
        tFine = var1 + var2;
        temperatureC = static_cast<float>((tFine * 5 + 128) >> 8) / 100.0f;

        int32_t v = tFine - 76800;
        v = (((((adcH << 14) - (static_cast<int32_t>(dig_H4) << 20) -
                (static_cast<int32_t>(dig_H5) * v)) + 16384) >> 15) *
             (((((((v * static_cast<int32_t>(dig_H6)) >> 10) *
                  (((v * static_cast<int32_t>(dig_H3)) >> 11) + 32768)) >> 10) + 2097152) *
                static_cast<int32_t>(dig_H2) + 8192) >> 14));
        v = v - (((((v >> 15) * (v >> 15)) >> 7) * static_cast<int32_t>(dig_H1)) >> 4);
        v = std::clamp(v, 0, 419430400);
        humidityPct = static_cast<float>(v >> 12) / 1024.0f;

        const double a = 17.62;
        const double b = 243.12;
        const double gamma = ((a * temperatureC) / (b + temperatureC)) + std::log(std::max(0.01f, humidityPct) / 100.0);
        dewPointC = static_cast<float>((b * gamma) / (a - gamma));
        return true;
    }

private:
    bool WriteReg8(uint8_t reg, uint8_t value)
    {
        uint8_t data[2] = {reg, value};
        if (::write(fd, data, 2) != 2)
        {
            PB_ERROR("Astralim BME280: failed to write register 0x%02x: %s", reg, std::strerror(errno));
            return false;
        }
        return true;
    }

    bool ReadReg8(uint8_t reg, uint8_t &value)
    {
        if (::write(fd, &reg, 1) != 1)
            return false;
        return ::read(fd, &value, 1) == 1;
    }

    bool ReadBlock(uint8_t reg, uint8_t *data, size_t len)
    {
        if (::write(fd, &reg, 1) != 1)
            return false;
        return ::read(fd, data, len) == static_cast<ssize_t>(len);
    }

    bool ReadU16LE(uint8_t reg, uint16_t &value)
    {
        uint8_t data[2] = {0, 0};
        if (!ReadBlock(reg, data, 2))
            return false;
        value = static_cast<uint16_t>(data[0] | (data[1] << 8));
        return true;
    }

    bool ReadS16LE(uint8_t reg, int16_t &value)
    {
        uint16_t raw = 0;
        if (!ReadU16LE(reg, raw))
            return false;
        value = static_cast<int16_t>(raw);
        return true;
    }

    bool ReadCalibration()
    {
        uint8_t e1 = 0, e2 = 0, e3 = 0, e4 = 0, e5 = 0, e6 = 0, e7 = 0;
        return ReadU16LE(0x88, dig_T1) &&
               ReadS16LE(0x8a, dig_T2) &&
               ReadS16LE(0x8c, dig_T3) &&
               ReadReg8(0xa1, dig_H1) &&
               ReadS16LE(0xe1, dig_H2) &&
               ReadReg8(0xe3, e3) &&
               ReadReg8(0xe4, e4) &&
               ReadReg8(0xe5, e5) &&
               ReadReg8(0xe6, e6) &&
               ReadReg8(0xe7, e7) &&
               ((dig_H3 = e3), true) &&
               ((dig_H4 = static_cast<int16_t>((static_cast<int16_t>(e4) << 4) | (e5 & 0x0f))), true) &&
               ((dig_H5 = static_cast<int16_t>((static_cast<int16_t>(e6) << 4) | (e5 >> 4))), true) &&
               ((dig_H6 = static_cast<int8_t>(e7)), true);
    }

    void Close()
    {
        if (fd >= 0)
        {
            ::close(fd);
            fd = -1;
        }
    }

    int fd = -1;
    uint16_t dig_T1 = 0;
    int16_t dig_T2 = 0;
    int16_t dig_T3 = 0;
    uint8_t dig_H1 = 0;
    int16_t dig_H2 = 0;
    uint8_t dig_H3 = 0;
    int16_t dig_H4 = 0;
    int16_t dig_H5 = 0;
    int8_t dig_H6 = 0;
    int32_t tFine = 0;
};

PinsBoxAstralimDevice::PinsBoxAstralimDevice() :
    gpio(new GPIOManager("/dev/gpiochip0", nullptr, 0, 0, nullptr, PowerPortCount, PowerGPIOs.data()))
{
    modelType = "Astralim Power Hat";
    serial = GetFullSerial();
    dewPwmResolution = 8;

    for (int i = 0; i < PowerPortCount; ++i)
    {
        powerState[i] = 0;
        powerBootstrap[i] = 0;
        powerReadOnly[i] = 0;
    }

    for (int i = 0; i < DewPortCount; ++i)
    {
        pwm[i] = new PWM(PWMChip, PWMChannels[i]);
        dewState[i] = 0;
        dewPWM[i] = 0;
        dewProbe[i] = -127.0f;
    }
}

PinsBoxAstralimDevice::~PinsBoxAstralimDevice()
{
    Close();
    StopStatusListener();
    delete gpio;
    for (PWM *port : pwm)
        delete port;
}

PB_ERROR_TYPE PinsBoxAstralimDevice::Open()
{
    if (!gpio->begin())
    {
        PB_ERROR("Astralim: failed to initialize GPIO power ports");
        return PB_ERROR_COMMUNICATION;
    }
    for (int i = 0; i < PowerPortCount; ++i)
    {
        if (!SetPowerState(i, 0))
            return PB_ERROR_COMMUNICATION;
    }

    for (int i = 0; i < DewPortCount; ++i)
    {
        if (!ConfigureDewPort(i))
            return PB_ERROR_COMMUNICATION;
    }

    if (!InitSensors())
        return PB_ERROR_COMMUNICATION;

    sessionStart = std::chrono::steady_clock::now();
    lastEnergyUpdate = sessionStart;
    isOpen.store(true);

    for (int i = 0; i < PowerPortCount; ++i)
        SetPowerState(i, powerBootstrap[i]);
    UpdateSensors();
    return PB_SUCCESS;
}

void PinsBoxAstralimDevice::Close()
{
    if (!isOpen.load())
        return;

    for (int i = 0; i < DewPortCount; ++i)
    {
        dewState[i] = 0;
        dewPWM[i] = 0;
        ApplyDewOutput(i);
    }
    for (int i = 0; i < PowerPortCount; ++i)
        SetPowerState(i, 0);

    bme280.reset();
    for (auto &sensor : ina219)
        sensor.reset();
    isOpen.store(false);
}

std::string PinsBoxAstralimDevice::GetSerial() { return serial; }

int PinsBoxAstralimDevice::GetUpTime()
{
    if (!isOpen.load())
        return 0;
    return static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - sessionStart).count());
}

float PinsBoxAstralimDevice::GetCoreTemp()
{
    std::ifstream file("/sys/class/thermal/thermal_zone0/temp");
    float milliC = 0.0f;
    if (file >> milliC)
        return milliC / 1000.0f;
    return -127.0f;
}

int PinsBoxAstralimDevice::GetExtSensor() { return extSensor; }
float PinsBoxAstralimDevice::GetTemperature() { return temperature; }
float PinsBoxAstralimDevice::GetHumidity() { return humidity; }
float PinsBoxAstralimDevice::GetDewPoint() { return dewPoint; }
float PinsBoxAstralimDevice::GetTemperatureOffset() { return temperatureOffset; }
bool PinsBoxAstralimDevice::SetTemperatureOffset(float offset) { temperatureOffset = offset; return true; }
float PinsBoxAstralimDevice::GetHumidityOffset() { return humidityOffset; }
bool PinsBoxAstralimDevice::SetHumidityOffset(float offset) { humidityOffset = offset; return true; }

float PinsBoxAstralimDevice::GetSupply12V() { return supply12V; }
float PinsBoxAstralimDevice::GetSupply5V() { return std::numeric_limits<float>::quiet_NaN(); }
float PinsBoxAstralimDevice::GetSupply12A() { return supply12A; }
float PinsBoxAstralimDevice::GetSupply12Ah() { return supply12Ah; }
float PinsBoxAstralimDevice::GetSupply12Wh() { return supply12Wh; }
float PinsBoxAstralimDevice::GetSupply12AverageA() { return supply12A; }

int PinsBoxAstralimDevice::GetPowerState(int port)
{
    return ValidPowerPort(port) ? powerState[port] : 0;
}

bool PinsBoxAstralimDevice::SetPowerState(int port, int state)
{
    if (!ValidPowerPort(port))
        return false;
    const int normalized = state ? 1 : 0;
    if (!gpio->digitalWrite(PowerGPIOs[port], normalized))
    {
        PB_ERROR("Astralim: failed to write DC%d GPIO%u", port + 1, PowerGPIOs[port]);
        return false;
    }
    powerState[port] = normalized;
    return true;
}

int PinsBoxAstralimDevice::GetPowerBootState(int port)
{
    return ValidPowerPort(port) ? powerBootstrap[port] : 0;
}

bool PinsBoxAstralimDevice::SetPowerBootState(int port, int state)
{
    if (!ValidPowerPort(port))
        return false;
    powerBootstrap[port] = state ? 1 : 0;
    return true;
}

bool PinsBoxAstralimDevice::ResetPowerOvercurrent(int port)
{
    if (ValidPowerPort(port))
        powerOvercurrent[port] = 0;
    return false;
}

int PinsBoxAstralimDevice::GetUSBState(int) { return 0; }
bool PinsBoxAstralimDevice::SetUSBState(int, int) { return true; }
int PinsBoxAstralimDevice::GetUSBBootState(int) { return 0; }
bool PinsBoxAstralimDevice::SetUSBBootState(int, int) { return true; }
bool PinsBoxAstralimDevice::ResetUSBOvercurrent(int) { return false; }

int PinsBoxAstralimDevice::GetDewState(int port)
{
    return ValidDewPort(port) ? dewState[port] : 0;
}

bool PinsBoxAstralimDevice::SetDewState(int port, int state, int power)
{
    if (!ValidDewPort(port))
        return false;

    dewState[port] = state ? 1 : 0;
    dewPWM[port] = std::clamp(power, 0, 255);
    return ApplyDewOutput(port);
}

int PinsBoxAstralimDevice::GetDewAutoMode(int port)
{
    return ValidDewPort(port) ? dewAuto[port] : 0;
}

bool PinsBoxAstralimDevice::SetDewAutoMode(int port, int state)
{
    if (!ValidDewPort(port))
        return false;
    dewAuto[port] = state ? 1 : 0;
    return true;
}

float PinsBoxAstralimDevice::GetDewAutoThreshold(int port)
{
    return ValidDewPort(port) ? dewThreshold[port] : std::numeric_limits<float>::quiet_NaN();
}

bool PinsBoxAstralimDevice::SetDewAutoThreshold(int port, float val)
{
    if (!ValidDewPort(port))
        return false;
    dewThreshold[port] = val;
    return true;
}

bool PinsBoxAstralimDevice::ResetDewOvercurrent(int port)
{
    if (ValidDewPort(port))
        dewOvercurrent[port] = 0;
    return false;
}

int PinsBoxAstralimDevice::GetDewPWMPower(int port)
{
    return ValidDewPort(port) ? dewPWM[port] : 0;
}

int PinsBoxAstralimDevice::GetBuckState() { return 0; }
bool PinsBoxAstralimDevice::SetBuckState(int, float) { return true; }
int PinsBoxAstralimDevice::GetBuckBootState() { return 0; }
bool PinsBoxAstralimDevice::SetBuckBootState(int) { return true; }
float PinsBoxAstralimDevice::GetBuckSetVoltage() { return 0.0f; }
bool PinsBoxAstralimDevice::ResetBuckOvercurrent() { return false; }

int PinsBoxAstralimDevice::GetPWMState() { return 0; }
bool PinsBoxAstralimDevice::SetPWMState(int, int) { return true; }
bool PinsBoxAstralimDevice::GetPWMPower() { return false; }
bool PinsBoxAstralimDevice::ResetPWMOvercurrent() { return false; }

int PinsBoxAstralimDevice::GetFirmwareVersion() { return 1; }
std::string PinsBoxAstralimDevice::GetModelType() { return modelType; }
std::string PinsBoxAstralimDevice::GetUUID() { return GetFullSerial(); }
PB_ERROR_TYPE PinsBoxAstralimDevice::FactoryReset() { return PB_SUCCESS; }

void PinsBoxAstralimDevice::StartStatusListener()
{
    StopStatusListener();
    statusListenerRunning.store(true);
    statusListenerThread = std::thread(&PinsBoxAstralimDevice::StatusListenerThreadFunc, this);
}

void PinsBoxAstralimDevice::StopStatusListener()
{
    statusListenerRunning.store(false);
    if (statusListenerThread.joinable())
        statusListenerThread.join();
}

void PinsBoxAstralimDevice::StatusListenerThreadFunc()
{
    while (statusListenerRunning.load())
    {
        if (isOpen.load())
            UpdateSensors();
        std::this_thread::sleep_for(std::chrono::seconds(std::max(1, updateRate)));
    }
}

bool PinsBoxAstralimDevice::InitSensors()
{
    bme280 = std::make_unique<BME280Reader>();
    if (!bme280->Begin())
        return false;

    for (size_t i = 0; i < ina219.size(); ++i)
    {
        ina219[i] = std::make_unique<INA219Reader>(INAAddresses[i], INAShuntOhms[i]);
        if (!ina219[i]->Begin())
            return false;
    }

    extSensor = 1;
    return true;
}

void PinsBoxAstralimDevice::UpdateSensors()
{
    if (bme280)
    {
        float t = temperature;
        float h = humidity;
        float d = dewPoint;
        if (bme280->Read(t, h, d))
        {
            temperature = t + temperatureOffset;
            humidity = h + humidityOffset;
            dewPoint = d;
        }
    }

    std::array<float, 6> voltages = {0.0f};
    std::array<float, 6> currents = {0.0f};
    for (size_t i = 0; i < ina219.size(); ++i)
    {
        if (ina219[i])
            ina219[i]->Read(voltages[i], currents[i]);
    }

    supply12V = voltages[0];
    supply12A = currents[0];
    powerCurrent[0] = currents[1];
    powerCurrent[1] = currents[2];
    powerCurrent[2] = currents[3];
    dewCurrent[0] = currents[4];
    dewCurrent[1] = currents[5];

    const auto now = std::chrono::steady_clock::now();
    const double dtHours = std::chrono::duration<double>(now - lastEnergyUpdate).count() / 3600.0;
    if (dtHours > 0.0 && dtHours < 1.0)
    {
        supply12Ah += static_cast<float>(supply12A * dtHours);
        supply12Wh += static_cast<float>(supply12V * supply12A * dtHours);
    }
    lastEnergyUpdate = now;
}

bool PinsBoxAstralimDevice::ConfigureDewPort(int port)
{
    const int channel = PWMChannels[port];
    if (!std::filesystem::exists(PWMChannelPath(channel)))
    {
        if (!pwm[port]->setExport())
        {
            PB_ERROR("Astralim: failed to export Dew%d PWM channel %d on %s for GPIO%u", port + 1, channel, PWMChipPath, DewGPIOs[port]);
            return false;
        }
    }

    if (!pwm[port]->setState(false))
    {
        PB_ERROR("Astralim: failed to disable Dew%d PWM before configuration", port + 1);
        return false;
    }
    if (!pwm[port]->setPeriod(DewPeriodNs))
    {
        PB_ERROR("Astralim: failed to set Dew%d PWM period", port + 1);
        return false;
    }
    if (!pwm[port]->setDutyCycle(0))
    {
        PB_ERROR("Astralim: failed to set Dew%d PWM duty cycle", port + 1);
        return false;
    }
    return true;
}

bool PinsBoxAstralimDevice::ApplyDewOutput(int port)
{
    const unsigned int duty = dewState[port] ? (DewPeriodNs * static_cast<unsigned int>(dewPWM[port]) / 255u) : 0u;
    if (!pwm[port]->setState(false))
    {
        PB_ERROR("Astralim: failed to disable Dew%d PWM before duty update", port + 1);
        return false;
    }
    if (!pwm[port]->setPeriod(DewPeriodNs))
    {
        PB_ERROR("Astralim: failed to update Dew%d PWM period", port + 1);
        return false;
    }
    if (!pwm[port]->setDutyCycle(duty))
    {
        PB_ERROR("Astralim: failed to update Dew%d PWM duty cycle", port + 1);
        return false;
    }
    if (!pwm[port]->setState(duty > 0))
    {
        PB_ERROR("Astralim: failed to set Dew%d PWM enable state", port + 1);
        return false;
    }
    return true;
}

std::string PinsBoxAstralimDevice::GetFullSerial()
{
    std::ifstream file("/proc/cpuinfo");
    std::string line;
    while (std::getline(file, line))
    {
        if (line.rfind("Serial", 0) == 0)
        {
            const auto pos = line.find(':');
            if (pos != std::string::npos)
            {
                std::string value = line.substr(pos + 1);
                value.erase(std::remove_if(value.begin(), value.end(), [](unsigned char ch) { return std::isspace(ch); }), value.end());
                return "astralim-" + value;
            }
        }
    }
    return "astralim-pinstars";
}

bool PinsBoxAstralimDevice::ValidPowerPort(int port) const
{
    return port >= 0 && port < PowerPortCount;
}

bool PinsBoxAstralimDevice::ValidDewPort(int port) const
{
    return port >= 0 && port < DewPortCount;
}

bool ScanPinsBoxAstralim(int *ids)
{
    if (ids == nullptr || !IsRPI())
        return false;

    if (!std::filesystem::exists(I2CBus) || !std::filesystem::exists(PWMChipPath))
        return false;

    uint8_t bmeID = 0;
    uint16_t inaProbe = 0;
    if (!ReadI2CReg8(I2CBus, 0x76, 0xd0, bmeID) || bmeID != 0x60)
        return false;
    if (!ReadI2CReg16(I2CBus, 0x40, 0x02, inaProbe))
        return false;

    std::lock_guard<std::mutex> lock(g_globalMutex);
    if (g_devices.find(0) == g_devices.end())
    {
        std::shared_ptr<Device> device = std::make_shared<PinsBoxAstralimDevice>();
        g_devices[0] = device;
        device->StartStatusListener();
    }

    ids[0] = 0;
    return true;
}

} // namespace PowerBox
