#pragma once
#include "iostream"
enum DeviceStatus{
    DEVICE_ERROR = 1,
    DEVICE_CHECK ,
    DEVICE_READY ,
    DEVICE_RUNING ,
    DEVICE_FINISH ,
};

enum DeviceErrorCode {
    ERRORCODE_NETWORK = 0x1000,
};

enum SelfCheckCode{
    SELFCHECK_FAIL = 0,
    SELFCHECK_SUCCESS = 1,
};

class DeviceBase{
public:
    virtual bool Stop() = 0;
    virtual bool Start() = 0;
    virtual bool Pause() = 0;
    virtual int SelfCheck() = 0;
    virtual float GetPower() = 0;
    virtual std::string GetDeviceId() = 0;
};
