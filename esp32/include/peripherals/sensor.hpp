#pragma once

template <class T>
class SensorBase {
  public:
    virtual bool initialize() = 0;
    virtual bool update() = 0;
    virtual bool isActive() { return _msg.success; }

  protected:
    T _msg;
};