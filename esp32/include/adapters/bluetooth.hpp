#pragma once
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#include "comm_base.hpp"
#include "event_bus.hpp"
#include "topic.hpp"

class BluetoothService : public CommBase<> {
    BLEServer* bleServer {nullptr};
    BLECharacteristic* txCharacteristic {nullptr};
    BLECharacteristic* rxCharacteristic {nullptr};
    bool connected {false};

  protected:
    template <typename Msg>
    using EventBusHandle = typename EventBus<Msg>::Handle;

    template <typename Msg>
    EventBusHandle<Msg>& getHandle(Topic topic) {
        return *static_cast<EventBusHandle<Msg>*>(subscriptionHandle[static_cast<size_t>(topic)]);
    }

    template <typename Msg>
    void setHandle(Topic topic, EventBusHandle<Msg>&& handle) {
        subscriptionHandle[static_cast<size_t>(topic)] = new EventBusHandle<Msg>(std::move(handle));
    }

    std::array<void*, static_cast<size_t>(Topic::COUNT)> subscriptionHandle {};

  private:
    void handleReceive(const std::string& data);
    void send(size_t clientId, const char* data, size_t len) override;

    struct ServerCb : BLEServerCallbacks {
        BluetoothService* svc;
        ServerCb(BluetoothService* s) : svc(s) {}
        void onConnect(BLEServer*) override { svc->connected = true; }
        void onDisconnect(BLEServer* s) override {
            svc->connected = false;
            for (size_t i = 0; i < static_cast<size_t>(Topic::COUNT); ++i) svc->unsubscribe(static_cast<Topic>(i), 0);
            svc->bleServer->startAdvertising();
        }
    };

    struct RxCb : BLECharacteristicCallbacks {
        BluetoothService* svc;
        RxCb(BluetoothService* s) : svc(s) {}
        void onWrite(BLECharacteristic* c) override {
            auto v = c->getValue();
            if (!v.empty()) svc->handleReceive(v);
        }
    };

  public:
    void begin(const char* name);
    void loop() {}
};