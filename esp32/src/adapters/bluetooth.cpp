#include <adapters/bluetooth.hpp>
#include <topic.hpp>
#include <ArduinoJson.h>

static constexpr auto SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr auto TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";
static constexpr auto RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";

void BluetoothService::begin(const char* name) {
#define X(e, t)                                                                  \
    setHandle<t>(Topic::e, EventBus<t>::subscribe([this](const t* d, size_t n) { \
                     if (connected && n) emit<Topic::e>(d[0]);                   \
                 }));
    TOPIC_LIST
#undef X

    BLEDevice::init(name);
    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new ServerCb(this));
    auto* svc = bleServer->createService(SERVICE_UUID);
    txCharacteristic = svc->createCharacteristic(TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
    txCharacteristic->addDescriptor(new BLE2902());
    rxCharacteristic = svc->createCharacteristic(RX_UUID, BLECharacteristic::PROPERTY_WRITE);
    rxCharacteristic->setCallbacks(new RxCb(this));
    svc->start();
    bleServer->getAdvertising()->start();
}

void BluetoothService::handleReceive(const std::string& data) {
    JsonDocument doc;
#if USE_MSGPACK
    if (deserializeMsgPack(doc, data)) return;
#else
    if (deserializeJson(doc, data)) return;
#endif

    serializeJson(doc, Serial);
    Serial.println();

    auto payload = doc.as<JsonArrayConst>();

    MsgKind msgKind = static_cast<MsgKind>(payload[0].as<uint8_t>());
    switch (msgKind) {
        case MsgKind::Connect:
            for (size_t i = 1; i < payload.size(); ++i) subscribe(static_cast<Topic>(payload[i].as<uint8_t>()), 0);
            break;

        case MsgKind::Disconnect:
            for (size_t i = 1; i < payload.size(); ++i) {
                Topic t = static_cast<Topic>(payload[i].as<uint8_t>());
                unsubscribe(t, 0);
#define X(e, m)                            \
    if (t == Topic::e) {                   \
        auto* h = &getHandle<m>(Topic::e); \
        if (h->valid()) h->unsubscribe();  \
    }
                TOPIC_LIST
#undef X
            }
            break;

        case MsgKind::Event:
            if (payload.size() < 3) break;
            switch (static_cast<Topic>(payload[1].as<uint8_t>())) {
#define X(e, m) \
    case Topic::e: EventBus<m>::publishAsync(parse<m>(payload[2]), getHandle<m>(Topic::e)); break;
                TOPIC_LIST
#undef X
                default: break;
            }
            break;

        default: break;
    }
}

void BluetoothService::send(size_t, const char* data, size_t len) {
    if (!connected) return;
    txCharacteristic->setValue((uint8_t*)data, len);
    txCharacteristic->notify();
}