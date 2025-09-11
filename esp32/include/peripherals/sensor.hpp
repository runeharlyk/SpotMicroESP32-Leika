#pragma once

#include <ArduinoJson.h>

struct SensorMessageBase {
    bool success;
    virtual void toJson(JsonVariant v) const = 0;
    virtual void fromJson(JsonVariantConst v) = 0;

    virtual ~SensorMessageBase() = default;
};

template <class T>
class SensorBase {
    static_assert(std::is_base_of<SensorMessageBase, T>::value, "T must inherit from SensorMessageBase");

  public:
    SensorBase() {}

    virtual bool initialize() = 0;

    virtual bool update() = 0;

    virtual void getResults(JsonVariant &root) { _msg.toJson(root); }

    virtual T getResult() { return _msg; }

    virtual bool isActive() { return _msg.success; }

  protected:
    T _msg;
};