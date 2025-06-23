#include "device.hpp"

Device::Device()
{
}

bool Device::Stop()
{
    return true;
}

bool Device::Start()
{
    return true;
}

bool Device::Pause()
{
    return true;
}

int Device::SelfCheck()
{
    return 0;
}

float Device::GetPower()
{
    return 0.0f;
}

std::string Device::GetDeviceId()
{
    return "00000000000000001";
}
