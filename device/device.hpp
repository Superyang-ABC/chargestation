#pragma once
#include "devicebase.hpp"
#include "iostream"
class Device : public DeviceBase
{
private:
    /* data */
public:
    Device(/* args */);
    virtual bool Stop() ;
    virtual bool Start() ;
    virtual bool Pause() ;
    virtual int SelfCheck() ;
    virtual float GetPower() ;
    virtual std::string GetDeviceId() ;
private:

};

